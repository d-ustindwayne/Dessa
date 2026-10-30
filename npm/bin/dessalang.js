#!/usr/bin/env node
// dessalang -- Dessa compiled to WebAssembly, from the command line (no C compiler needed).
//
//   dessalang run app.ds [args]          run it as WebAssembly in Node
//   dessalang build app.ds [-o app.wasm] app.wasm, and app.js to run it (node app.js)
//   dessalang build page.dhtml [-o out]  page.html with its Dessa scripts as WebAssembly
//   dessalang run page.dhtml             serve the page's folder and open it in the browser
//   dessalang check app.ds|page.dhtml    errors only
//   dessalang serve [folder] [--port N]  a web server; .dhtml pages compiled on each request
"use strict";
const dessa = require("..");

const args = process.argv.slice(2);
const command = args[0];

function usage() {
    console.log(`dessalang ${dessa.version()}: Dessa compiled to WebAssembly

  dessalang run <file.ds> [args]          run it as WebAssembly in Node
  dessalang run <page.dhtml>              serve the page and open it in the browser
  dessalang build <file.ds> [-o out.wasm] app.wasm, and app.js to run it (node app.js)
  dessalang build <page.dhtml> [-o out]   page.html with its scripts compiled to WebAssembly
  dessalang check <file.ds|page.dhtml>    show errors
  dessalang serve [folder] [--port N]     serve a folder; .dhtml pages are compiled on each request
  dessalang setup                         download Dessa and the WebAssembly compiler now
  dessalang version`);
}

async function main() {
    switch (command) {
        case "version":
        case "--version":
            console.log(dessa.version());
            return 0;
        case "setup":
            console.log(await dessa.install());
            return 0;
        case "run":
        case "build":
        case "check":
        case "serve": {
            // a .ds program goes to WebAssembly; pages always do
            const rest = args.slice(1);
            const file = rest.find((a) => !a.startsWith("-"));
            const wasm = (command === "run" || command === "build") && file && !dessa.isPage(file) && !rest.includes("--target");
            if (wasm) {
                const dash = rest.indexOf("--");
                rest.splice(dash >= 0 ? dash : rest.length, 0, "--target", "wasm");
            }
            const r = await dessa.dessa([command, ...rest], { inherit: true });
            return r.code;
        }
        default:
            usage();
            return command && command !== "help" && command !== "--help" ? 1 : 0;
    }
}

main().then(
    (code) => (process.exitCode = code),
    (error) => {
        console.error(error && error.message ? error.message : error);
        process.exitCode = 1;
    },
);
