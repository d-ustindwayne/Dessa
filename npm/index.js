// dessalang -- Dessa for Node: compiles Dessa programs and pages to
// WebAssembly and runs them.
//
//     const dessa = require("dessalang");
//     await dessa.build("app.ds");                 // app.wasm, and app.js to run it (node app.js)
//     const code = await dessa.run("app.ds");      // runs it as WebAssembly in Node
//     const html = await dessa.buildPage("page.dhtml");
//
// It drives the real Dessa compiler. The first time, it downloads the Dessa
// release of this version for your system and the WebAssembly compiler (about
// 60 MB, once) into ~/.dessa/npm/<version>. Set DESSALANG_DESSA to a `dessa`
// you already have to use that one instead.
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const https = require("https");
const { spawn, spawnSync } = require("child_process");

const VERSION = require("./package.json").version;
const REPO = "https://github.com/d-ustindwayne/Dessa";

class DessaError extends Error {
    constructor(message, diagnostics = []) {
        super(message);
        this.name = "DessaError";
        this.diagnostics = diagnostics;
    }
}

function platform() {
    if (process.platform === "win32" && process.arch === "x64") return "windows-x64";
    if (process.platform === "linux" && process.arch === "x64") return "linux-x64";
    if (process.platform === "darwin" && process.arch === "arm64") return "macos-arm64";
    throw new DessaError(`dessalang: there is no Dessa for ${process.platform}-${process.arch} yet (Windows x64, Linux x64 and macOS on Apple silicon are available)`);
}

function home() {
    return process.env.DESSALANG_HOME || path.join(os.homedir(), ".dessa", "npm", VERSION);
}

function exeName() {
    return process.platform === "win32" ? "dessa.exe" : "dessa";
}

// Downloads `url` (following GitHub's redirects) into `file`
function download(url, file) {
    return new Promise((resolve, reject) => {
        const get = (address, left) => {
            https.get(address, { headers: { "User-Agent": "dessalang" } }, (response) => {
                if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location && left > 0) {
                    response.resume();
                    get(new URL(response.headers.location, address).toString(), left - 1);
                    return;
                }
                if (response.statusCode !== 200) {
                    response.resume();
                    reject(new DessaError(`dessalang: could not download ${url} (HTTP ${response.statusCode})`));
                    return;
                }
                const out = fs.createWriteStream(file);
                response.pipe(out);
                out.on("finish", () => out.close(resolve));
                out.on("error", reject);
            }).on("error", (e) => reject(new DessaError(`dessalang: could not download ${url}: ${e.message}`)));
        };
        get(url, 10);
    });
}

function unpack(archive, into) {
    // tar reads .tar.gz everywhere and .zip on Windows 10 and newer (Windows' own tar.exe)
    const tar = process.platform === "win32" && process.env.SystemRoot ? path.join(process.env.SystemRoot, "System32", "tar.exe") : "tar";
    const result = spawnSync(tar, ["-xf", archive, "-C", into], { stdio: "inherit" });
    if (result.status !== 0) throw new DessaError(`dessalang: could not unpack ${archive}`);
}

let ready = null;

/**
 * The path of the `dessa` this package uses, downloading Dessa and the
 * WebAssembly compiler first if they aren't there yet.
 */
function install() {
    if (process.env.DESSALANG_DESSA) return Promise.resolve(process.env.DESSALANG_DESSA);
    if (ready) return ready;
    ready = (async () => {
        const name = `dessa-${VERSION}-${platform()}`;
        const folder = path.join(home(), name);
        const exe = path.join(folder, "bin", exeName());
        if (!fs.existsSync(exe)) {
            fs.mkdirSync(home(), { recursive: true });
            const archive = path.join(home(), name + (process.platform === "win32" ? ".zip" : ".tar.gz"));
            console.error(`dessalang: downloading Dessa ${VERSION} (once)...`);
            await download(`${REPO}/releases/download/v${VERSION}/${path.basename(archive)}`, archive);
            unpack(archive, home());
            fs.rmSync(archive, { force: true });
        }
        if (!fs.existsSync(path.join(folder, "wasm"))) {
            const setup = spawnSync(exe, ["setup", "wasm"], { stdio: ["ignore", process.stderr, "inherit"] });
            if (setup.status !== 0) throw new DessaError("dessalang: could not get the WebAssembly compiler (dessa setup wasm)");
        }
        return exe;
    })();
    ready.catch(() => (ready = null));
    return ready;
}

// Runs dessa with `args`; resolves to { code, stdout, stderr } (output passed through when `inherit`)
async function dessa(args, { inherit = false, cwd } = {}) {
    const exe = await install();
    return new Promise((resolve, reject) => {
        const child = spawn(exe, args, { cwd, stdio: inherit ? "inherit" : ["inherit", "pipe", "pipe"] });
        let stdout = "", stderr = "";
        if (!inherit) {
            child.stdout.on("data", (d) => (stdout += d));
            child.stderr.on("data", (d) => (stderr += d));
        }
        child.on("error", reject);
        child.on("close", (code) => resolve({ code: code ?? 1, stdout, stderr }));
    });
}

function isPage(file) {
    return /\.(dhtml|html?)$/i.test(file);
}

/** The errors and warnings of a .ds file or a page. */
async function check(file) {
    const r = await dessa(["check", file, "--json"]);
    try {
        return JSON.parse(r.stdout);
    } catch (e) {
        throw new DessaError(r.stderr.trim() || r.stdout.trim() || "dessalang: check failed");
    }
}

/**
 * Compiles a .ds file to WebAssembly: `out` (default app.wasm next to it), and
 * a .js file next to it that runs it with Node. Resolves to the .wasm path.
 */
async function build(file, options = {}) {
    const out = options.out || file.replace(/\.[^./\\]+$/, "") + ".wasm";
    const r = await dessa(["build", file, "--target", "wasm", "-o", out]);
    if (r.code !== 0) throw new DessaError((r.stderr + r.stdout).trim());
    return out;
}

/** A .dhtml page (or .html with Dessa scripts) with its scripts compiled to WebAssembly: one HTML file's text. */
async function buildPage(file) {
    const out = path.join(os.tmpdir(), `dessalang-${process.pid}-${Date.now()}.html`);
    const r = await dessa(["build", file, "-o", out]);
    if (r.code !== 0) throw new DessaError((r.stderr + r.stdout).trim());
    const html = fs.readFileSync(out, "utf8");
    fs.rmSync(out, { force: true });
    return html;
}

/** Runs a .ds file as WebAssembly in Node, with this terminal. Resolves to its exit code. */
async function run(file, args = []) {
    const r = await dessa(["run", file, "--target", "wasm", "--", ...args], { inherit: true });
    return r.code;
}

/** The Dessa version this package drives. */
function version() {
    return VERSION;
}

module.exports = { install, check, build, buildPage, run, version, isPage, DessaError, dessa };
