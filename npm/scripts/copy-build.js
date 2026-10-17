// Copies the WebAssembly build (../web/dessa.js) and the license into the package.
// Build dessa.js first: emcmake cmake -S .. -B ../build-wasm && cmake --build ../build-wasm
const fs = require("fs");
const path = require("path");
const root = path.join(__dirname, "..", "..");
const built = path.join(root, "web", "dessa.js");
if (!fs.existsSync(built)) {
    console.error("web/dessa.js is missing. Build it with Emscripten first:\n" +
                  "  emcmake cmake -S . -B build-wasm && cmake --build build-wasm");
    process.exit(1);
}
fs.copyFileSync(built, path.join(__dirname, "..", "dessa.js"));
fs.copyFileSync(path.join(root, "LICENSE"), path.join(__dirname, "..", "LICENSE"));
console.log("copied dessa.js and LICENSE");
