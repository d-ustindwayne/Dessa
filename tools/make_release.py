#!/usr/bin/env python3
"""Packs a prebuilt Dessa into an archive users can download.

    python tools/make_release.py <build-folder> [output-folder]

The build folder must contain dessa and riv (cmake --build <build> --target riv).
Creates dessa-<version>-<os>-<arch>.zip (Windows) or .tar.gz (Linux/macOS):

    dessa-0.2.1-windows-x64/
        bin/dessa(.exe) bin/riv(.exe)
        runtime/  include/              the core
        tools/riv/packages/             riv packages (riv installs them from here without downloading)
        tools/vscode-dessa/             the VS Code extension
        web/dessa.js  web/example/        Dessa in the browser (when built)
        install.ps1  install.sh         they see bin/ and skip building
        LICENSE  NOTICE  README.md
"""

import os
import platform
import re
import shutil
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def version():
    text = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
    return re.search(r'kVersion\s*=\s*"([^"]+)"', text).group(1)


def target_name():
    system = {"Windows": "windows", "Linux": "linux", "Darwin": "macos"}.get(platform.system(), platform.system().lower())
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    build = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "dist"
    exe = ".exe" if os.name == "nt" else ""
    for program in ("dessa", "riv"):
        if not (build / f"{program}{exe}").exists():
            print(f"missing {build / (program + exe)}; build it first (cmake --build <build> --target riv)")
            return 1

    name = f"dessa-{version()}-{target_name()}"
    stage = out / name
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "bin").mkdir(parents=True)
    for program in ("dessa", "riv"):
        shutil.copy2(build / f"{program}{exe}", stage / "bin")

    ignore = shutil.ignore_patterns("riv_modules", "__pycache__", "*.o", "node_modules")
    for folder in ("runtime", "include", "tools/riv/packages", "tools/vscode-dessa"):
        shutil.copytree(ROOT / folder, stage / folder, ignore=ignore)
    # phl is left out for now (its syntax is being revised)
    shutil.rmtree(stage / "tools" / "riv" / "packages" / "phl", ignore_errors=True)
    if (ROOT / "web" / "dessa.js").exists():   # the browser build (emcmake ... build-wasm)
        shutil.copytree(ROOT / "web", stage / "web", ignore=ignore)
    for file in ("install.ps1", "install.sh", "LICENSE", "NOTICE", "README.md"):
        shutil.copy2(ROOT / file, stage / file)

    out.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        archive = out / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            for path in sorted(stage.rglob("*")):
                z.write(path, path.relative_to(out))
    else:
        archive = out / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as t:
            t.add(stage, arcname=name)   # keeps the executable bits
    shutil.rmtree(stage)
    print(f"{archive}  ({archive.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
