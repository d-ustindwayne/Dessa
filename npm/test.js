// Tests the package against a dessa: the one in DESSALANG_DESSA (in this
// repository: ../build/dessa after a build, with the WebAssembly compiler from
// `dessa setup wasm`), or else the release this version downloads.
//
//     DESSALANG_DESSA=../build/dessa bun run test.js
"use strict";
const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const dessa = require(".");

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dessalang-test-"));
const write = (name, text) => {
    const file = path.join(dir, name);
    fs.writeFileSync(file, text);
    return file;
};

(async () => {
    // A program to WebAssembly, run with Node
    const app = write("app.ds", '$("math") { Big }\nvar[Int<>] xs = <3, 1, 2>;\nxs.sort();\nprint(xs, new Big("2").pow(100));\n');
    const wasm = await dessa.build(app);
    assert(fs.existsSync(wasm), "build made the .wasm");
    const out = execFileSync("node", [wasm.replace(/\.wasm$/, ".js")]).toString();
    assert.strictEqual(out.replace(/\r\n/g, "\n"), "<1, 2, 3> 1267650600228229401496703205376\n");

    // A page: its script becomes WebAssembly inside the HTML
    const page = write("page.dhtml", '<!doctype html><html><body><p id="x"></p><script>\n$("webutils") { document }\nprint("hi");\n</script></body></html>');
    const html = await dessa.buildPage(page);
    assert(html.includes("DessaWasm.page("), "the page's script is WebAssembly");
    assert(!html.includes('$("webutils")'), "no Dessa source left in the page");

    // Errors come back as diagnostics
    const broken = write("broken.ds", "var[Int] x = \"text\";\n");
    const diagnostics = await dessa.check(broken);
    assert(diagnostics.some((d) => d.severity === "error"), "check reports the error");
    await assert.rejects(dessa.build(broken), dessa.DessaError);

    fs.rmSync(dir, { recursive: true, force: true });
    console.log("npm package: all tests passed");
})().catch((e) => {
    console.error(e);
    process.exitCode = 1;
});
