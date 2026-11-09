#!/usr/bin/env python3
"""webutils' utilities in a real browser: builds tests/web/wasm/utils.dhtml,
serves it, opens it in headless Chrome and compares what it wrote into the page.

    python tests/web/chrome_utils.py          (needs build/dessa and Chrome; skipped without Chrome)
    python tests/web/chrome_utils.py --update  rewrites utils.out (review the diff!)
"""
import functools
import html
import http.server
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / ("dessa.exe" if os.name == "nt" else "dessa")
PAGE = ROOT / "tests" / "web" / "wasm" / "utils.dhtml"
EXPECTED = ROOT / "tests" / "web" / "wasm" / "utils.out"


def find_chrome():
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"):
        if found := shutil.which(name):
            return found
    candidates = [
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        os.path.expandvars(r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"),
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ]
    return next((c for c in candidates if os.path.exists(c)), None)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    chrome = find_chrome()
    if not chrome:
        print("skip webutils in Chrome (no Chrome here)")
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
        url = f"http://127.0.0.1:{server.server_address[1]}/index.html"
        profile = tmp / "profile"
        try:
            result = subprocess.run([chrome, "--headless=new", "--disable-gpu", "--no-sandbox", f"--user-data-dir={profile}",
                                     "--virtual-time-budget=20000", "--dump-dom", url],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        finally:
            server.shutdown()
        found = re.search(r'<pre id="out">(.*?)</pre>', result.stdout, re.S)
        got = html.unescape(found.group(1)) if found else ""
    if "<title>HACKED</title>" in result.stdout:
        print("FAIL text from a user ran as a script")
        return 1
    if "--update" in sys.argv:
        EXPECTED.write_text(got, encoding="utf-8", newline="\n")
        print(got)
        return 0
    wanted = EXPECTED.read_text(encoding="utf-8").replace("\r\n", "\n")
    # Things that arrive later come in any order
    if sorted(got.splitlines()) != sorted(wanted.splitlines()):
        print("FAIL webutils in Chrome")
        for line in sorted(set(wanted.splitlines()) ^ set(got.splitlines())):
            print("    ", "expected" if line in wanted.splitlines() else "got     ", line)
        if not got:
            print(result.stdout[:2000], result.stderr[:1000])
        return 1
    print("ok   webutils in Chrome")
    return 0


if __name__ == "__main__":
    sys.exit(main())
