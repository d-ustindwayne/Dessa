"""Compares the Dessa lexer (selfhost/lexer.dm) with the C++ one: both must
give the same tokens for every Dessa file in the repository.

    python selfhost/check.py
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")
EXTENSIONS = {".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"}


def main():
    # Build the Dessa lexer once
    tool = pathlib.Path(tempfile.gettempdir()) / ("dessa_selfhost_lexer" + (".exe" if os.name == "nt" else ""))
    build = subprocess.run([str(EXE), "build", str(ROOT / "selfhost" / "main.ds"), "-o", str(tool)], capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 1

    files = sorted(p for p in ROOT.rglob("*") if p.suffix in EXTENSIONS and "build" not in p.parts and "node_modules" not in p.parts)
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in files:
            # Same line endings for both (the C++ command reads in text mode)
            copy = pathlib.Path(tmp) / path.name
            copy.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
            expected = subprocess.run([str(EXE), "tokens", str(copy)], capture_output=True)
            got = subprocess.run([str(tool), "tokens", str(copy)], capture_output=True)
            if expected.stdout != got.stdout:
                failed += 1
                a = expected.stdout.decode("utf-8", "replace").splitlines()
                b = got.stdout.decode("utf-8", "replace").splitlines()
                line = next((i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b) or a[i] != b[i]), 0)
                print(f"FAIL {path.relative_to(ROOT).as_posix()} (token {line + 1})")
                print(f"     C++:   {a[line] if line < len(a) else '<end>'!r}")
                print(f"     Dessa: {b[line] if line < len(b) else '<end>'!r}")
    print(f"{len(files) - failed} of {len(files)} files give the same tokens")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
