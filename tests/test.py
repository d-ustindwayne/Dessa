#!/usr/bin/env python3
"""Dessa test runner.

Every tests/cases/**/*.ds file is a test, except files whose name starts
with '_' and anything inside a folder named 'lib' (helper modules that
other tests import).

Two kinds of tests:

* Diagnostic tests -- the file contains at least one inline expectation:

      var[Int] x = "hi";   // error: Cannot convert Str to Int
      a?.b;                // warning: is unnecessary

  `dessa check --json` must report exactly those errors/warnings, each on
  the same line, each message containing the given text.

* Run tests -- no inline expectations. `dessa run` must succeed and its
  stdout must equal the sibling `<name>.out` file. Use --update to
  (re)write .out files from the current output (review the diff!).
  A first line `// exit: N` expects exit code N instead of 0.

Usage:
    python tests/test.py [--update] [--js] [filter]

--js runs the run tests through the JavaScript backend (dessa run --target js,
needs Node) and compares with the same .out files; tests that use libraries
with a C++ part are skipped.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tests" / "cases"
EXE = Path(os.environ["DESSA_EXE"]).resolve() if os.environ.get("DESSA_EXE") else ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")

EXPECT_RE = re.compile(r"//\s*(error|warning):\s*(.+?)\s*$")
EXIT_RE = re.compile(r"^//\s*exit:\s*(-?\d+)")
STDERR_RE = re.compile(r"^//\s*stderr:\s*(.+)$", re.MULTILINE)
SAMPLES = ROOT / "sample"
JS = False   # --js: run tests through the JavaScript backend
PACKAGES = ROOT / "tools" / "riv" / "packages"
# Libraries that are riv packages (sql, request, json, phl, ashwee, ...) are
# used straight from the source tree, without `riv install`.
os.environ.setdefault("DESSA_PACKAGES", str(PACKAGES))


def start_http_server():
    """A local HTTP server for tests marked `// needs: http`. Its address is
    passed to the program in the DESSA_TEST_HTTP environment variable.

      /echo          -> JSON describing the request (method, path, body, headers)
      /status/<code> -> responds with that status
      /redirect      -> 302 to /echo
    """
    import threading
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def respond(self):
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length).decode("utf-8") if length else ""
            if self.path.startswith("/status/"):
                code = int(self.path.split("/")[-1])
                payload = f"status {code}".encode()
                self.send_response(code)
            elif self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", "/echo")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            else:
                payload = json.dumps({
                    "method": self.command,
                    "path": self.path,
                    "body": body,
                    "contentType": self.headers.get("Content-Type", ""),
                    "token": self.headers.get("X-Token", ""),
                }).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("X-Server", "dessa-tests")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = respond

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    os.environ["DESSA_TEST_HTTP"] = f"http://127.0.0.1:{server.server_address[1]}"
    return server


def run(args, cwd=ROOT, timeout=120, stdin_file=None):
    stdin = stdin_file.read_text(encoding="utf-8") if stdin_file and stdin_file.exists() else ""
    return subprocess.run(
        [str(EXE)] + args,
        cwd=cwd,
        input=stdin,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


DESSA_EXTENSIONS = {".ds", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss", ".dm"}


def collect():
    for path in sorted(CASES.rglob("*")):
        if path.suffix not in DESSA_EXTENSIONS:
            continue
        rel = path.relative_to(CASES)
        if path.name.startswith("_") or "lib" in rel.parts[:-1]:
            continue
        # .dm module files can't run; they're only used as diagnostic tests
        if path.suffix == ".dm" and not EXPECT_RE.search(path.read_text(encoding="utf-8")):
            continue
        yield path
    # Sample programs with an expected .out file are run as tests too.
    for path in sorted(SAMPLES.glob("*.ds")):
        if path.with_suffix(".out").exists():
            yield path


def diagnostic_test(path, expectations):
    result = run(["check", str(path.relative_to(ROOT)), "--json"])
    try:
        diagnostics = json.loads(result.stdout)
    except json.JSONDecodeError:
        return [f"could not parse `dessa check --json` output:\n{result.stdout}{result.stderr}"]

    target = path.relative_to(ROOT).as_posix()
    problems = []
    remaining = [d for d in diagnostics if d["severity"] in ("error", "warning")]

    for line, severity, text in expectations:
        match = next(
            (d for d in remaining
             if d["line"] == line and d["severity"] == severity and text in d["message"]
             and d["file"].replace("\\", "/") == target),
            None,
        )
        if match is None:
            got = [d["message"] for d in remaining if d["line"] == line]
            problems.append(f"line {line}: expected {severity} containing {text!r}; got {got or 'nothing'}")
        else:
            remaining.remove(match)

    for d in remaining:
        problems.append(f"unexpected {d['severity']} at {d['file']}:{d['line']}:{d['column']}: {d['message']}")
    return problems


def run_test(path, update):
    first = path.read_text(encoding="utf-8").splitlines()[:1]
    expected_exit = 0
    if first and (m := EXIT_RE.match(first[0])):
        expected_exit = int(m.group(1))

    # `// needs: display` tests (ashwee windows): Windows and macOS always have
    # one; on Linux a desktop or xvfb (DISPLAY / WAYLAND_DISPLAY). Not in JS.
    if "// needs: display" in path.read_text(encoding="utf-8"):
        if JS:
            return ["SKIP", "native windows"]
        if sys.platform.startswith("linux") and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            return ["SKIP", "no display"]
    command = ["run", str(path.relative_to(ROOT))] + (["--target", "js"] if JS else [])
    result = run(command, stdin_file=path.with_suffix(".in"))
    if JS and "has a C++ part" in result.stderr:
        return ["SKIP", "library with a C++ part"]
    output = result.stdout.replace("\r\n", "\n")
    problems = []
    # `// needs: mssql` tests are skipped where no SQL Server is reachable
    if "// needs: mssql" in path.read_text(encoding="utf-8") and (
            "Could not connect" in result.stdout + result.stderr or "No SQL Server ODBC driver" in result.stdout + result.stderr):
        return ["SKIP", "no SQL Server here"]
    if result.returncode != expected_exit:
        problems.append(f"exit code {result.returncode} (expected {expected_exit})\n{result.stderr.strip()}")
    for wanted in STDERR_RE.findall(path.read_text(encoding="utf-8")):
        if wanted.strip() not in result.stderr:
            problems.append(f"stderr should contain {wanted.strip()!r}; got:\n{result.stderr.strip()}")

    out_file = path.with_suffix(".out")
    if update:
        out_file.write_text(output, encoding="utf-8", newline="\n")
        return problems
    if not out_file.exists():
        problems.append(f"missing {out_file.name} (run with --update to create it)\n--- output ---\n{output}")
        return problems
    expected = out_file.read_text(encoding="utf-8").replace("\r\n", "\n")
    if output != expected:
        exp_lines, got_lines = expected.splitlines(), output.splitlines()
        for i in range(max(len(exp_lines), len(got_lines))):
            e = exp_lines[i] if i < len(exp_lines) else "<missing>"
            g = got_lines[i] if i < len(got_lines) else "<missing>"
            if e != g:
                problems.append(f"output differs at line {i + 1}:\n  expected: {e}\n  got:      {g}")
                break
    return problems


def declarations_test():
    """Builtin declarations must be valid Dessa and declare every builtin:
    runtime/decl/builtins.ds for print/input, include/<lib>/<lib>.ds for each
    native library. Every library .dm file must check cleanly too."""
    decl_dir = ROOT / "runtime" / "decl"
    include_dir = ROOT / "include"
    problems = []
    files = sorted(decl_dir.glob("*.ds")) + sorted(include_dir.rglob("*.ds")) + sorted(include_dir.rglob("*.dm")) + sorted(PACKAGES.rglob("*.dm"))
    for path in files:
        result = run(["check", str(path.relative_to(ROOT))])
        if result.returncode != 0:
            problems.append(f"{path.relative_to(ROOT).as_posix()} has errors:\n{result.stdout.strip()}")

    result = run(["symbols", "tests/cases/run/hello.ds"])
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        return problems + [f"could not parse `dessa symbols` output:\n{result.stdout}{result.stderr}"]

    wanted = {}  # file -> names
    for lib in data["libraries"]:
        public = [m["name"] for m in lib["members"] if not m["name"].startswith("_")]
        if public:  # _helpers of a library's .dm part need no declarations
            wanted.setdefault(include_dir / lib["name"] / f"{lib['name']}.ds", []).extend(public)
    for g in data["globals"]:
        if g["kind"] == "builtin" and g.get("library") == "builtins":
            wanted.setdefault(decl_dir / "builtins.ds", []).append(g["name"])

    for path, names in wanted.items():
        rel = path.relative_to(ROOT).as_posix()
        if not path.exists():
            problems.append(f"missing declaration file {rel}")
            continue
        text = path.read_text(encoding="utf-8")
        for name in names:
            if not re.search(rf"\b(func\s*\[[^\n]*\]|const\s*\[[^\]]*\])\s*{re.escape(name)}\b", text):
                problems.append(f"{rel} does not declare '{name}'")
    return problems


EDITOR_FIXTURE = ROOT / "tests" / "editor" / "probe.ds"

# (half-typed line, names that must be offered, names that must not)
COMPLETION_CASES = [
    ("    rex.", {"age", "fetch", "name", "speak"}, {"length"}),          # own + inherited members
    ("    print(names.le", {"length", "push", "join"}, {"upper"}),         # Str<> methods
    ("    if (b.", {"add", "sci", "digits"}, {"_of", "_make"}),             # Big from $(<math>), no _helpers
    ("    var[Color] c = Color.", {"RED", "GREEN"}, set()),                 # enum values
    ("    lo", {"local", "rex", "count", "main", "Big", "print"}, set()),   # locals, params, globals, imports
    ("    print(maybe?.", {"name", "fetch"}, set()),                        # Dog | Nil through ?.
    ("    names.forEach((n[Str]) := print(n.", {"upper", "split"}, set()),  # lambda parameter
]

# (needle on a line of the fixture, word in it, expected text in the JSON)
HOVER_CASES = [
    ("print(rex.fetch", "fetch", '"memberOf":"Dog"'),
    ("b.sci()", "sci", '"default":"2"'),
    ("print(maybe.name)", "maybe", '"type":"Dog"'),                         # narrowed by != nil
]


def editor_test():
    """`dessa complete` / `dessa hover`, which the VS Code extension uses."""
    problems = []
    base = EDITOR_FIXTURE.read_text(encoding="utf-8")
    lines = base.split("\n")
    row = lines.index("    //PROBE") + 1
    for text, wanted, unwanted in COMPLETION_CASES:
        source = base.replace("    //PROBE", text)
        result = subprocess.run([str(EXE), "complete", str(EDITOR_FIXTURE), "--stdin", "--at", f"{row}:{len(text) + 1}"],
                                input=source, capture_output=True, text=True, encoding="utf-8", timeout=60)
        try:
            data = json.loads(result.stdout)
        except json.JSONDecodeError:
            problems.append(f"complete {text.strip()!r}: bad output {result.stdout}{result.stderr}")
            continue
        names = {item["name"] for item in data.get("items", [])}
        if missing := wanted - names:
            problems.append(f"complete {text.strip()!r}: missing {sorted(missing)} (got {sorted(names)})")
        if extra := unwanted & names:
            problems.append(f"complete {text.strip()!r}: should not offer {sorted(extra)}")
    for needle, word, expected in HOVER_CASES:
        line = next(i for i, l in enumerate(lines) if needle in l)
        column = lines[line].index(word, lines[line].index(needle)) + 1
        result = run(["hover", str(EDITOR_FIXTURE), "--at", f"{line + 1}:{column}"])
        if expected not in result.stdout:
            problems.append(f"hover {word!r} on line {line + 1}: expected {expected} in {result.stdout.strip()}")
    return problems


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    update = "--update" in sys.argv
    global JS
    JS = "--js" in sys.argv
    pattern = args[0] if args else ""

    if not EXE.exists():
        print(f"compiler not found at {EXE}; build it first:\n"
              "  cmake -S . -B build -G \"MinGW Makefiles\" && cmake --build build")
        return 2

    start_http_server()
    passed = failed = 0
    if not JS and (not pattern or pattern in "declarations"):
        problems = declarations_test()
        if problems:
            failed += 1
            print("FAIL declarations (runtime/decl, include/)")
            for p in problems:
                print("     " + p.replace("\n", "\n     "))
        else:
            passed += 1
            print("ok   declarations (runtime/decl, include/)")
    if not JS and (not pattern or pattern in "editor"):
        problems = editor_test()
        if problems:
            failed += 1
            print("FAIL editor (dessa complete / hover)")
            for p in problems:
                print("     " + p)
        else:
            passed += 1
            print("ok   editor (dessa complete / hover)")

    for path in collect():
        name = (path.relative_to(CASES) if CASES in path.parents else path.relative_to(ROOT)).as_posix()
        if pattern and pattern not in name:
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        expectations = [
            (i + 1, m.group(1), m.group(2))
            for i, line in enumerate(lines)
            if (m := EXPECT_RE.search(line))
        ]
        if JS and expectations:
            continue   # compile errors don't depend on the backend
        try:
            problems = diagnostic_test(path, expectations) if expectations else run_test(path, update)
        except subprocess.TimeoutExpired:
            problems = ["timed out"]

        if problems and problems[0] == "SKIP":
            print(f"skip {name} ({problems[1]})")
            continue
        if problems:
            failed += 1
            print(f"FAIL {name}")
            for p in problems:
                print("     " + p.replace("\n", "\n     "))
        else:
            passed += 1
            print(f"ok   {name}")

    print(f"\n{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
