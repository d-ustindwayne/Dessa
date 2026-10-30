// The playground's toolchain, in a Web Worker: the real dessa command built for
// WebAssembly (compiler/dessa.mjs, from src/main.cpp), and clang built for
// WebAssembly (@yowasp/clang, downloaded the first time a program runs) to
// turn the C that dessa emits into a program.
//
// Messages in:  {id, type: "command", args, files, cwd}   any dessa command
//               {id, type: "compile", path, files, cwd}   a .ds file to a WebAssembly program
// Messages out: {type: "status", ...} while loading, then {id, code, out, err, files, wasm}
//
// `files` is the workspace: {"/main.ds": "text", ...}. After a command, the
// files it wrote or changed come back in `files`.

const WORK = "/work";
let compiler = null;   // the Emscripten module, made once
let output = { out: "", err: "" };

const status = (what) => postMessage({ type: "status", ...what });

async function loadCompiler() {
    if (compiler) return compiler;
    status({ compiler: "loading" });
    const { default: create } = await import("./compiler/dessa.mjs");
    compiler = await create({
        print: (s) => (output.out += s + "\n"),
        printErr: (s) => (output.err += s + "\n"),
        preRun: [(m) => {
            m.ENV.DESSA_HOME = "/dessa";
            m.ENV.DESSA_PACKAGES = "/dessa/packages";
            m.ENV.RIV_NO_UPDATE_CHECK = "1";
        }],
    });
    compiler.FS.mkdir(WORK);
    status({ compiler: "ready" });
    return compiler;
}

// ---- the workspace in the compiler's file system ----
let written = new Map();   // path -> the text written there, to see what a command changed

function mkdirs(fs, path) {
    let at = "";
    for (const part of path.split("/").filter(Boolean)) {
        at += "/" + part;
        try { fs.mkdir(at); } catch (e) { /* already there */ }
    }
}

function writeWorkspace(m, files) {
    const fs = m.FS;
    for (const path of written.keys()) {
        if (!(path in files)) try { fs.unlink(WORK + path); } catch (e) { /* gone */ }
    }
    written = new Map();
    for (const [path, content] of Object.entries(files)) {
        const full = WORK + path;
        mkdirs(fs, full.slice(0, full.lastIndexOf("/")));
        fs.writeFile(full, content);
        written.set(path, content);
    }
}

// Files the command made or changed (dessa emit -o, fix-lists)
function changedFiles(m) {
    const fs = m.FS, changed = {};
    const walk = (dir) => {
        for (const name of fs.readdir(dir)) {
            if (name === "." || name === "..") continue;
            const full = `${dir}/${name}`;
            const st = fs.stat(full);
            if (fs.isDir(st.mode)) walk(full);
            else {
                const path = full.slice(WORK.length);
                const data = fs.readFile(full);
                const before = written.get(path);
                const same = before !== undefined && (typeof before === "string" ? new TextDecoder().decode(data) === before : equalBytes(before, data));
                if (!same) changed[path] = looksLikeText(data) ? new TextDecoder().decode(data) : data;
            }
        }
    };
    walk(WORK);
    return changed;
}

const equalBytes = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const looksLikeText = (data) => !data.subarray(0, 4096).includes(0);

function runDessa(m, args, cwd) {
    output = { out: "", err: "" };
    m.FS.chdir(WORK + (cwd === "/" ? "" : cwd));
    let code;
    try {
        code = m.callMain(args);
    } catch (e) {
        // exit() from C++ (or a crash): the module may not be usable again
        code = typeof e?.status === "number" ? e.status : 1;
        if (typeof e?.status !== "number") {
            output.err += `dessa: internal error in the playground's compiler (${e?.message || e})\n`;
            compiler = null;
        }
    }
    try { m._dessa_flush(); } catch (e) { /* the module is gone */ }
    return code;
}

// ---- clang, the first time a program is built ----
let clang = null;

