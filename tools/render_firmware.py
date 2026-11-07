"""Compile/render the real firmware UI with LVGL 9 and explicit hardware fixtures."""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from PIL import Image
from companion.icons import builtin_icon
from companion.profiles import ProfileManager


def execute(command: list[str],log_path: Path,environment: dict[str,str]) -> None:
    log_path.parent.mkdir(parents=True,exist_ok=True)
    with log_path.open("w",encoding="utf-8") as log:
        process = subprocess.Popen(command,cwd=ROOT,env=environment,stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace")
        for line in process.stdout:
            log.write(line); log.flush()
            if "error:" in line.lower() or line.startswith(("PASS:","FAIL:","landscape:","portrait:")):
                print(line.rstrip(),flush=True)
        code = process.wait()
    if code:
        print(log_path.read_text("utf-8")[-7000:],file=sys.stderr)
        raise SystemExit(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir",default="test-results/lvgl-preview")
    parser.add_argument("--idf-path",default=os.environ.get("IDF_PATH","C:/Espressif/frameworks/esp-idf-v5.5.4"))
    parser.add_argument("--skip-build",action="store_true")
    args = parser.parse_args()
    directory = (ROOT / args.output_dir).resolve()
    directory.mkdir(parents=True,exist_ok=True)
    environment = os.environ.copy()
    environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".cache/zig-global")
    environment["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / ".cache/zig-local")
    executable = directory / "build/nexus_ui_preview.exe"
    if not args.skip_build:
        package = importlib.util.find_spec("ziglang")
        if package is None:
            raise SystemExit("Install the optional host compiler: python -m pip install ziglang==0.13.0")
        compiler = Path(package.origin).parent / ("zig.exe" if os.name == "nt" else "zig")
        cmake = shutil.which("cmake")
        if not cmake:
            candidates = sorted(Path("C:/Espressif/tools/cmake").glob("*/bin/cmake.exe"))
            cmake = str(candidates[-1]) if candidates else None
        if not cmake:
            raise SystemExit("Install CMake or run from an ESP-IDF terminal")
        print("Configuring the headless LVGL renderer…",flush=True)
        execute([cmake,"-S",str(ROOT / "tools/lvgl_preview"),"-B",str(directory / "build"),"-G","Ninja",
                 f"-DCMAKE_C_COMPILER={compiler}","-DCMAKE_C_COMPILER_ARG1=cc",
                 f"-DCMAKE_CXX_COMPILER={compiler}","-DCMAKE_CXX_COMPILER_ARG1=c++",
                 "-DCMAKE_ASM_COMPILER_ARG1=cc",
                 "-DCMAKE_BUILD_TYPE=Release",f"-DIDF_PATH={args.idf_path}"],directory / "host-configure.log",environment)
        print("Building the actual LVGL pages…",flush=True)
        execute([cmake,"--build",str(directory / "build"),"--parallel","4"],directory / "host-build.log",environment)
    manager = ProfileManager(directory / "fixture-data")
    profiles = manager.snapshot()
    # Test imported, animated color artwork through the device profile's asset reference.
    asset = builtin_icon("server")
    profiles["custom"]["buttons"][0]["icon_image"] = asset.json()
    bundle = manager.bundle("default",profiles=profiles)
    fixture = directory / "fixture-profile.json"
    fixture.write_text(json.dumps(bundle),"utf-8")
    icons = directory / "fixture-icons"
    icons.mkdir(exist_ok=True)
    (icons / f"{asset.id}.ndi").write_bytes(asset.data)
    print("Rendering portrait/landscape and every settings popup…",flush=True)
    execute([str(directory / "build/nexus_apps_tests.exe")],directory / "local-app-tests.log",environment)
    execute([str(directory / "build/nexus_ota_tests.exe")],directory / "ota-tests.log",environment)
    execute([str(executable),str(fixture),str(directory),str(icons)],directory / "layout-checks.log",environment)
    count = 0
    for source in directory.glob("*.ppm"):
        with Image.open(source) as image:
            image.save(source.with_suffix(".png"))
        count += 1
    print(f"Saved {count} actual LVGL screen images to {directory}",flush=True)


if __name__ == "__main__":
    main()
