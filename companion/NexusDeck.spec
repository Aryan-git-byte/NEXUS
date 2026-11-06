# Build from the repository root: python -m PyInstaller companion/NexusDeck.spec --noconfirm
from pathlib import Path
import json
import sys
from PyInstaller.utils.hooks import collect_submodules
from companion import __version__

root = Path(SPECPATH).parent
hidden = collect_submodules('winrt') + collect_submodules('pycaw') + collect_submodules(
    'comtypes', filter=lambda name: not name.startswith('comtypes.test'))
hidden += ['bleak.backends.winrt', 'pynvml']
a = Analysis([str(root / 'companion/main.py')], pathex=[str(root)],
             binaries=[], datas=[(str(root / 'profiles'), 'profiles'), (str(root / 'assets'), 'assets')],
             hiddenimports=hidden,
             excludes=['PySide6.QtWebEngineCore', 'PySide6.QtWebEngineWidgets',
                       'PySide6.QtWebEngineQuick', 'PySide6.QtPdf', 'PySide6.QtPdfWidgets',
                       'tkinter', 'pytest', 'comtypes.test'], noarchive=False)
if sys.platform == 'win32':
    # Qt 6.12 imports the unversioned Windows ICU API (for example ucnv_open).
    # A foreign ICU DLL on PATH can be collected despite exporting only suffixed
    # symbols such as ucnv_open_78. Windows 11 supplies these DLLs itself.
    windows_icu = {'icu.dll', 'icuuc.dll', 'icuin.dll', 'icudt.dll'}
    a.binaries = [entry for entry in a.binaries if Path(entry[0]).name.lower() not in windows_icu]
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='NexusDeck',
          debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
          console=False, icon=str(root / 'companion/assets/nexus.ico'))
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='NexusDeck')
package_directory = Path(DISTPATH) / 'NexusDeck'
(package_directory / 'package-info.json').write_text(
    json.dumps({'name': 'NEXUS DECK', 'version': __version__}, indent=2) + '\n', encoding='utf-8')
