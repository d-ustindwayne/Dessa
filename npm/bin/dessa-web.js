#!/usr/bin/env node
// dessa-web -- Dessa for JavaScript, from the command line (no C++ compiler needed).
//
//   dessa-web run app.ds                 run with Node
//   dessa-web build app.ds [-o app.js]   one self-contained .js file
//   dessa-web build page.dhtml           page.html with its Dessa scripts compiled
//   dessa-web check app.ds|page.dhtml    errors only
//   dessa-web serve [folder] [--port N]  a web server; .dhtml pages compiled on each request
"use strict";
const fs = require("fs");
const path = require("path");
const http = require("http");
const dessa = require("..");

const args = process.argv.slice(2);
const command = args[0];
const option = (name) => {
    const i = args.indexOf(name);
    return i >= 0 ? args[i + 1] : undefined;
};
const positional = args.slice(1).filter((a, i, all) => !a.startsWith("--") && a !== "-o" && all[i - 1] !== "-o" && all[i - 1] !== "--port");

function usage() {
    console.log(`dessa-web: Dessa for JavaScript

  dessa-web run <file.ds> [args]        run with Node
  dessa-web build <file.ds> [-o out.js] compile to one .js file (runtime included)
  dessa-web build <page.dhtml>          compile a page's Dessa scripts: page.html
  dessa-web check <file.ds|page.dhtml>  show errors
  dessa-web serve [folder] [--port N]   serve a folder; .dhtml pages are compiled on each request
  dessa-web version`);
}

function fail(error) {
    if (error instanceof dessa.DessaError) console.error(error.message);
    else console.error("dessa-web: " + (error && error.message ? error.message : error));
    process.exit(1);
}

async function main() {
    switch (command) {
        case "run": {
            if (!positional[0]) return usage();
            const code = await dessa.runFile(positional[0]);
            process.exitCode = code;
            return;
        }
        case "build": {
            const file = positional[0];
            if (!file) return usage();
            if (dessa.isPage(file)) {
                const html = await dessa.buildPage(file);
                const out = option("-o") || (path.extname(file).toLowerCase() === ".dhtml"
                    ? file.replace(/\.dhtml$/i, ".html") : file.replace(/\.html?$/i, ".built.html"));
                fs.writeFileSync(out, html);
                console.log("Wrote " + out);
            } else {
                const js = await dessa.compileFile(file);
                const out = option("-o") || file.replace(/\.[^.\\/]+$/, "") + ".js";
                fs.writeFileSync(out, js);
                console.log("Wrote " + out);
            }
            return;
        }
        case "check": {
            if (!positional[0]) return usage();
            const diagnostics = await dessa.check(positional[0]);
            for (const d of diagnostics) console.log(dessa.formatDiagnostic(d));
            if (diagnostics.some((d) => d.severity === "error")) process.exitCode = 1;
            else console.log("No errors.");
            return;
        }
        case "serve":
            return serve(positional[0] || ".", Number(option("--port")) || 8080);
        case "version":
            console.log("dessa " + (await dessa.version()));
            return;
        default:
            usage();
            if (command) process.exitCode = 1;
    }
}

const TYPES = {
    ".html": "text/html; charset=utf-8", ".htm": "text/html; charset=utf-8", ".dhtml": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png", ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg", ".gif": "image/gif", ".ico": "image/x-icon", ".wasm": "application/wasm",
    ".ds": "text/plain; charset=utf-8", ".dm": "text/plain; charset=utf-8", ".txt": "text/plain; charset=utf-8",
};

function serve(folder, port) {
    const root = path.resolve(folder);
    const server = http.createServer(async (req, res) => {
        try {
            let file = path.join(root, decodeURIComponent(new URL(req.url, "http://x").pathname));
            if (!file.startsWith(root)) { res.writeHead(403).end("Forbidden"); return; }
            if (fs.existsSync(file) && fs.statSync(file).isDirectory()) {
                file = ["index.dhtml", "index.html"].map((n) => path.join(file, n)).find((f) => fs.existsSync(f)) || file;
            }
            if (!fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404).end("Not found"); return; }
            const ext = path.extname(file).toLowerCase();
            const text = ext === ".dhtml" || ext === ".html" || ext === ".htm" ? fs.readFileSync(file, "utf8") : null;
            if (ext === ".dhtml" || (text !== null && /type\s*=\s*["']?(text|application)\/(dessa|ds)/i.test(text))) {
                try {
                    const html = await dessa.buildPage(file);
                    res.writeHead(200, { "Content-Type": TYPES[".html"], "Cache-Control": "no-store" }).end(html);
                    console.log("  " + req.url + " (compiled)");
                } catch (e) {
                    const message = e instanceof dessa.DessaError ? e.message : String(e && e.message || e);
                    console.error(message);
                    const safe = message.replace(/[<>&]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;" })[c]);
                    res.writeHead(200, { "Content-Type": TYPES[".html"] })
                       .end(`<!doctype html><meta charset="utf-8"><title>Dessa error</title><h2 style="color:#c62828">This page has errors</h2><pre>${safe}</pre>`);
                }
                return;
            }
            res.writeHead(200, { "Content-Type": TYPES[ext] || "application/octet-stream", "Cache-Control": "no-store" });
            fs.createReadStream(file).pipe(res);
        } catch (e) {
            res.writeHead(500).end(String(e));
        }
    });
    server.listen(port, "127.0.0.1", () => console.log(`Serving ${root} at http://localhost:${port}/  (Ctrl+C to stop)`));
}

main().catch(fail);
