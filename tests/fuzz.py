#!/usr/bin/env python3
"""Feeds the compiler broken programs: whatever it is given, `dessa check` has to
answer with a result or an error message. A crash or a hang is a bug.

Each round takes a real program from the repository and damages it (bytes
changed, pieces cut, repeated or swapped, deep nesting added). A failure leaves
the input in tests/fuzz-failures/ to reproduce it. The same seed gives the same
programs, so a failure in CI can be repeated:

    python tests/fuzz.py [--seconds 60] [--seed 1] [--jobs 4]
"""
import argparse
import concurrent.futures
import os
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = Path(os.environ["DESSA_EXE"]).resolve() if os.environ.get("DESSA_EXE") else ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")
FAILURES = ROOT / "tests" / "fuzz-failures"
# Pieces of the language, so damage often stays close to something valid
TOKENS = [b"func[Int] f:T(", b"class A:T {", b"$\"{", b"}\"", b"\"\"\"", b"<", b">", b"<>", b"<<", b">>", b"(", b")", b"[", b"]", b"{", b"}",
          b"new ", b"?.", b"??", b"?!", b"!!", b"|>", b":=", b"...", b"is ", b"Map[Str, ", b"Func[Int, [", b"$(\"", b"@override ", b"init(", b"super(",
          b"this.", b"catch (e[", b"try {", b"switch (", b"case ", b"default :=", b"enum E {", b"interface I {", b"var[", b"const[", b"\x00", b"\xff\xfe",
          b"\xf0\x9f\x98\x80", b"0x", b"0b2", b"99999999999999999999999", b"'\\x", b"\\u", b"/*", b"*/", b"//", b"\n", b"yes", b"nil"]


def mutate(source, rng):
    data = bytearray(source)
    for _ in range(rng.randint(1, 8)):
        if not data:
            data = bytearray(rng.choice(TOKENS))
        at = rng.randrange(len(data))
        kind = rng.randrange(8)
        if kind == 0:
            data[at] = rng.randrange(256)
        elif kind == 1:
            del data[at:at + rng.randint(1, 64)]
        elif kind == 2:
            data[at:at] = rng.choice(TOKENS)
        elif kind == 3:
            piece = data[at:at + rng.randint(1, 80)]
            data[rng.randrange(len(data)):0] = piece
        elif kind == 4:
            del data[at:]
        elif kind == 5:
            data[at:at] = rng.choice(TOKENS) * rng.randint(2, 3000)
        elif kind == 6:
            other = rng.randrange(len(data))
            data[at], data[other] = data[other], data[at]
        else:
            piece = data[at:at + rng.randint(1, 16)]
            data[at:at] = bytes(piece) * rng.randint(2, 500)
    return bytes(data)


def one_round(seed, sources, folder):
    rng = random.Random(seed)
    # In its own kind of file: .xds is lexed differently
    source, suffix = rng.choice(sources)
    data = mutate(source, rng)
    file = Path(folder) / f"fuzz_{seed}{suffix}"
    file.write_bytes(data)
    try:
        result = subprocess.run([str(EXE), "check", str(file)], capture_output=True, timeout=30, cwd=folder)
        # 0: fine, 1: errors in the program. Anything else is the compiler dying
        problem = None if result.returncode in (0, 1) else f"exit code {result.returncode & 0xffffffff:#x}"
    except subprocess.TimeoutExpired:
        problem = "took more than 30 seconds"
    if problem:
        FAILURES.mkdir(exist_ok=True)
        (FAILURES / f"fuzz_{seed}{suffix}").write_bytes(data)
    file.unlink(missing_ok=True)
    return seed, problem


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    options = parser.parse_args()
    files = [p for folder in ("tests/cases", "sample", "include", "tools/riv/packages") for p in (ROOT / folder).rglob("*")
             if p.suffix in (".ds", ".dm", ".xds") and p.stat().st_size < 60000]
    sources = [(p.read_bytes(), p.suffix) for p in sorted(files)]
    end = time.time() + options.seconds
    rounds = 0
    failures = []
    seed = options.seed * 1000000
    with tempfile.TemporaryDirectory() as folder, concurrent.futures.ThreadPoolExecutor(options.jobs) as pool:
        running = set()
        while time.time() < end or running:
            while time.time() < end and len(running) < options.jobs * 2:
                running.add(pool.submit(one_round, seed, sources, folder))
                seed += 1
            done, running = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                rounds += 1
                number, problem = future.result()
                if problem:
                    failures.append((number, problem))
                    print(f"FAIL seed {number}: {problem} (tests/fuzz-failures/fuzz_{number}.ds)", flush=True)
    print(f"{rounds} programs from {len(sources)} sources, {len(failures)} problem(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
