#!/usr/bin/env python3
"""Makes the WebAssembly compiler download that `dessa setup wasm` installs.

    python tools/make_wasm_tools.py <platform> [--from wasi-sdk.tar.gz] [-o folder]

<platform> is windows-x64, linux-x64 or macos-arm64. It takes the wasi-sdk
release for that platform (downloaded, or the file given with --from) and
keeps only what Dessa uses: clang, the WebAssembly linker, clang's own
headers, and the WASI C library (no C++, no other tools). The result is
dessa-wasm-<platform>.tar.gz, with bin/, lib/ and share/ at the top, about a
fifth of wasi-sdk's size. The Release workflow attaches it to each release.
"""

import io
import re
import sys
import tarfile
import urllib.request
from pathlib import Path

WASI_SDK = "34"
ASSETS = {
    "windows-x64": "x86_64-windows",
    "linux-x64": "x86_64-linux",
    "macos-arm64": "arm64-macos",
}

KEEP = [
    re.compile(r"^bin/(clang|clang-\d+|wasm-ld|lld)(\.exe)?$"),
    re.compile(r"^bin/clang\.cfg$"),
    re.compile(r"^bin/(?!libclang|liblldb)[^/]+\.dll$"),         # what clang and lld need on Windows (libxml2, zlib)
    re.compile(r"^lib/lib(LLVM|clang-cpp|xml2)[^/]*\.(so|dylib)[^/]*$"),   # Linux, macOS: clang and lld are small programs using these
    re.compile(r"^lib/clang/\d+/include/"),                        # stddef.h, stdarg.h, ...
    re.compile(r"^lib/clang/\d+/lib/wasm32-unknown-wasip1/"),       # clang's builtins
    re.compile(r"^share/wasi-sysroot/include/wasm32-wasip1/(?!c\+\+/)"),
    re.compile(r"^share/wasi-sysroot/lib/wasm32-wasip1/(crt1[^/]*\.o|lib(c|m|setjmp|dl|rt|util|pthread|crypt|resolv|xnet)\.a|libwasi-emulated-[^/]+\.a)$"),
]


def keep(name):
    return any(p.search(name) for p in KEEP)


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ASSETS:
        print(__doc__)
        return 2
    platform = args[0]
    source = Path(args[args.index("--from") + 1]) if "--from" in args else None
    out_dir = Path(args[args.index("-o") + 1]) if "-o" in args else Path(".")
    out_dir.mkdir(parents=True, exist_ok=True)

    asset = f"wasi-sdk-{WASI_SDK}.0-{ASSETS[platform]}.tar.gz"
    if source is None:
        url = f"https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-{WASI_SDK}/{asset}"
        print(f"downloading {url}")
        source = out_dir / asset
        urllib.request.urlretrieve(url, source)

    target = out_dir / f"dessa-wasm-{platform}.tar.gz"
    kept = 0
    size = 0
    with tarfile.open(source, "r:gz") as src, tarfile.open(target, "w:gz") as dst:
        for member in src:
            parts = member.name.split("/", 1)
            if len(parts) < 2 or not parts[1]:
                continue
            name = parts[1]          # without the wasi-sdk-34.0-... folder
            if not keep(name) or member.isdir():
                continue
            # On Windows wasm-ld.exe is a whole copy of lld.exe (elsewhere a link to lld)
            if platform == "windows-x64" and name == "bin/lld.exe":
                continue
            member.name = name
            if member.issym() or member.islnk():
                if member.islnk():
                    member.linkname = member.linkname.split("/", 1)[-1]
                dst.addfile(member)
            else:
                dst.addfile(member, src.extractfile(member))
                size += member.size
            kept += 1
    print(f"wrote {target}: {kept} files, {size / 1e6:.0f} MB unpacked, {target.stat().st_size / 1e6:.0f} MB to download")
    return 0


if __name__ == "__main__":
    sys.exit(main())
