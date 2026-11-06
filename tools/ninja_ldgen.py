"""Keep ESP-IDF's linker generator invocation out of a long Windows cmd line.

Only generated Ninja files are adjusted. ESP-IDF's original linker generator,
fragments, config, libraries and objdump are used without modification.
"""
from __future__ import annotations
import argparse
import json
import os
import runpy
import shlex
import subprocess
import sys
from pathlib import Path


def rewrite(directory: Path) -> None:
    directory = directory.resolve()
    path = directory / "build.ninja"
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    count = 0
    for index, line in enumerate(lines):
        if not line.lstrip().startswith("COMMAND = ") or "/tools/ldgen/ldgen.py " not in line:
            continue
        command = line.partition(" && ")[2].strip()
        if not command.endswith('"'):
            raise ValueError("Unrecognized ESP-IDF linker-generator command")
        command = command[:-1].replace("$:", ":").replace("$$", "$")
        tokens = [token.strip('"') for token in shlex.split(command, posix=False)]
        if len(tokens) < 3 or Path(tokens[1]).name != "ldgen.py":
            raise ValueError("Unrecognized ESP-IDF linker-generator arguments")
        arguments_file = directory / "nexus-ldgen-arguments.json"
        arguments_file.write_text(json.dumps({"script": tokens[1], "arguments": tokens[2:],
                                             "directory": str(directory)}, indent=2), encoding="utf-8")
        short = subprocess.list2cmdline([tokens[0], str(Path(__file__).resolve()),
                                         "--arguments", str(arguments_file)])
        lines[index] = "  COMMAND = " + short.replace("$", "$$") + "\n"
        count += 1
    if count != 1:
        if any("ninja_ldgen.py --arguments" in line for line in lines):
            return
        raise ValueError(f"Expected one linker-generator rule, found {count}")
    path.write_text("".join(lines), encoding="utf-8")
    print("Using ESP-IDF linker generator with a short Windows invocation")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--rewrite-build-dir")
    group.add_argument("--arguments")
    args = parser.parse_args()
    if args.rewrite_build_dir:
        rewrite(Path(args.rewrite_build_dir))
        return
    invocation = json.loads(Path(args.arguments).read_text(encoding="utf-8"))
    script = Path(invocation["script"]).resolve()
    if script.name != "ldgen.py" or script.parent.name != "ldgen" or not script.is_file():
        raise ValueError("Invalid ESP-IDF linker-generator path")
    os.chdir(invocation["directory"])
    sys.path.insert(0, str(script.parent))
    sys.argv = [str(script), *invocation["arguments"]]
    runpy.run_path(str(script), run_name="__main__")


if __name__ == "__main__":
    main()
