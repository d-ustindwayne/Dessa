"""Runs the same benchmark in C++, Java, JavaScript, Python and Dessa (native
and compiled to JavaScript), checks that every language got the same
results, and writes RESULTS.md and results.csv next to this file.

    python tests/bench/run_bench.py            the full sizes (a billion-step loop)
    python tests/bench/run_bench.py --quick    a tenth of the loop, for a quick check
    python tests/bench/run_bench.py --only dessa,c++

Needs: g++, javac/java, node, python, and dessa built in build/.
"""
import csv
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
EXE = ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")
TASKS = ["loop", "fib", "sieve", "strings", "sorting", "maps", "objects", "mandelbrot"]
ABOUT = {
    "loop": "1,000,000,000 steps of acc = (acc * 31 + i) % 1000000007",
    "fib": "naive recursive Fibonacci of 32 (about 7 million calls)",
    "sieve": "primes up to 10,000,000 (sieve of Eratosthenes)",
    "strings": "building one text from the numbers 0 to 999,999",
    "sorting": "sorting 1,000,000 random numbers",
    "maps": "1,000,000 inserts and 1,000,000 lookups in a hash map",
    "objects": "3,000,000 steps that each make 2 small objects",
    "mandelbrot": "800 x 600 Mandelbrot set, up to 100 steps per point",
}


def run(command, timeout=None):
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout)


def version(command):
    try:
        result = run(command)
        text = (result.stdout + result.stderr).strip().splitlines()
        return text[0] if text else "?"
    except (OSError, subprocess.SubprocessError):
        return "not found"


def cpu_name():
    if os.name == "nt":
        result = run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name"])
        if result.stdout.strip():
            return result.stdout.strip().splitlines()[0]
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "?"


