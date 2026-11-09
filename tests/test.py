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
    python tests/test.py [--update] [--wasm | --c | --cpp] [filter]

--wasm runs the run tests through WebAssembly (dessa run --target wasm: needs
the WebAssembly compiler and Node) and compares with the same .out files;
programs it can't build (desktop windows) are skipped.
"""

import json
import os
import re
import subprocess
import time
import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tests" / "cases"
EXE = Path(os.environ["DESSA_EXE"]).resolve() if os.environ.get("DESSA_EXE") else ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")

EXPECT_RE = re.compile(r"//\s*(error|warning):\s*(.+?)\s*$")
EXIT_RE = re.compile(r"^//\s*exit:\s*(-?\d+)")
STDERR_RE = re.compile(r"^//\s*stderr:\s*(.+)$", re.MULTILINE)
SAMPLES = ROOT / "sample"
WASM = False # --wasm: run tests through WebAssembly
MODE = []    # --c: only through C (programs C can't do yet are skipped); --cpp: only through C++
PACKAGES = ROOT / "tools" / "riv" / "packages"
# Libraries that are riv packages (sql, request, json, ashwee, ...) are
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


DESSA_EXTENSIONS = {".ds", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss", ".dm", ".xds", ".xdz", ".xdx"}


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
    for path in sorted(p for p in SAMPLES.rglob("*") if p.suffix in DESSA_EXTENSIONS):
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
    # one; on Linux a desktop or xvfb (DISPLAY / WAYLAND_DISPLAY). Not in WebAssembly.
    if "// needs: display" in path.read_text(encoding="utf-8"):
        if WASM:
            return ["SKIP", "native windows"]
        if sys.platform.startswith("linux") and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            return ["SKIP", "no display"]
    command = ["run", str(path.relative_to(ROOT))] + (["--target", "wasm"] if WASM else MODE)
    result = run(command, stdin_file=path.with_suffix(".in"))
    if WASM and "can't be built for WebAssembly yet" in result.stderr:
        return ["SKIP", result.stderr.strip().split("(it uses ", 1)[-1].rstrip(")")]
    if "can't be built from C yet" in result.stderr:
        return ["SKIP", result.stderr.strip().split("(it uses ", 1)[-1].rstrip(")")]
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
    ("    if (b.", {"add", "sci", "digits"}, {"_of", "_make"}),             # Big from $("math"), no _helpers
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
    # Unused imports: hints the editor shows faded
    source = '$("math") { sqrt, PI, floor }\n$("time") { now }\n$("os");\nprint(sqrt(PI));\n'
    result = subprocess.run([str(EXE), "check", str(EDITOR_FIXTURE), "--json", "--stdin"],
                            input=source, capture_output=True, text=True, encoding="utf-8", timeout=60)
    try:
        hints = {(d["line"], d["message"]) for d in json.loads(result.stdout) if d.get("tag") == "unused"}
    except json.JSONDecodeError:
        hints = set()
    expected_hints = {(1, "'floor' is imported but never used"), (2, "'now' is imported but never used"), (3, "Unused import")}
    if hints != expected_hints:
        problems.append(f"unused imports: expected {sorted(expected_hints)}, got {sorted(hints)}")
    return problems


def page_import_test():
    """Inline page scripts have webutils without an import; a file of its own needs one."""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        check = lambda file: subprocess.run([str(EXE), "check", str(file)], capture_output=True, text=True, encoding="utf-8", timeout=60)
        page = tmp / "page.dhtml"
        page.write_text("""<!doctype html>
