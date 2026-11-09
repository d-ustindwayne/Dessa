"""Compares the compiler written in Dessa (selfhost/) with the C++ one: for
every Dessa file in the repository, component files (.xds) included, and for
some of them saved in other encodings too, both must give the same tokens
(`dessa tokens`) and the same tree (`dessa ast`).

    python selfhost/check.py
"""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")
EXTENSIONS = {".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss", ".xds", ".xdz", ".xdx"}
# Source files saved another way: each is read as UTF-8 (sourceToUtf8), and
# what isn't valid text is refused
ENCODINGS = {
    "utf-8-bom": lambda text: b"\xef\xbb\xbf" + text.encode("utf-8"),
    "utf-16le": lambda text: b"\xff\xfe" + text.encode("utf-16-le"),
    "utf-16be": lambda text: b"\xfe\xff" + text.encode("utf-16-be"),
    "utf-32le": lambda text: b"\xff\xfe\x00\x00" + text.encode("utf-32-le"),
    "utf-32be": lambda text: b"\x00\x00\xfe\xff" + text.encode("utf-32-be"),
    "broken": lambda text: b"\xff\xfe" + text.encode("utf-16-le") + b"A",   # a byte left over
    "crlf": lambda text: text.replace("\n", "\r\n").encode("utf-8"),
    "invalid": lambda text: text.encode("utf-8") + b"\n\xc0\x80\n",          # not UTF-8 at the end
}


STAGES = ["tokens", "ast"]


def compare(tool, copy, label):
    """True when every stage gives the same output"""
    for stage in STAGES:
        if not compare_stage(tool, copy, label, stage):
            return False
    return True


def compare_stage(tool, copy, label, stage):
    expected = subprocess.run([str(EXE), stage, str(copy)], capture_output=True)
    got = subprocess.run([str(tool), stage, str(copy)], capture_output=True)
    if expected.stdout == got.stdout and expected.returncode == got.returncode:
        return True
    a = expected.stdout.decode("utf-8", "replace").splitlines()
    b = got.stdout.decode("utf-8", "replace").splitlines()
    line = next((i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b) or a[i] != b[i]), 0)
    if expected.stdout == got.stdout:
        print(f"FAIL {label} ({stage}: exit code {expected.returncode} vs {got.returncode})")
        return False
    print(f"FAIL {label} ({stage}, line {line + 1})")
    print(f"     C++:   {a[line] if line < len(a) else '<end>'!r}")
    print(f"     Dessa: {b[line] if line < len(b) else '<end>'!r}")
    return False


def main():
    # Build the Dessa lexer once
    tool = pathlib.Path(tempfile.gettempdir()) / ("dessa_selfhost" + (".exe" if os.name == "nt" else ""))
    build = subprocess.run([str(EXE), "build", str(ROOT / "selfhost" / "main.ds"), "-o", str(tool)], capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout + build.stderr)
        return 1

    files = sorted(p for p in ROOT.rglob("*") if p.suffix in EXTENSIONS and "build" not in p.parts and "node_modules" not in p.parts)
    # As git gives them on Linux, so every platform checks the same bytes (CRLF is one of the copies)
    sources = {path: path.read_bytes().replace(b"\r\n", b"\n") for path in files}
    # In other encodings: a few files with text beyond ASCII, and a component file
    encoded = [p for p in files if any(byte >= 0x80 for byte in sources[p])][:4]
    encoded += [p for p in files if p.suffix == ".xds"][:1]
    failed = 0
    checks = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in files:
            copy = pathlib.Path(tmp) / path.name
            copy.write_bytes(sources[path])
            checks += 1
            failed += not compare(tool, copy, path.relative_to(ROOT).as_posix())
        for path in encoded:
            text = sources[path].decode("utf-8")
            for name, encode in ENCODINGS.items():
                copy = pathlib.Path(tmp) / f"{name}_{path.name}"
                copy.write_bytes(encode(text))
                checks += 1
                failed += not compare(tool, copy, f"{path.relative_to(ROOT).as_posix()} as {name}")
    print(f"{checks - failed} of {checks} files give the same tokens and tree ({len(files)} files, {len(encoded) * len(ENCODINGS)} encoded copies)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