def main():
    quick = "--quick" in sys.argv
    # --runs N: every language N times, round after round; each task keeps its best time
    runs = int(sys.argv[sys.argv.index("--runs") + 1]) if "--runs" in sys.argv else 1
    only = None
    if "--only" in sys.argv:
        only = {n.strip().lower() for n in sys.argv[sys.argv.index("--only") + 1].split(",")}
    build = Path(tempfile.mkdtemp(prefix="dessa_bench_"))
    exe = ".exe" if os.name == "nt" else ""
    args = ["quick"] if quick else []

    # name -> (how to build, how to run, version)
    languages = [
        ("C++", [["g++", "-O2", "-std=c++20", str(HERE / "bench.cpp"), "-o", str(build / f"bench_cpp{exe}")]],
         [str(build / f"bench_cpp{exe}")], version(["g++", "--version"])),
        ("Java", [["javac", "-d", str(build), str(HERE / "Bench.java")]],
         ["java", "-cp", str(build), "Bench"], version(["java", "-version"])),
        ("JavaScript", [], ["node", str(HERE / "bench.js")], "Node " + version(["node", "--version"])),
        ("Python", [], [sys.executable, str(HERE / "bench.py")], version([sys.executable, "--version"])),
        ("Dessa", [[str(EXE), "build", str(HERE / "bench.ds"), "--release", "-o", str(build / f"bench_ds{exe}")]],
         [str(build / f"bench_ds{exe}")], version([str(EXE), "--version"]) + " (native, through C: the default)"),
        ("Dessa (C++)", [[str(EXE), "build", str(HERE / "bench.ds"), "--release", "--cpp", "-o", str(build / f"bench_dscpp{exe}")]],
         [str(build / f"bench_dscpp{exe}")], version([str(EXE), "--version"]) + " (native, through C++: --cpp)"),
        ("Dessa (wasm)", [[str(EXE), "build", str(HERE / "bench.ds"), "--target", "wasm", "-o", str(build / "bench_dsw.wasm")]],
         ["node", str(build / "bench_dsw.js")], version([str(EXE), "--version"]) + " compiled to WebAssembly, Node"),
    ]

    results = {}   # language -> task -> (result, ms)
    totals = {}
    built = set()
    for round_ in range(runs):
        if runs > 1:
            print(f"#### round {round_ + 1} of {runs}", flush=True)
        for name, steps, command, about in languages:
            if only and name.lower() not in only:
                continue
            print(f"== {name}: {about}", flush=True)
            failed = False
            for step in steps if name not in built else []:
                done = run(step)
                if done.returncode != 0:
                    print(f"   could not build: {(done.stdout + done.stderr).strip()[:500]}")
                    failed = True
                    break
            if failed:
                continue
            built.add(name)
            start = time.perf_counter()
            process = subprocess.Popen(command + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            by_task = results.setdefault(name, {})
            for line in process.stdout:
                parts = line.rstrip("\n").split("\t")
                if len(parts) == 3 and parts[0] in TASKS:
                    ms = int(parts[2])
                    if parts[0] not in by_task or ms < by_task[parts[0]][1]:
                        by_task[parts[0]] = (parts[1], ms)
                    print(f"   {parts[0]:<11} {ms:>9,} ms   result {parts[1]}", flush=True)
                elif line.strip():
                    print("   " + line.rstrip())
            process.wait()
            totals[name] = min(totals.get(name, float("inf")), time.perf_counter() - start)

    # Every language must agree with C++ (or with the first one that ran)
    reference = results.get("C++") or next(iter(results.values()), {})
    problems = []
    for name, by_task in results.items():
        for task in TASKS:
            if task not in by_task:
                problems.append(f"{name}: no result for {task}")
            elif task in reference and by_task[task][0] != reference[task][0]:
                problems.append(f"{name}: {task} gave {by_task[task][0]}, expected {reference[task][0]}")

    write_results(results, totals, languages, quick, problems, runs)
    shutil.rmtree(build, ignore_errors=True)
    print()
    if problems:
        print("RESULTS DIFFER:")
        for p in problems:
            print("  " + p)
        return 1
    print(f"All languages gave the same results. Written to {HERE / 'RESULTS.md'}")
    return 0


def write_results(results, totals, languages, quick, problems, runs=1):
    names = [n for n, *_ in languages if n in results]
    with open(HERE / "results.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["task"] + names)
        for task in TASKS:
            w.writerow([task] + [results[n].get(task, ("", ""))[1] for n in names])

    lines = ["# Benchmark results", ""]
    lines.append(f"Measured {time.strftime('%Y-%m-%d %H:%M')} on {cpu_name()}, {platform.system()} {platform.release()}"
                 f"{' (quick sizes: the loop is 100,000,000 steps)' if quick else ''}.")
    lines.append("")
    lines.append("Times are milliseconds for each task alone (lower is faster)" +
                 (f", the best of {runs} runs taken in turns." if runs > 1 else ".") + " Every language ran the")
    lines.append("same algorithm and gave the same result" + (" (see the problems below)." if problems else "."))
    lines.append("")
    lines.append("| Task | " + " | ".join(names) + " |")
    lines.append("|---|" + "---:|" * len(names))
    for task in TASKS:
        row = [f"{results[n][task][1]:,}" if task in results[n] else "-" for n in names]
        lines.append(f"| {task} | " + " | ".join(row) + " |")
    task_sums = [sum(ms for _, ms in results[n].values()) for n in names]
    lines.append("| **all tasks** | " + " | ".join(f"**{t:,}**" for t in task_sums) + " |")
    if "C++" in results:
        base = sum(ms for _, ms in results["C++"].values()) or 1
        lines.append("| times C++ | " + " | ".join(f"{t / base:.1f}x" for t in task_sums) + " |")
    lines.append("")
    lines.append("Whole run, including starting the program (seconds): " +
                 ", ".join(f"{n} {totals[n]:.1f}" for n in names) + ".")
    lines.append("")
    lines.append("## The tasks")
    lines.append("")
    for task in TASKS:
        lines.append(f"- **{task}**: {ABOUT[task]}")
    lines.append("")
    lines.append("## Versions")
    lines.append("")
    for name, _, _, about in languages:
        if name in results:
            lines.append(f"- {name}: {about}")
    lines.append("")
    lines.append("## Notes")
    lines.append("")
    lines.append("- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)")
    lines.append("  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and")
    lines.append("  every list index, which the C++ version doesn't.")
    lines.append("- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly")
    lines.append("  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.")
    lines.append("- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day")
    lines.append("  on a laptop than the native ones; compare them within one run.")
    lines.append("- Java's times include the JIT warming up; Python runs in the standard interpreter.")
    if problems:
        lines.append("")
        lines.append("## Problems")
        lines.append("")
        lines.extend(f"- {p}" for p in problems)
    (HERE / "RESULTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