async function loadClang() {
    if (clang) return clang;
    status({ clang: "loading", done: 0, total: 0 });
    // The files are gzipped (GitHub Pages doesn't compress .wasm): this fetch
    // unpacks them for @yowasp/clang, which takes globalThis.fetch when it loads
    const sizes = await (await fetch(new URL("./clang/sizes.json", import.meta.url))).json();
    const plainFetch = globalThis.fetch.bind(globalThis);
    globalThis.fetch = async (url, options) => {
        const u = new URL(url instanceof Request ? url.url : String(url), location.href);
        const name = u.pathname.split("/").pop();
        if (!u.pathname.includes("/playground/clang/") || !(name in sizes)) return plainFetch(url, options);
        const response = await plainFetch(u.href + ".gz", options);
        if (!response.ok) throw new Error(`could not download ${name} (HTTP ${response.status})`);
        const body = response.body.pipeThrough(new DecompressionStream("gzip"));
        return new Response(body, {
            headers: { "content-type": name.endsWith(".wasm") ? "application/wasm" : "application/octet-stream", "content-length": String(sizes[name]) },
        });
    };
    const total = Object.values(sizes).reduce((a, b) => a + b, 0);
    const done = new Map();
    const progress = (event) => {
        done.set(event.source, event.doneLength);
        status({ clang: "loading", done: [...done.values()].reduce((a, b) => a + b, 0), total });
    };
    const { runClang } = await import("./clang/bundle.js");
    // Downloads and compiles clang now (a tiny file), so the first run's time is the program's own
    await runClang(["clang", "--version"], {}, { fetchProgress: progress, stdout: () => {}, stderr: () => {} });
    clang = runClang;
    status({ clang: "ready" });
    return clang;
}

// ---- the runtime and libraries, compiled ahead of time (scripts/playground.ts) ----
let objectList = null;
const objectCache = new Map();

async function objectsFor(libraries) {
    if (!objectList) objectList = await (await fetch(new URL("./objects/objects.json", import.meta.url))).json();
    const files = {}, link = [];
    for (const lib of ["runtime", ...libraries]) {
        const entry = objectList.libraries[lib];
        if (!entry) throw new Error(`the library '${lib}' can't run in the playground (it has no C part for WebAssembly)`);
        for (const name of entry.objects) {
            if (!objectCache.has(name)) {
                const response = await fetch(new URL(`./objects/${name}`, import.meta.url));
                if (!response.ok) throw new Error(`could not download ${name}`);
                objectCache.set(name, new Uint8Array(await response.arrayBuffer()));
            }
            files[name] = objectCache.get(name);
        }
        link.push(...entry.link);
    }
    return { files, link, flags: objectList.flags };
}

let headers = null;
async function runtimeHeaders() {
    if (headers) return headers;
    headers = {};
    for (const name of ["dessa_rt.h", "dessa_web.h"]) {
        headers[name] = compiler.FS.readFile(`/dessa/runtime/${name}`, { encoding: "utf8" });
    }
    return headers;
}

// .ds -> C (dessa emit --target wasm, the same C `dessa build --target wasm` makes) -> WebAssembly
async function compile(m, path, cwd) {
    const code = runDessa(m, ["emit", path, "--target", "wasm", "-o", "/tmp/program.c"], cwd);
    if (code !== 0) return { code, out: output.out, err: output.err };
    const source = m.FS.readFile("/tmp/program.c", { encoding: "utf8" });
    const libraries = (/\/\* dessa-glue:(.*?)\*\//.exec(source)?.[1] || "").trim().split(/\s+/).filter(Boolean);
    const { files, link, flags } = await objectsFor(libraries);
    const run = await loadClang();
    let errors = "";
    const decoder = new TextDecoder();
    try {
        const result = await run(
            ["clang", ...flags, "-lsetjmp", "-Wl,-z,stack-size=8388608", "program.c", ...Object.keys(files), ...link, "-o", "program.wasm", "-lm"],
            { "program.c": source, ...(await runtimeHeaders()), ...files },
            { stderr: (b) => { if (b) errors += decoder.decode(b, { stream: true }); }, stdout: () => {} },
        );
        return { code: 0, out: "", err: "", wasm: result["program.wasm"] };
    } catch (e) {
        // The C that dessa makes always compiles; if it doesn't, it's a bug in Dessa
        return { code: 1, out: "", err: `dessa: internal error -- clang couldn't build the program. Please report it.\n${errors || e.message}\n` };
    }
}

onmessage = async (event) => {
    const { id, type, args, path, files, cwd = "/" } = event.data;
    try {
        if (type === "preload-clang") {
            await loadClang();
            postMessage({ id, code: 0 });
            return;
        }
        const m = await loadCompiler();
        writeWorkspace(m, files || {});
        if (type === "compile") {
            const result = await compile(m, path, cwd);
            postMessage({ id, ...result }, result.wasm ? [result.wasm.buffer] : []);
        } else {
            const code = runDessa(m, args, cwd);
            postMessage({ id, code, out: output.out, err: output.err, files: compiler ? changedFiles(m) : {} });
        }
    } catch (e) {
        postMessage({ id, code: 1, out: "", err: `${e?.message || e}\n` });
    }
};
