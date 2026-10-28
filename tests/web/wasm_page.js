// A page compiled to WebAssembly, run in Node against a small stand-in for
// the browser's page: builds tests/web/wasm/page.dhtml with `dessa build`
// and checks what its script did to the page.
//
//   node tests/web/wasm_page.js        (needs build/dessa and a WebAssembly compiler)
"use strict";
const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

const root = path.resolve(__dirname, "..", "..");
const exe = path.join(root, "build", process.platform === "win32" ? "dessa.exe" : "dessa");
const out = path.join(os.tmpdir(), `dessa_wasm_page_${process.pid}.html`);
execFileSync(exe, ["build", path.join(__dirname, "wasm", "page.dhtml"), "-o", out], { stdio: ["ignore", "inherit", "inherit"] });
const html = fs.readFileSync(out, "utf8");
fs.unlinkSync(out);
assert(html.includes("DessaWasm.page("), "the script was compiled to WebAssembly");

// ---- the page, as far as the test needs it ----
class Element {
    constructor(tag, id = "") {
        this.tagName = tag.toUpperCase();
        this.id = id;
        this.textContent = "";
        this.style = { color: "" };
        const classes = new Set();
        this.classList = {
            add: (c) => classes.add(c),
            remove: (c) => classes.delete(c),
            contains: (c) => classes.has(c),
            toggle: (c) => (classes.has(c) ? (classes.delete(c), false) : (classes.add(c), true)),
        };
        this.children = [];
        this.listeners = {};
    }
    appendChild(child) {
        this.children.push(child);
        return child;
    }
    addEventListener(type, f) {
        (this.listeners[type] = this.listeners[type] || []).push(f);
    }
    click() {
        for (const f of this.listeners.click || []) f({ type: "click", target: this });
    }
    find(id) {
        if (this.id === id) return this;
        for (const c of this.children) {
            const found = c.find(id);
            if (found) return found;
        }
        return null;
    }
}
const body = new Element("body");
const app = body.appendChild(new Element("div", "app"));
const list = body.appendChild(new Element("ul", "list"));
const document = {
    body,
    title: "",
    getElementById: (id) => body.find(id),
    querySelector: (selector) => (selector.startsWith("#") ? body.find(selector.slice(1)) : null),
    createElement: (tag) => new Element(tag),
};
const window = { innerWidth: 1024, document };
const logged = [];
const consoleStandIn = { log: (...a) => logged.push(a.join(" ")), error: (...a) => logged.push("ERROR " + a.join(" ")), warn: console.warn };

// ---- run the page's scripts ----
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const page = new Function("window", "document", "console", scripts.join("\n;\n"));
page(window, document, consoleStandIn);
const DessaWasm = globalThis.DessaWasm;

DessaWasm.ready().then(() => {
    assert.strictEqual(app.textContent, "clicked 2 (click)!");
    assert.strictEqual(app.style.color, "red");
    assert(app.classList.contains("ready"));
    assert.deepStrictEqual(list.children.map((c) => c.textContent), ["item 1", "item 2", "item 3"]);
    assert.strictEqual(body.children[2].id, "btn");
    assert.strictEqual(document.title, "sum 2999997");
    assert.deepStrictEqual(logged.filter((l) => !l.startsWith("ERROR")), ["children: 3", "wide true", "true", "caught in the page", "still running"]);
    assert(logged.some((l) => l.startsWith("ERROR") && l.includes("oops")), "the listener's error is shown: " + logged.join(" | "));
    console.log("ok   a page as WebAssembly (page objects, events, errors)");
}).catch((e) => {
    console.error(e);
    process.exit(1);
});
