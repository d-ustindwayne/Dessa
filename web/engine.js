// web/engine.js -- runs Dessa in a web page, like Babel's standalone build.
// Built into dessa.js (after the WebAssembly compiler). A page only needs:
//
//     <script src="dessa.js"></script>
//     <script type="text/dessa" src="app.ds"></script>
//     <script type="text/dessa">
//         { document } from $("webutils");
//         document.body.textContent = "Hello from Dessa!";
//     </script>
//
// Each script is fetched (with the files it imports through $("...")),
// compiled to JavaScript right in the browser and run, in page order.
// print() goes to the browser console, and also into an element with
// id="dessa-output" if the page has one.
(function () {
    "use strict";
    const isBrowser = typeof window !== "undefined" && typeof document !== "undefined";
    const EXTENSIONS = [".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"];
    let compilerPromise = null;
    const compiler = () => (compilerPromise ??= DessaCompiler());

    function outputElement() {
        return isBrowser ? document.getElementById("dessa-output") : null;
    }
    function show(text, isError) {
        const out = outputElement();
        if (!out) return;
        const line = document.createElement("div");
        line.textContent = text;
        if (isError) line.style.color = "#c62828";
        out.appendChild(line);
    }
    const hooks = {
        onLine: (line) => { console.log(line); show(line, false); },
        onErrorLine: (line) => { console.error(line); show(line, true); },
    };

    // The $("a.b") user modules a source imports (libraries $("x") are built in)
    function importsOf(source) {
        const found = [];
        const pattern = /\$\(([^()]*)\)/g;
        let m;
        while ((m = pattern.exec(source))) {
            for (const part of m[1].split(",")) {
                const s = /^\s*"([^"]+)"\s*$/.exec(part);
                if (s) found.push(s[1]);
            }
        }
        return found;
    }

    async function fetchText(url) {
        const response = await fetch(url);
        if (!response.ok) return null;
        return await response.text();
    }

    // Writes `source` (from `url`) to /app/<path> and fetches what it imports,
    // trying every Dessa extension next to it.
    async function collect(module, source, path, url, seen) {
        writeFile(module, "/app/" + path, source);
        const folder = path.includes("/") ? path.slice(0, path.lastIndexOf("/") + 1) : "";
        for (const spec of importsOf(source)) {
            const relative = folder + spec.replace(/\./g, "/");
            if (seen.has(relative)) continue;
            seen.add(relative);
            for (const ext of EXTENSIONS) {
                const fileUrl = new URL(spec.replace(/\./g, "/") + ext, url).href;
                const text = await fetchText(fileUrl).catch(() => null);
                if (text !== null) {
                    await collect(module, text, relative + ext, fileUrl, seen);
                    break;
                }
            }
        }
    }

    function writeFile(module, path, text) {
        const parts = path.split("/").filter(Boolean);
        let dir = "";
        for (const part of parts.slice(0, -1)) {
            dir += "/" + part;
            try { module.FS.mkdir(dir); } catch (e) { /* exists */ }
        }
        module.FS.writeFile(path, text);
    }

    function resetApp(module) {
        const remove = (dir) => {
            for (const name of module.FS.readdir(dir)) {
                if (name === "." || name === "..") continue;
                const full = dir + "/" + name;
                if (module.FS.isDir(module.FS.stat(full).mode)) { remove(full); module.FS.rmdir(full); }
                else module.FS.unlink(full);
            }
        };
        try { remove("/app"); } catch (e) { try { module.FS.mkdir("/app"); } catch (e2) { /* ok */ } }
    }

    // Compiles Dessa source to JavaScript. `url` is where relative imports are fetched from.
    async function compile(source, options = {}) {
        const module = await compiler();
        const name = options.name || "main.ds";
        const baseUrl = options.url || (isBrowser ? document.baseURI : "file:///");
        resetApp(module);
        if (options.files) {
            for (const [path, text] of Object.entries(options.files)) writeFile(module, "/app/" + path, text);
            writeFile(module, "/app/" + name, source);
        } else {
            await collect(module, source, name, baseUrl, new Set());
        }
        const json = module.ccall("dessa_compile", "string", ["string"], ["/app/" + name]);
        const result = JSON.parse(json);
        result.diagnostics.forEach((d) => { d.file = d.file.replace(/^\/?app\//, ""); });
        return result;
    }

    function formatDiagnostic(d) {
        return (d.file ? d.file + (d.line ? `:${d.line}:${d.column}` : "") + ": " : "") + `${d.severity}: ${d.message}`;
    }

    // Compiles and runs. Returns the exit code (0 = fine).
    async function run(source, options = {}) {
        const result = await compile(source, options);
        for (const d of result.diagnostics) {
            if (d.severity === "error") hooks.onErrorLine(formatDiagnostic(d));
            else console.warn(formatDiagnostic(d));
        }
        if (!result.ok) return 1;
        return execute(result.js);
    }

    function execute(js) {
        const g = typeof globalThis !== "undefined" ? globalThis : window;
        const saved = g.$dessaHooks;
        g.$dessaHooks = Object.assign({}, hooks, g.DessaHooks || {});
        try {
            new Function(js)();
            return 0;
        } finally {
            g.$dessaHooks = saved;
        }
    }

    async function runPageScripts() {
        const scripts = Array.from(document.querySelectorAll('script[type="text/dessa"]'));
        let inline = 0;
        for (const script of scripts) {
            try {
                if (script.src) {
                    const source = await fetchText(script.src);
                    if (source === null) { hooks.onErrorLine(`Dessa: could not load ${script.src}`); continue; }
                    const name = new URL(script.src).pathname.split("/").pop() || "main.ds";
                    await run(source, { name, url: script.src });
                } else {
                    inline++;
                    await run(script.textContent, { name: `inline${inline}.ds`, url: document.baseURI });
                }
            } catch (e) {
                hooks.onErrorLine(`Dessa: ${e && e.message ? e.message : e}`);
            }
        }
        document.dispatchEvent(new Event("dessa:done"));
    }

    const api = { compile, run, execute, version: () => compiler().then((m) => m.ccall("dessa_version", "string", [], [])) };
    if (typeof globalThis !== "undefined") globalThis.Dessa = api;
    if (typeof module !== "undefined" && module.exports) {
        module.exports = DessaCompiler;
        module.exports.Dessa = api;
    }
    if (isBrowser) {
        if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", runPageScripts);
        else runPageScripts();
    }
})();
