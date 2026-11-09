#!/usr/bin/env python3
"""Components in a real browser: builds tests/web/xds/page.html (an ordinary
page using <script src="app.xds"> and <AppContainer />), opens it in headless
Chrome, and compares what its own script wrote after clicking things.

    python tests/web/chrome_xds.py          (needs build/dessa and Chrome; skipped without Chrome)
    python tests/web/chrome_xds.py --update  rewrites page.out (review the diff!)
"""
import functools
import html
import http.server
import re
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chrome_utils import EXE, Quiet, ROOT, find_chrome

PAGE = ROOT / "tests" / "web" / "xds" / "page.html"
EXPECTED = ROOT / "tests" / "web" / "xds" / "page.out"


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    chrome = find_chrome()
    if not chrome:
        print("skip components in Chrome (no Chrome here)")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / "data.txt").write_text("from the server\n", encoding="utf-8")
        built = subprocess.run([str(EXE), "build", str(PAGE), "-o", str(tmp / "index.html")], capture_output=True, text=True, timeout=300)
        if built.returncode != 0:
            print("FAIL could not build the page:\n" + built.stdout + built.stderr)
            return 1
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(tmp)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            result = subprocess.run([chrome, "--headless=new", "--disable-gpu", "--no-sandbox", f"--user-data-dir={tmp / 'profile'}",
                                     "--virtual-time-budget=20000", "--dump-dom", f"http://127.0.0.1:{server.server_address[1]}/index.html"],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        finally:
            server.shutdown()
        found = re.search(r'<pre id="out">(.*?)</pre>', result.stdout, re.S)
        got = html.unescape(found.group(1)) if found else ""
    if "--update" in sys.argv:
        EXPECTED.write_text(got, encoding="utf-8", newline="\n")
        print(got)
        return 0
    wanted = EXPECTED.read_text(encoding="utf-8").replace("\r\n", "\n")
    if got != wanted:
        print("FAIL components in Chrome")
        for line in sorted(set(wanted.splitlines()) ^ set(got.splitlines())):
            print("    ", "expected" if line in wanted.splitlines() else "got     ", line)
        if not got:
            print(result.stdout[:2000], result.stderr[:1000])
        return 1
    print("ok   components in Chrome")
    return 0


if __name__ == "__main__":
    sys.exit(main())
