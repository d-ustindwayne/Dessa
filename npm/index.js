// dessa -- the Dessa compiler for JavaScript (Node). It runs the real Dessa
// compiler, built for WebAssembly, and gives back JavaScript.
//
//     const dessa = require("dessa");
//     const { js, diagnostics } = await dessa.compile('print("Hello");');
//     await dessa.runFile("app.ds");
//     const html = await dessa.buildPage("page.dhtml");
"use strict";
const fs = require("fs");
const path = require("path");
const pages = require("./lib/pages");

const DessaCompiler = require("./dessa.js");
const engine = DessaCompiler.Dessa;
const EXTENSIONS = [".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"];

class DessaError extends Error {
    constructor(diagnostics) {
        super(diagnostics.filter((d) => d.severity === "error").map(formatDiagnostic).join("\n") || "compile error");
        this.name = "DessaError";
        this.diagnostics = diagnostics;
    }
}

function formatDiagnostic(d) {
    return (d.file ? d.file + (d.line ? `:${d.line}:${d.column}` : "") + ": " : "") + `${d.severity}: ${d.message}`;
}

// The $("a.b") modules a source imports
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

// Where $("spec") points, like the native loader: next to the importing file,
// in the project folder, or in riv_modules (in the project or a folder above).
function resolveImport(spec, fromDir, projectDir) {
    const relative = spec.replace(/\./g, "/");
    for (const dir of [fromDir, projectDir]) {
        for (const ext of EXTENSIONS) {
            const candidate = path.join(dir, relative + ext);
            if (fs.existsSync(candidate)) return candidate;
        }
    }
    const pkg = spec.split(".")[0];
    const inPackage = spec.includes(".") ? [spec.slice(pkg.length + 1).replace(/\./g, "/")] : ["lib", pkg];
    for (let dir = projectDir; ; dir = path.dirname(dir)) {
        for (const name of inPackage) {
            for (const ext of EXTENSIONS) {
                const candidate = path.join(dir, "riv_modules", pkg, name + ext);
                if (fs.existsSync(candidate)) return candidate;
            }
        }
        if (path.dirname(dir) === dir) break;
    }
    return null;
}

// The files a program needs, keyed by their path relative to the project folder
function collectFiles(entry, source) {
    const projectDir = path.dirname(entry);
    const files = {};
    const seen = new Set([path.resolve(entry)]);
    const visit = (file, text) => {
        for (const spec of importsOf(text)) {
            const target = resolveImport(spec, path.dirname(file), projectDir);
            if (!target || seen.has(path.resolve(target))) continue;
            seen.add(path.resolve(target));
            const content = fs.readFileSync(target, "utf8");
            files[path.relative(projectDir, target).split(path.sep).join("/")] = content;
            visit(target, content);
        }
    };
    visit(entry, source);
    return files;
}

/**
 * Compiles Dessa source to JavaScript.
 * options.name: the file name used in messages (default "main.ds")
 * options.files: { "lib/util.ds": "..." } other files it imports
 * Returns { ok, js, diagnostics }.
 */
async function compile(source, options = {}) {
    return engine.compile(source, { name: options.name || "main.ds", files: options.files || {} });
}

/** Compiles a .ds file (and everything it imports) to JavaScript. Throws DessaError on errors. */
async function compileFile(file) {
    const source = fs.readFileSync(file, "utf8");
    const result = await compile(source, { name: path.basename(file), files: collectFiles(file, source) });
    if (!result.ok) throw new DessaError(result.diagnostics);
    return result.js;
}

/** Runs Dessa source here (print goes to stdout). Returns the exit code. */
async function run(source, options = {}) {
    const result = await compile(source, options);
    if (!result.ok) throw new DessaError(result.diagnostics);
    return engine.execute(result.js);
}

/** Runs a .ds file. Returns the exit code. */
async function runFile(file) {
    return engine.execute(await compileFile(file));
}

/**
 * A page (.dhtml, or .html with <script type="text/dessa">) with its Dessa
 * scripts compiled to JavaScript: plain HTML that runs anywhere.
 */
async function buildPage(file) {
    const page = fs.readFileSync(file, "utf8");
    const dir = path.dirname(file);
    let html = "";
    let pos = 0;
    const diagnostics = [];
    for (const script of pages.scripts(file, page)) {
        html += page.slice(pos, script.open);
        if (!script.dessa) {
            html += page.slice(script.open, script.closeEnd);
            pos = script.closeEnd;
            continue;
        }
        let result;
        if (script.src !== null) {
            const target = path.join(dir, script.src);
            const source = fs.readFileSync(target, "utf8");
            result = await compile(source, { name: path.basename(target), files: collectFiles(target, source) });
        } else {
            const source = pages.padded(page, script);
            result = await compile(source, { name: path.basename(file), files: collectFiles(file, source) });
        }
        diagnostics.push(...result.diagnostics);
        html += "<script>\n" + pages.safeForScript(result.js) + "\n</script>";
        pos = script.closeEnd;
    }
    html += page.slice(pos);
    if (diagnostics.some((d) => d.severity === "error")) throw new DessaError(diagnostics);
    return html;
}

/** Checks a .ds file or a page; returns the diagnostics (errors and warnings). */
async function check(file) {
    if (pages.isPage(file)) {
        try {
            await buildPage(file);
            return [];
        } catch (e) {
            if (e instanceof DessaError) return e.diagnostics;
            throw e;
        }
    }
    const source = fs.readFileSync(file, "utf8");
    const result = await compile(source, { name: path.basename(file), files: collectFiles(file, source) });
    return result.diagnostics;
}

/** The compiler's version. */
async function version() {
    return engine.version();
}

module.exports = { compile, compileFile, run, runFile, buildPage, check, version, formatDiagnostic, DessaError, isPage: pages.isPage };
