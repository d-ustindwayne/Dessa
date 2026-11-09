#!/usr/bin/env python3
"""Runs the test programs with the compiler's memory checkers switched on
(AddressSanitizer and UndefinedBehaviorSanitizer): reading past the end of
something, using freed memory, integer mistakes in the runtime. Each program's C
is built together with the runtime with DS_NO_POOL, which makes every allocation
visible to the checkers. Linux or macOS, with gcc or clang.

Leaks aren't counted: an error caught by try/catch leaves the temporary values
of the statement it interrupted (setjmp can't release them; see docs/compiler.md).

    python tests/sanitize.py [filter]
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = Path(os.environ["DESSA_EXE"]).resolve() if os.environ.get("DESSA_EXE") else ROOT / "build" / "dessa"
CASES = ROOT / "tests" / "cases" / "run"
RUNTIME = ROOT / "runtime"
INCLUDE = ROOT / "include"
PACKAGES = ROOT / "tools" / "riv" / "packages"
CC = os.environ.get("CC", "gcc")
FLAGS = ["-std=gnu11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer", "-DDS_NO_POOL",
         f"-I{RUNTIME}"]
ENV = dict(os.environ, DESSA_PACKAGES=str(PACKAGES), ASAN_OPTIONS="detect_leaks=0:detect_stack_use_after_return=0:allocator_may_return_null=1",
           UBSAN_OPTIONS="print_stacktrace=1")
# Sources of a library's C part, by the name its generated bridge uses
LIBS = {"math": INCLUDE / "math" / "math.c", "os": INCLUDE / "os" / "os.c", "time": INCLUDE / "time" / "time.c", "dcrypto": INCLUDE / "dcrypto" / "dcrypto.c",
        "sql": PACKAGES / "sql" / "sql.c", "request": PACKAGES / "request" / "request.c", "http": PACKAGES / "http" / "http.c"}
# Runs with its own limits, or can't be told from a crash by these tools
SKIP = {"stack_overflow", "ashwee_lib", "torrent_lib", "mssql"}


def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else ""
    objects = {}
    failed = passed = skipped = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        runtime = tmp / "rt.o"
        subprocess.run([CC, *FLAGS, "-c", str(RUNTIME / "dessa_rt.c"), "-o", str(runtime)], check=True)
        for case in sorted(CASES.glob("*.ds")):
            if pattern not in case.name:
                continue
            text = case.read_text(encoding="utf-8")
            if case.stem in SKIP or "// needs: display" in text or "// needs: mssql" in text:
                skipped += 1
                continue
            emitted = subprocess.run([str(EXE), "emit", str(case.relative_to(ROOT)), "--target", "c"], cwd=ROOT, env=ENV, capture_output=True, text=True, encoding="utf-8")
            if emitted.returncode != 0 or not emitted.stdout.strip():
                skipped += 1      # needs C++: not this tool's subject
                continue
            source = tmp / (case.stem + ".c")
            source.write_text(emitted.stdout, encoding="utf-8")
            libraries = []
            second = emitted.stdout.split("\n", 2)[1] if "\n" in emitted.stdout else ""
            uses = second.split("dessa-glue:")[1].split("*/")[0].split() if "dessa-glue:" in second else []
            unknown = [u for u in uses if u not in LIBS]
            if unknown:
                skipped += 1
                continue
            for name in uses:
                if name not in objects:
                    obj = tmp / f"{name}.o"
                    subprocess.run([CC, *FLAGS, f"-I{LIBS[name].parent}", "-c", str(LIBS[name]), "-o", str(obj)], check=True)
                    objects[name] = obj
                libraries.append(str(objects[name]))
                if name == "sql":
                    sqlite = tmp / "sqlite3.o"
                    if not sqlite.exists():
                        # Without the checkers: it is someone else's code, and slow with them
                        subprocess.run([CC, "-std=gnu11", "-O1", "-c", str(PACKAGES / "sql" / "sqlite" / "sqlite3.c"), "-o", str(sqlite)], check=True)
                    libraries.append(str(sqlite))
            program = tmp / case.stem
            built = subprocess.run([CC, *FLAGS, str(source), str(runtime), *libraries, "-lm", "-ldl", "-lpthread", "-o", str(program)], capture_output=True, text=True, encoding="utf-8", errors="replace")
            if built.returncode != 0:
                failed += 1
                print(f"FAIL {case.name}: could not build\n{built.stderr[-1500:]}")
                continue
            stdin = case.with_suffix(".in").read_text(encoding="utf-8") if case.with_suffix(".in").exists() else ""
            try:
                run = subprocess.run([str(program)], cwd=ROOT, env=ENV, input=stdin, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=240)
            except subprocess.TimeoutExpired:
                failed += 1
                print(f"FAIL {case.name}: timed out")
                continue
            if "ERROR: AddressSanitizer" in run.stderr or "runtime error:" in run.stderr:
                failed += 1
                print(f"FAIL {case.name}\n{run.stderr[:3000]}")
            else:
                passed += 1
                print(f"ok   {case.name}")
    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
