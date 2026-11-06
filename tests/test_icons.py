"""Imported artwork, animation timing, validation, and BLE sync regression tests."""
import asyncio
import base64
import copy
import os
from types import SimpleNamespace
from unittest.mock import Mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import pytest
from PIL import Image
from PySide6.QtTest import QTest, QSignalSpy
from PySide6.QtWidgets import QApplication
from bleak.exc import BleakError

from companion.bluetooth import BluetoothManager
from companion.config_editor import ConfigEditor
from companion.controller import Controller
from companion.icons import (MAX_BYTES, builtin_icon, builtins, decode_asset,
                             encode_frames, import_image, render_svg, validate_icon)
from companion.icon_widget import IconWidget
from companion.profiles import ICONS, ProfileManager, device_profile, validate_profile
from companion.protocol import PROFILE_UUID, STATUS_UUID, Reassembler, decode_json, encode_json, fragment
from companion.settings import Settings


@pytest.fixture(scope="module")
def app():
    yield QApplication.instance() or QApplication([])


def test_color_alpha_aspect_ratio_and_identity(tmp_path):
    source = Image.new("RGBA",(64,32),(239,40,85,160))
    path = tmp_path / "color.png"
    source.save(path)
    asset = import_image(path)
    restored = validate_icon(asset.json())
    assert restored == asset.json()
    assert len(asset.data) <= MAX_BYTES
    image = decode_asset(base64.b64decode(restored["data"])).rgba()
    pixel = image.getpixel((16,16))
    assert all(abs(pixel[index] - expected) <= 2 for index,expected in enumerate((239,40,85)))
    assert pixel[3] == 160
    assert image.getpixel((16,0))[3] == 0
    assert len(asset.id) == 14


@pytest.mark.parametrize("suffix",["gif","png","webp"])
def test_animated_raster_frames_and_durations(tmp_path,suffix):
    frames = [Image.new("RGBA",(32,32),color) for color in ("#E04050","#208DD0")]
    path = tmp_path / f"animated.{suffix}"
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=[120,240],loop=0,lossless=True)
    asset = import_image(path)
    assert len(asset.frames) == 2
    assert asset.durations == (120,240)
    assert asset.rgba(0).getpixel((16,16))[0] > 190
    assert asset.rgba(1).getpixel((16,16))[2] > 180


def test_long_animation_is_resampled_to_bounded_frames(tmp_path):
    frames = [Image.new("RGBA",(32,32),(index * 10,100,230,255)) for index in range(20)]
    path = tmp_path / "long.gif"
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=200,loop=0)
    asset = import_image(path)
    assert len(asset.frames) == 16
    assert sum(asset.durations) == 4000
    assert len(set(asset.frames)) > 1


def test_svg_full_color_import_and_rejection(app):
    asset = render_svg(b'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="#20a060"/></svg>')
    red,green,blue,alpha = asset.rgba().getpixel((16,16))
    assert green > red and green > blue and alpha == 255
    with pytest.raises(ValueError):
        render_svg(b'<!DOCTYPE svg [<!ENTITY external SYSTEM "file:///x">]><svg/>')
    with pytest.raises(ValueError):
        render_svg(b"not an SVG")


@pytest.mark.parametrize("kind",["width","frames","flags","timing","palette","truncated"])
def test_invalid_device_images_are_rejected(kind):
    original = encode_frames([Image.new("RGBA",(32,32),"red")],[100])
    bad = bytearray(original.data)
    if kind == "width": bad[4] = 31
    elif kind == "frames": bad[8] = 0
    elif kind == "flags": bad[9] = 1
    elif kind == "timing": bad[10:12] = b"\x01\x00"
    elif kind == "palette": bad[-1] = 255
    else: bad.pop()
    with pytest.raises(ValueError): decode_asset(bytes(bad))


def test_forged_icon_id_and_conflicting_legacy_encoding(tmp_path):
    value = encode_frames([Image.new("RGBA",(32,32),"red")],[100]).json()
    with pytest.raises(ValueError): validate_icon({**value,"id":"0" * 14})
    with pytest.raises(ValueError): validate_icon({**value,"data":"*"})
    profile = ProfileManager(tmp_path).snapshot()["default"]
    profile["buttons"][0].update(icon_image=value,icon_data=base64.b64encode(bytes(72)).decode())
    with pytest.raises(ValueError): validate_profile(profile)