<script>
func[Str] find(a[Str]) { return a; }
var[Element] item = document.createElement("li");
item.textContent = find("mine");
log("no import");
</script>
<script>
$("webutils") { document, Element }
var[Element | Nil] first = document.querySelector("li");
print(first != nil);
</script>
""", encoding="utf-8")
        if (r := check(page)).returncode != 0:
            problems.append("inline scripts: " + r.stdout + r.stderr)
        script = tmp / "app.ds"
        script.write_text('document.title = "x";\n', encoding="utf-8")
        if check(script).returncode == 0:
            problems.append("a .ds file got webutils without importing it")
    return problems


def verify_test():
    """dessa verify-download: what riv update relies on before running an installer."""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        good = tmp / "release.bin"
        # Longer than one SHA-256 block, with line endings and zero bytes kept as they are
        good.write_bytes(bytes(range(256)) * 300 + b"\r\n\0")
        import hashlib
        real = hashlib.sha256(good.read_bytes()).hexdigest()
        sums = tmp / "SHA256SUMS"
        check = lambda file: subprocess.run([str(EXE), "verify-download", str(file), str(sums)], capture_output=True, text=True, timeout=60)
        sums.write_text(f"{'0' * 64}  other.bin\n{real}  release.bin\n", encoding="utf-8")
        if (r := check(good)).returncode != 0:
            problems.append("the right file was refused: " + r.stderr)
        sums.write_text(f"{'0' * 64}  release.bin\n", encoding="utf-8")
        if (r := check(good)).returncode == 0 or "not the file that was released" not in r.stderr:
            problems.append("a changed file was accepted: " + r.stderr)
        sums.write_text(f"{real}  other.bin\n", encoding="utf-8")
        if (r := check(good)).returncode == 0 or "not in the list" not in r.stderr:
            problems.append("a file that isn't listed was accepted: " + r.stderr)
    return problems


def limits_test():
    """Code that is too deep is an error message from `dessa check`, on every
    platform's stack, and code just under each limit still compiles."""
    fits = [
        ("parentheses", "var[Int] x = " + "(" * 190 + "1" + ")" * 190 + ";\n"),
        ("negations", "var[Bul] x = " + "!" * 190 + "true;\n"),
        ("operators", "var[Int] x = 1" + " + 1" * 990 + ";\n"),
        ("calls", "class A { func[A] f() { return this; } }\nvar[A] a = new A();\na" + ".f()" * 450 + ";\n"),
        ("else ifs", "var[Int] n = 1;\nif (n == 0) { }\n" + "else if (n == 0) { }\n" * 900),
        ("blocks", "{\n" * 150 + "}\n" * 150),
        ("templates", "print(" + '$"{' * 50 + "1" + '}"' * 50 + ");\n"),
        ("generic calls", "func[T] id:T(x[T]) { return x; }\nvar[Int] y = " + "id(" * 100 + "1" + ")" * 100 + ";\n"),
        # Found by tests/fuzz.py: the receiver of a generic method was analysed again at every link
        ("generic method calls", "class A { func[A] to:T() { return this; } }\nvar[A] a = new A();\na" + ".to:Int()" * 100 + ";\n"),
        ("inferred generic method calls", "class A { func[A] put:T(x[T]) { return this; } }\nvar[A] a = new A();\na" + ".put(1)" * 100 + ";\n"),
        ("list types", "var[Int" + "<>" * 30 + "] x = <>;\n"),
    ]
    deep = [
        ("parentheses", "var[Int] x = " + "(" * 200000 + "1" + ")" * 200000 + ";\n"),
        ("negations", "var[Bul] x = " + "!" * 200000 + "true;\n"),
        ("operators", "var[Int] x = 1" + " + 1" * 200000 + ";\n"),
        ("calls", "var[Str] a = \"\";\na" + ".trim()" * 200000 + ";\n"),
        ("pipes", "var[Int] a = 1" + " |> f" * 200000 + ";\n"),
        ("else ifs", "var[Int] n = 1;\nif (n == 0) { }\n" + "else if (n == 0) { }\n" * 200000),
        ("blocks", "{\n" * 200000 + "}\n" * 200000),
        ("templates", "print(" + '$"{' * 5000 + "1" + '}"' * 5000 + ");\n"),
        ("list types", "var[Int" + "<>" * 200000 + "] x = <>;\n"),
        ("union types", "var[Int" + " | Int" * 200000 + "] x = 1;\n"),
        ("grouped types", "var[" + "(" * 200000 + "Int" + ")" * 200000 + "] x = 1;\n"),
        # Found by tests/fuzz.py: `<` tried as an index and undone at every level
        ("unclosed indexes", "class V { init(a[Int], b[Int]) { } }\nvar[Int] s = 0;\nprint(" + "s<new V(0," * 60 + ");\n"),
        ("a long error", "print(" + '"' + "x" * 200000 + '" "' + "x" * 200000 + '"' + ");\n"),
    ]
    # Found by tests/fuzz.py too: what isn't valid text is an error, wherever it is
    broken = [("in a string", b'print("a\xffb");\n'), ("in an import", b'$("ma\xfeth");\n'), ("in a comment", b'// \xc0\x80\nprint(1);\n'),
              ("cut short", b'print("\xe2\x82'), ("a surrogate", b'print("\xed\xa0\x80");\n'), ("too high", b'print("\xf4\x90\x80\x80");\n')]
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        file = Path(tmp) / "limits.ds"
        for name, source in fits + deep:
            file.write_text(source, encoding="utf-8")
            try:
                # Far more than any of these needs: work that doubles with each level never ends
                result = subprocess.run([str(EXE), "check", str(file)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
            except subprocess.TimeoutExpired:
                problems.append(f"{name}: timed out")
                continue
            text = (result.stdout + result.stderr).strip()
            if (name, source) in fits:
                if result.returncode != 0:
                    problems.append(f"{name} under the limit: exit {result.returncode}: {text[:200]}")
            elif result.returncode != 1 or "error" not in text:
                problems.append(f"{name} too deep: exit {result.returncode} (a crash is not a message): {text[:200]}")
        for name, source in broken:
            file.write_bytes(source)
            result = subprocess.run([str(EXE), "check", str(file)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
            if result.returncode != 1 or "UTF-8" not in result.stdout + result.stderr:
                problems.append(f"invalid text {name}: exit {result.returncode}: {(result.stdout + result.stderr)[:200]}")
        file.write_bytes('print("é€😀");\n'.encode("utf-8"))
        result = subprocess.run([str(EXE), "check", str(file)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
        if result.returncode != 0:
            problems.append(f"valid text refused: {result.stdout}{result.stderr}")
    return problems


def execute_test():
    """os.execute: arguments reach the program exactly as given, and nothing in
    them is ever treated as a command."""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "echo.ds").write_text("""
$("sys") { args, exit }
for (var[Str] a in args()) print($"[{a}]");
exit(3);
""", encoding="utf-8")
        (tmp / "main.ds").write_text("""
$("os");
$("sys") { args };
var[ProcessResult] r = execute(args()<0>, <"plain", "two words", "quote\\"inside", "back\\\\slash", "trail\\\\", "\\\\\\"", "$(echo x > marker)", "`echo x > marker`", "& echo x> marker", "; echo x > marker", "| echo x> marker", "%PATH%", "">);
print(r.code, r.ok());
print(r.output);
try {
    execute("this-program-does-not-exist-anywhere");
} catch (e[Str]) {
    print(e);
}
try { execute("something.BAT"); } catch (e[Str]) { print(e.substr(0, 34)); }
""", encoding="utf-8")
        for name in ("echo", "main"):
            r = subprocess.run([str(EXE), "build", f"{name}.ds", "-o", f"{name}.exe"], cwd=tmp, capture_output=True, text=True, timeout=240)
            if r.returncode != 0:
                return [f"could not build {name}: " + r.stdout + r.stderr]
        r = subprocess.run([str(tmp / "main.exe"), str(tmp / "echo.exe")], cwd=tmp, capture_output=True, text=True, encoding="utf-8", timeout=60)
        wanted = ["plain", "two words", 'quote"inside', "back\\slash", "trail\\", '\\"', "$(echo x > marker)", "`echo x > marker`",
                  "& echo x> marker", "; echo x > marker", "| echo x> marker", "%PATH%", ""]
        lines = r.stdout.replace("\r\n", "\n").split("\n")
        if lines[0] != "3 false":
            problems.append(f"exit code: {lines[0]!r}")
        got = [l[1:-1] for l in lines[1:] if l.startswith("[")]
        if got != wanted:
            problems.append(f"arguments changed: {got}")
        if (tmp / "marker").exists():
            problems.append("an argument ran as a command")
        if "could not start this-program-does-not-exist-anywhere" not in r.stdout:
            problems.append("a missing program: " + r.stdout + r.stderr)
        if os.name == "nt" and "execute can't run something.BAT" not in r.stdout:
            problems.append("a batch file wasn't refused: " + r.stdout)
    return problems


def raw_request(port, data, timeout=15):
    """Sends bytes exactly as given (no client library tidies the path) and returns the answer."""
    import socket
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        s.sendall(data)
        out = b""
        try:
            while chunk := s.recv(65536):
                out += chunk
        except OSError:
            pass
    return out


def http_server_test():
    """The http package over a real socket: what a hostile or slow client can do."""
    import socket
    program = """
$("http") { Server, Request, Response, text, redirect }
var[Server] server = new Server(0);
server.quiet = true;
server.timeoutSeconds = 2;
server.maxBytes = 1000;
server.files("/files", "public");
server.get("/", (req[Request]) := text("home"));
server.get("/boom", (req[Request]) := text(Str(<1, 2><5>)));
server.get("/go", (req[Request]) := redirect(req.query<"to">));
server.get("/cookie", (req[Request]) := text(req.cookies().getOr("a", "none")).cookie("b", "2", 60));
server.use((req[Request]) := req.path == "/blocked" ? text("no", 403) : nil);
server.listen();
"""
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "public").mkdir()
        (tmp / "public" / "a.txt").write_text("file a", encoding="utf-8")
        (tmp / "public" / ".env").write_text("SECRET", encoding="utf-8")
        (tmp / "public" / "bin.dat").write_bytes(b"\xff\xfe\x00\x80ok")
        (tmp / "secret.txt").write_text("SECRET", encoding="utf-8")
        # A free port: the server's own choice isn't visible from here
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        (tmp / "server.ds").write_text(program.replace("new Server(0)", f"new Server({port})"), encoding="utf-8")
        build = subprocess.run([str(EXE), "build", "server.ds", "-o", "server.exe"], cwd=tmp, capture_output=True, text=True, timeout=240)
        if build.returncode != 0:
            return ["could not build the server: " + build.stdout + build.stderr]
        server = subprocess.Popen([str(tmp / "server.exe")], cwd=tmp, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    socket.create_connection(("127.0.0.1", port), timeout=1).close()
                    break
                except OSError:
                    time.sleep(0.1)
            head = lambda path: f"GET {path} HTTP/1.1\r\nHost: x\r\n\r\n".encode()
            def expect(what, data, wanted, unwanted=None):
                answer = raw_request(port, data)
                if wanted.encode() not in answer or (unwanted and unwanted.encode() in answer):
                    problems.append(f"{what}: got {answer[:200]!r}")
            expect("normal", head("/"), "home")
            # Nothing a handler says about its failure reaches the client
            expect("error text", head("/boom"), "500 Internal", "IndexError")
            # One client that sends nothing, and one that sends half, don't hold up the rest
            idle = socket.create_connection(("127.0.0.1", port))
            half = socket.create_connection(("127.0.0.1", port))
            half.sendall(b"GET / HTTP/1.1\r\nHo")
            time.sleep(0.2)
            started = time.time()
            expect("beside idle clients", head("/"), "home")
            if time.time() - started > 1:
                problems.append("idle clients held up another one")
            # ... and they run out of time
            half.settimeout(6)
            if b"408" not in half.recv(1000):
                problems.append("a slow client wasn't told it ran out of time")
            half.close()
            idle.close()
            # Limits, and what is refused
            expect("too long", b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1001\r\n\r\n", "413")
            expect("much too long", b"POST / HTTP/1.1\r\nContent-Length: 99999999999999999999999\r\n\r\n", "413")
            expect("bad length", b"POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n", "400")
            expect("two lengths", b"POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab", "400")
            expect("chunked", b"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", "501")
            expect("huge headers", b"GET / HTTP/1.1\r\nX: " + b"a" * 70000, "431")
            # A value can't start a header of its own
            expect("header injection", head("/go?to=/x%0d%0aX-Evil:%201"), "Location: /xX-Evil: 1")
            answer = raw_request(port, head("/go?to=/x%0d%0aX-Evil:%201"))
            if b"\r\nX-Evil" in answer:
                problems.append(f"a header was injected: {answer[:200]!r}")
            # Files: inside the folder only
            expect("a file", head("/files/a.txt"), "file a")
            for path in ["/files/../secret.txt", "/files/%2e%2e/secret.txt", "/files/..%2fsecret.txt", "/files/.env", "/files/..%5csecret.txt"]:
                expect("leaving the folder: " + path, head(path), "404", "SECRET")
            answer = raw_request(port, head("/files/bin.dat"))
            if b"Content-Length: 6\r\n" not in answer or not answer.endswith(b"\xff\xfe\x00\x80ok"):
                problems.append(f"a file that isn't text: {answer!r}")
            # Cookies and steps before the routes
            expect("cookies", b"GET /cookie HTTP/1.1\r\nCookie: z=1; a=hello\r\n\r\n", "Set-Cookie: b=2; Path=/; SameSite=Lax; Max-Age=60; HttpOnly")
            expect("cookie read", b"GET /cookie HTTP/1.1\r\nCookie: z=1; a=hello\r\n\r\n", "hello")
            expect("use", head("/blocked"), "403")
        finally:
            server.kill()
            server.wait()
    return problems


def serve_test():
    """dessa serve hands out only what is inside its folder."""
    import socket
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "site").mkdir()
        (tmp / "site-secret").mkdir()
        (tmp / "site" / "a.txt").write_text("public", encoding="utf-8")
        (tmp / "site-secret" / "s.txt").write_text("SECRET", encoding="utf-8")
        (tmp / "outside.txt").write_text("SECRET", encoding="utf-8")
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        server = subprocess.Popen([str(EXE), "serve", "site", "--port", str(port)], cwd=tmp, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    socket.create_connection(("127.0.0.1", port), timeout=1).close()
                    break
                except OSError:
                    time.sleep(0.1)
            get = lambda path, host="localhost": raw_request(port, f"GET {path} HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
            if b"public" not in get("/a.txt"):
                problems.append("a file inside the folder wasn't served")
            for path in ["/../site-secret/s.txt", "/%2e%2e/site-secret/s.txt", "/../outside.txt", "/..%2foutside.txt"]:
                if b"SECRET" in get(path):
                    problems.append("served from outside the folder: " + path)
            if b"public" in get("/a.txt", "evil.example"):
                problems.append("answered a request for another site's name")
            if b"public" not in get("/a.txt", f"127.0.0.1:{port}"):
                problems.append("refused 127.0.0.1")
        finally:
            server.kill()
            server.wait()
    return problems


def riv_test():
    """riv end to end: bundled packages, the index check, and a package that
    travels by torrent from one riv home to another."""
    riv = EXE.parent / ("riv.exe" if os.name == "nt" else "riv")
    if not riv.exists():
        return None   # riv isn't built
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        index = ROOT / "tools" / "riv" / "index"

        def riv_run(args, cwd, env_extra, timeout=120):
            env = dict(os.environ, RIV_TRACKERS="0", RIV_LAN="0", RIV_WEBSEED="0", RIV_TIMEOUT="20", DESSA=str(EXE))
            env.update(env_extra)
            return subprocess.run([str(riv)] + args, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8", timeout=timeout)

        # The official index matches the bundled packages
        result = riv_run(["index", str(PACKAGES), "--out", str(index), "--check"], ROOT, {"RIV_HOME": str(tmp / "h0")})
        if result.returncode != 0:
            problems.append("tools/riv/index is out of date:\n" + result.stdout.strip())

        # A bundled package installs from disk
        app = tmp / "app"
        app.mkdir()
        home1 = {"RIV_HOME": str(tmp / "h1"), "RIV_INDEX": str(index), "DESSA_PACKAGES": str(PACKAGES)}
        riv_run(["init", "app"], app, home1)
        result = riv_run(["add", "colors"], app, home1)
        if not re.search(r"colors@[0-9.]+: from the packages that came with Dessa", result.stdout) or not (app / "riv_modules" / "textfx" / "riv.pkg").exists():
            problems.append("riv add colors (bundled): " + result.stdout + result.stderr)
        # Old or changed files with the same version number are noticed and replaced
        lib = app / "riv_modules" / "colors" / "lib.ds"
        original = lib.read_text(encoding="utf-8")
        lib.write_text(original + "\n// changed\n", encoding="utf-8")
        result = riv_run(["install"], app, home1)
        if "installing it again" not in result.stdout or lib.read_text(encoding="utf-8") != original:
            problems.append("riv install with changed files: " + result.stdout + result.stderr)
        # riv add writes a range (^), riv install a riv.lock with versions and sha256
        manifest = (app / "riv.pkg").read_text(encoding="utf-8")
        lock_file = app / "riv.lock"
        lock = lock_file.read_text(encoding="utf-8") if lock_file.exists() else ""
        if not re.search(r"colors = \^[0-9.]+", manifest) or not re.search(r"colors = [0-9.]+ sha256:[0-9a-f]{64}", lock) or "textfx = " not in lock:
            problems.append("riv.pkg range / riv.lock:\n" + manifest + "\n" + lock)
        # A package whose files aren't the ones riv.lock remembers is refused; riv upgrade takes it again
        lock_file.write_text(re.sub(r"(colors = [0-9.]+ sha256:)[0-9a-f]{64}", r"\g<1>" + "0" * 64, lock), encoding="utf-8")
        result = riv_run(["install"], app, home1)
        if result.returncode == 0 or "doesn't have the files riv.lock remembers" not in result.stdout:
            problems.append("riv install with a changed package: " + result.stdout + result.stderr)
        result = riv_run(["upgrade"], app, home1)
        if result.returncode != 0 or "0" * 64 in lock_file.read_text(encoding="utf-8"):
            problems.append("riv upgrade: " + result.stdout + result.stderr)
        result = riv_run(["add", "colors@>=99.0"], app, home1)
        if result.returncode == 0 or "No version of colors fits '>=99.0'" not in result.stdout:
            problems.append("riv add with a range nothing fits: " + result.stdout + result.stderr)

        # A new package: published in one home, shared, installed in another
        greet = tmp / "greet"
        greet.mkdir()
        (greet / "riv.pkg").write_text("name = greet\nversion = 1.0.0\nentry = lib.ds\ndescription = Says hello\n", encoding="utf-8")
        (greet / "lib.ds").write_text('func[Str] hello(name[Str]) { return $"Hello, {name}!"; }\n', encoding="utf-8")
        mine = {"RIV_HOME": str(tmp / "h2"), "RIV_INDEX": str(tmp / "h2" / "index")}
        result = riv_run(["publish", str(greet)], tmp, mine)
        if "Published greet@1.0.0" not in result.stdout:
            problems.append("riv publish: " + result.stdout + result.stderr)
        env = dict(os.environ, RIV_TRACKERS="0", RIV_LAN="0", RIV_WEBSEED="0", **mine)
        seeder = subprocess.Popen([str(riv), "seed", "--port", "7979", "--minutes", "2"], cwd=tmp, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8")
        try:
            time.sleep(2)
            other = tmp / "other"
            other.mkdir()
            theirs = {"RIV_HOME": str(tmp / "h3"), "RIV_INDEX": str(tmp / "h2" / "index"), "RIV_PEERS": "127.0.0.1:7979"}
            riv_run(["init", "other"], other, theirs)
            result = riv_run(["add", "greet"], other, theirs)
            if "greet@1.0.0: 1 piece(s): 1 from 1 peer(s)" not in result.stdout:
                problems.append("riv add greet (by torrent): " + result.stdout + result.stderr)
            (other / "main.ds").write_text('$("greet") { hello }\nprint(hello("riv"));\n', encoding="utf-8")
            result = subprocess.run([str(EXE), "run", "main.ds"], cwd=other, capture_output=True, text=True, encoding="utf-8", timeout=120)
            if result.stdout.strip() != "Hello, riv!":
                problems.append("running the downloaded package: " + result.stdout + result.stderr)
        finally:
            seeder.kill()
            seeder.wait()
    return problems


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    update = "--update" in sys.argv
    global WASM, MODE
    WASM = "--wasm" in sys.argv
    MODE = ["--target", "c"] if "--c" in sys.argv else ["--cpp"] if "--cpp" in sys.argv else []
    pattern = args[0] if args else ""

    if not EXE.exists():
        print(f"compiler not found at {EXE}; build it first:\n"
              "  cmake -S . -B build -G \"MinGW Makefiles\" && cmake --build build")
        return 2

    start_http_server()
    passed = failed = 0
    if not WASM and (not pattern or pattern in "declarations"):
        problems = declarations_test()
        if problems:
            failed += 1
            print("FAIL declarations (runtime/decl, include/)")
            for p in problems:
                print("     " + p.replace("\n", "\n     "))
        else:
            passed += 1
            print("ok   declarations (runtime/decl, include/)")
    if not WASM and (not pattern or pattern in "editor"):
        problems = editor_test()
        if problems:
            failed += 1
            print("FAIL editor (dessa complete / hover)")
            for p in problems:
                print("     " + p)
        else:
            passed += 1
            print("ok   editor (dessa complete / hover)")
    if not WASM and (not pattern or pattern in "limits"):
        problems = limits_test()
        if problems:
            failed += 1
            print("FAIL limits (code that is too deep)")
            for p in problems:
                print("     " + p)
        else:
            passed += 1
            print("ok   limits (code that is too deep)")
    for name, label, check in (("http_server", "http server (hostile and slow clients)", http_server_test),
                               ("serve", "dessa serve (stays inside its folder)", serve_test),
                               ("execute", "os.execute (arguments stay arguments)", execute_test)):
        if WASM or (pattern and pattern not in name):
            continue
        problems = check()
        if problems:
            failed += 1
            print(f"FAIL {label}")
            for p in problems:
                print("     " + p)
        else:
            passed += 1
            print(f"ok   {label}")
    if not WASM and (not pattern or pattern in "verify"):
        problems = verify_test()
        if problems:
            failed += 1
            print("FAIL verify (checking downloads)")
            for p in problems:
                print("     " + p)
        else:
            passed += 1
            print("ok   verify (checking downloads)")
    if not WASM and (not pattern or pattern in "page imports"):
        problems = page_import_test()
        if problems:
            failed += 1
            print("FAIL page imports")
            for p in problems:
                print("     " + p.replace("\n", "\n     "))
        else:
            passed += 1
            print("ok   page imports")
    if not WASM and (not pattern or pattern in "riv"):
        problems = riv_test()
        if problems is None:
            print("skip riv (build it: cmake --build build --target riv)")
        elif problems:
            failed += 1
            print("FAIL riv (index, bundled packages, sharing by torrent)")
            for p in problems:
                print("     " + p.replace("\n", "\n     "))
        else:
            passed += 1
            print("ok   riv (index, bundled packages, sharing by torrent)")

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
        if WASM and expectations:
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