def test_every_builtin_has_color_artwork():
    assert set(builtins()) == set(ICONS)
    for name in ICONS:
        asset = builtin_icon(name)
        validate_icon(asset.json())
        assert asset.palette and len(asset.data) <= MAX_BYTES
    assert len(builtin_icon("server").frames) > 1


def test_animation_timer_advances_and_stops_when_hidden(app):
    asset = encode_frames([Image.new("RGBA",(32,32),color) for color in ("red","blue")],[40,40])
    widget = IconWidget(asset)
    timer_signal = QSignalSpy(widget.timer.timeout)
    widget.show()
    try:
        assert timer_signal.wait(500)
        assert widget.frame == 1
        widget.hide()
        stopped_at = widget.frame
        QTest.qWait(55)
        assert widget.frame == stopped_at and not widget.timer.isActive()
    finally:
        widget.deleteLater(); app.processEvents()


def test_editor_persists_color_icon_without_changing_action(app,tmp_path,monkeypatch):
    manager = ProfileManager(tmp_path)
    controller = Controller(Settings(auto_connect=False),manager,tmp_path)
    controller.profiles_updated = Mock()
    warning = Mock()
    monkeypatch.setattr("companion.config_editor.QMessageBox.warning",warning)
    editor = ConfigEditor(manager,controller,no_tray=True)
    try:
        expected_action = copy.deepcopy(editor.draft["buttons"][0]["action"])
        editor._color_icon = builtin_icon("server").json()
        assert editor.save_profile()
        button = ProfileManager(tmp_path).profiles["default"]["buttons"][0]
        assert button["icon_image"] == editor._color_icon
        assert button["action"] == expected_action
        assert device_profile(manager.profiles["default"])["buttons"][0]["icon_asset"] == editor._color_icon["id"]
        warning.assert_not_called()
    finally:
        editor.deleteLater(); app.processEvents()


def test_icon_sync_at_minimum_mtu_deduplicates_and_resends_after_removal(tmp_path):
    async def scenario():
        profiles = ProfileManager(tmp_path).snapshot()
        asset = encode_frames([Image.new("RGBA",(32,32),"red")],[100]).json()
        for button in profiles["default"]["buttons"][:2]: button["icon_image"] = asset
        central = BluetoothManager(Settings(auto_connect=False),Mock(),Mock(),Mock(),Mock())
        central.session = "a" * 32
        central.icon_assets_supported = True
        received = []
        assembler = Reassembler()

        class Peer:
            is_connected = True
            mtu_size = 23

            async def write_gatt_char(self,channel,packet,response):
                assert channel == PROFILE_UUID and response and len(packet) <= 20
                payload = assembler.feed(packet)
                if payload is None: return
                message = decode_json(payload); received.append(message)
                if message["type"] == "icon_sync":
                    validate_icon({"id":message["asset"],"data":message["data"]})
                reply = {"type":"icon_ack" if message["type"] == "icon_sync" else "profile_ack",
                         "sequence":message["sequence"],"session":central.session,"status":"success"}
                for part in fragment(encode_json(reply),77 + len(received),20):
                    central._notify(SimpleNamespace(uuid=STATUS_UUID),bytearray(part))

        central.client = Peer()
        await central.synchronize(profiles,"default")
        assert [m["type"] for m in received] == ["icon_sync","profile_sync"]
        assert central.synced_icons == {asset["id"]}
        await central.synchronize(profiles,"default")
        assert [m["type"] for m in received][-1] == "profile_sync"
        without = copy.deepcopy(profiles)
        for button in without["default"]["buttons"]: button.pop("icon_image",None)
        await central.synchronize(without,"default")
        assert not central.synced_icons
        await central.synchronize(profiles,"default")
        assert [m["type"] for m in received][-2:] == ["icon_sync","profile_sync"]
    asyncio.run(scenario())


def test_unsupported_firmware_rejects_color_sync_before_writing(tmp_path):
    async def scenario():
        profiles = ProfileManager(tmp_path).snapshot()
        profiles["default"]["buttons"][0]["icon_image"] = builtin_icon("code").json()
        central = BluetoothManager(Settings(auto_connect=False),Mock(),Mock(),Mock(),Mock())
        central.session = "a" * 32
        central.client = SimpleNamespace(is_connected=True)
        with pytest.raises(BleakError,match="Update the deck firmware"):
            await central.synchronize(profiles,"default")
    asyncio.run(scenario())
