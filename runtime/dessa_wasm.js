// dessa_wasm.js -- runs Dessa compiled to WebAssembly, in Node or in a page.
//
//   node dessa_wasm.js app.wasm [arguments]       (what `dessa run --target wasm` does)
//   DessaWasm.page(bytes)                          (what a compiled page does)
//
// Two parts:
//   - WASI, the small system interface the C library inside the program uses:
//     output, input, arguments, the clock, random numbers, exit.
//   - the bridge to the page (import module "dessa", see runtime/dessa_web.c):
//     page objects are kept in a table and Dessa holds their numbers.
(function (root) {
    "use strict";
    if (root.DessaWasm) return;
    const isNode = typeof process !== "undefined" && !!(process.versions && process.versions.node) && typeof window === "undefined";
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();

    class Exit {
        constructor(code) { this.code = code; }
    }

    // The imports a program needs, and how to start it once it's made
    function environment(options) {
        const args = ["dessa", ...(options.args || [])];
        let instance = null;
        const memory = () => instance.exports.memory;
        const view = () => new DataView(memory().buffer);
        const u8 = () => new Uint8Array(memory().buffer);
        const text = (ptr, len) => decoder.decode(u8().subarray(ptr, ptr + len));

        // ---- WASI ----
        const SUCCESS = 0, BADF = 8, NOSYS = 52;
        const lines = { 1: "", 2: "" };
        const write = options.write || ((fd, s) => {
            // A page: whole lines to the console
            lines[fd] += s;
            let at;
            while ((at = lines[fd].indexOf("\n")) >= 0) {
                (fd === 2 ? console.error : console.log)(lines[fd].slice(0, at));
                lines[fd] = lines[fd].slice(at + 1);
            }
        });
        let pendingInput = new Uint8Array(0);
        const readInput = options.read || (() => {
            const answer = typeof prompt === "function" ? prompt(lines[1] || "") : null;
            lines[1] = "";
            return answer === null ? null : answer + "\n";
        });
        const strings = (list) => list.map((s) => encoder.encode(s + "\0"));
        const wasi = {
            args_sizes_get(countPtr, sizePtr) {
                const all = strings(args);
                view().setUint32(countPtr, all.length, true);
                view().setUint32(sizePtr, all.reduce((n, b) => n + b.length, 0), true);
                return SUCCESS;
            },
            args_get(argvPtr, bufPtr) {
                let at = bufPtr;
                strings(args).forEach((b, i) => {
                    view().setUint32(argvPtr + 4 * i, at, true);
                    u8().set(b, at);
                    at += b.length;
                });
                return SUCCESS;
            },
            environ_sizes_get(countPtr, sizePtr) {
                view().setUint32(countPtr, 0, true);
                view().setUint32(sizePtr, 0, true);
                return SUCCESS;
            },
            environ_get() { return SUCCESS; },
            fd_write(fd, iovs, count, writtenPtr) {
                if (fd !== 1 && fd !== 2) return BADF;
                let total = 0, out = "";
                for (let i = 0; i < count; i++) {
                    const ptr = view().getUint32(iovs + 8 * i, true), len = view().getUint32(iovs + 8 * i + 4, true);
                    out += text(ptr, len);
                    total += len;
                }
                write(fd, out);
                view().setUint32(writtenPtr, total, true);
                return SUCCESS;
            },
            fd_read(fd, iovs, count, readPtr) {
                if (fd !== 0) return BADF;
                if (pendingInput.length === 0) {
                    const more = readInput();
                    if (more === null) {
                        view().setUint32(readPtr, 0, true);
                        return SUCCESS;
                    }
                    pendingInput = typeof more === "string" ? encoder.encode(more) : more;
                }
                let total = 0;
                for (let i = 0; i < count && pendingInput.length; i++) {
                    const ptr = view().getUint32(iovs + 8 * i, true), len = view().getUint32(iovs + 8 * i + 4, true);
                    const n = Math.min(len, pendingInput.length);
                    u8().set(pendingInput.subarray(0, n), ptr);
                    pendingInput = pendingInput.subarray(n);
                    total += n;
                }
                view().setUint32(readPtr, total, true);
                return SUCCESS;
            },
            fd_fdstat_get(fd, ptr) {
                if (fd > 2) return BADF;
                const v = view();
                v.setUint8(ptr, options.terminal && fd > 0 ? 2 : 4);   // a character device (a terminal) or a file
                v.setUint16(ptr + 2, 0, true);
                v.setBigUint64(ptr + 8, 0xffffffffffffffffn, true);
                v.setBigUint64(ptr + 16, 0xffffffffffffffffn, true);
                return SUCCESS;
            },
            fd_prestat_get() { return BADF; },
            fd_close() { return SUCCESS; },
            fd_seek() { return NOSYS; },
            clock_time_get(id, precision, timePtr) {
                const ns = id === 0
                    ? BigInt(Date.now()) * 1000000n
                    : BigInt(Math.round((typeof performance !== "undefined" ? performance.now() : Date.now()) * 1e6));
                view().setBigUint64(timePtr, ns, true);
                return SUCCESS;
            },
            clock_res_get(id, resPtr) {
                view().setBigUint64(resPtr, 1000n, true);
                return SUCCESS;
            },
            random_get(ptr, len) {
                const bytes = u8().subarray(ptr, ptr + len);
                for (let at = 0; at < len; at += 65536) crypto.getRandomValues(bytes.subarray(at, Math.min(len, at + 65536)));
                return SUCCESS;
            },
            sched_yield() { return SUCCESS; },
            proc_exit(code) { throw new Exit(code); },
        };

        // ---- the page ----
        const objects = [undefined];
        const freeSlots = [];
        const keep = (v) => {
            if (v === null || v === undefined) return 0;
            const n = freeSlots.length ? freeSlots.pop() : objects.length;
            objects[n] = v;
            return n;
        };
        const names = new Map();
        const nameAt = (ptr, len) => {
            let name = names.get(ptr);
            if (name === undefined) names.set(ptr, (name = text(ptr, len)));
            return name;
        };
        let pushed = [];
        let result;
        let resultText = null;
        const browser = typeof window !== "undefined" && typeof document !== "undefined";
        const need = (what) => {
            if (!browser) throw new Error(`${what} needs a web browser (it isn't available in Node)`);
        };
        let httpAnswer = ["", "", ""];   // the last answer of _http
        // webutils' own functions (the calls on no object)
        const webutils = {
            _document: () => (browser ? document : null),
            _window: () => (browser ? window : null),
            alert: (m) => { need("alert()"); window.alert(m); },
            prompt: (q, a) => { need("prompt()"); return window.prompt(q, a); },
            confirm: (q) => { need("confirm()"); return window.confirm(q); },
            log: (parts) => console.log(parts.join(" ")),
            warn: (parts) => console.warn(parts.join(" ")),
            error: (parts) => console.error(parts.join(" ")),
            setTimeout: (f, ms) => { need("setTimeout()"); return window.setTimeout(f, ms); },
            setInterval: (f, ms) => { need("setInterval()"); return window.setInterval(f, ms); },
            clearTimer: (id) => { if (browser) { window.clearTimeout(id); window.clearInterval(id); } },
            fetchText: (url) => {
                need("fetchText()");
                const request = new XMLHttpRequest();
                try {
                    request.open("GET", url, false);   // Dessa code runs straight through: wait for the answer
                    request.send();
                } catch (e) {
                    throw new Error(`Could not download ${url}: ${e && e.message ? e.message : e}`);
                }
                if (request.status >= 400 || request.status === 0) throw new Error(`Could not download ${url} (HTTP ${request.status})`);
                return request.responseText;
            },
            // $("request") (request.c): one HTTP request, waited for. The answer's three parts
            // (status, headers, body) are read with _httpPart
            _http: (method, url, headers, body, timeout) => {
                const lines = headers.split("\n").filter((l) => l.includes(":"));
                const sends = body.length > 0 || ["POST", "PUT", "PATCH"].includes(method);
                const failed = (why) => new Error(`Request to ${url} failed: ${why}`);
                if (typeof XMLHttpRequest !== "undefined") {
                    // a page, or a Web Worker (the website's playground): Dessa code
                    // runs straight through, so the request is synchronous
                    const request = new XMLHttpRequest();
                    request.open(method, url, false);
                    for (const l of lines) {
                        const at = l.indexOf(":");
                        try { request.setRequestHeader(l.slice(0, at).trim(), l.slice(at + 1).trim()); } catch (e) { /* one the browser sets itself */ }
                    }
                    try {
                        request.send(sends ? body : null);
                    } catch (e) {
                        throw failed(e && e.message ? e.message : String(e));
                    }
                    if (request.status === 0) throw failed("the browser blocked it (another site that doesn't allow it?) or nothing answered");
                    httpAnswer = [String(request.status), `HTTP/1.1 ${request.status} ${request.statusText}\r\n` + request.getAllResponseHeaders(), request.responseText];
                    return 0;
                }
                // Node: fetch is asynchronous, so a short child process does it and Dessa waits for that
                const script = `let s = ""; process.stdin.on("data", (d) => (s += d)).on("end", async () => {
                    const q = JSON.parse(s), h = {};
                    for (const l of q.lines) { const at = l.indexOf(":"); h[l.slice(0, at).trim()] = l.slice(at + 1).trim(); }
                    try {
                        const r = await fetch(q.url, { method: q.method, headers: h, body: q.sends ? q.body : undefined, redirect: "follow",
                                                       signal: AbortSignal.timeout(q.timeout * 1000) });
                        const body = await r.text();
                        let headers = "HTTP/1.1 " + r.status + " " + r.statusText + "\\r\\n";
                        r.headers.forEach((v, k) => { headers += k + ": " + v + "\\r\\n"; });
                        process.stdout.write(JSON.stringify({ status: r.status, headers, body }));
                    } catch (e) {
                        process.stderr.write(String((e && e.cause && e.cause.message) || (e && e.message) || e));
                        process.exit(1);
                    }
                });`;
                const seconds = timeout > 0 ? timeout : 30;
                let out;
                try {
                    out = require("child_process").execFileSync(process.execPath, ["-e", script], {
                        input: JSON.stringify({ method, url, lines, body, sends, timeout: seconds }),
                        maxBuffer: 1 << 30, timeout: seconds * 1000 + 10000, stdio: ["pipe", "pipe", "pipe"],
                    });
                } catch (e) {
                    throw failed((e.stderr && e.stderr.toString().trim()) || e.message);
                }
                const answer = JSON.parse(out.toString());
                httpAnswer = [String(answer.status), answer.headers, answer.body];
                return 0;
            },
            _httpPart: (part) => httpAnswer[part],
            // $("time") (include/time/time.c): minutes east of UTC of a zone at an instant, NaN for an unknown zone
            _tzOffset: (zone, utc) => {
                if (zone === "local") return -new Date(utc).getTimezoneOffset();
                try {
                    const part = new Intl.DateTimeFormat("en-US", { timeZone: zone, timeZoneName: "longOffset" })
                        .formatToParts(new Date(utc)).find((p) => p.type === "timeZoneName");
                    const m = part && /^GMT(?:([+-])(\d{1,2})(?::(\d{2}))?)?$/.exec(part.value);   // "GMT+08:00", "GMT"
                    if (!m) return NaN;
                    return m[1] ? (m[1] === "-" ? -1 : 1) * (Number(m[2]) * 60 + Number(m[3] || 0)) : 0;
                } catch (e) {
                    return NaN;   // Intl doesn't know the zone
                }
            },
        };
        const callback = (f, kind, arg) => (event) => {
            try {
                instance.exports.dsw_invoke(f, kind, kind === 1 ? keep(event) : 0, arg);
            } catch (e) {
                if (!(e instanceof Exit)) console.error(e);
            }
        };
        const take = () => {
            const v = result;
            result = undefined;
            return v;
        };
        const bridge = {
            push_str: (ptr, len) => pushed.push(text(ptr, len)),
            push_null: () => pushed.push(null),
            push_num: (v) => pushed.push(v),
            push_bool: (v) => pushed.push(v !== 0),
            push_obj: (n) => pushed.push(objects[n]),
            push_fn: (f, kind, arg) => pushed.push(callback(f, kind, arg)),
            pack: (count) => pushed.push(pushed.splice(pushed.length - count, count)),
            get: (n, ptr, len) => {
                pushed = [];
                result = (n ? objects[n] : webutils)[nameAt(ptr, len)];
            },
            set: (n, ptr, len) => {
                const value = pushed[0];
                pushed = [];
                objects[n][nameAt(ptr, len)] = value;
            },
            call: (n, ptr, len) => {
                const values = pushed;
                pushed = [];
                const target = n ? objects[n] : webutils;
                const name = nameAt(ptr, len);
                try {
                    result = target[name](...values);
                    return 0;
                } catch (e) {
                    result = e && e.message ? e.message : String(e);
                    return 1;
                }
            },
            take_num: () => Number(take()),
            take_bool: () => (take() ? 1 : 0),
            take_obj: () => keep(take()),
            text_len: () => {
                const v = take();
                if (v === null || v === undefined) return -1;
                resultText = encoder.encode(String(v));
                return resultText.length;
            },
            text_copy: (ptr) => {
                u8().set(resultText, ptr);
                resultText = null;
            },
            length: (n) => objects[n].length >>> 0,
            item: (n, i) => keep(objects[n][i]),
            drop: (n) => {
                objects[n] = undefined;
                freeSlots.push(n);
            },
        };

        return {
            imports(module) {
                const imports = { wasi_snapshot_preview1: {}, dessa: bridge, env: {} };
                for (const im of WebAssembly.Module.imports(module)) {
                    if (im.module === "wasi_snapshot_preview1") imports.wasi_snapshot_preview1[im.name] = wasi[im.name] || (() => NOSYS);
                    // Emscripten's notes to its own JavaScript (emscripten_notify_memory_growth): nothing to do
                    else if (im.module === "env" && im.kind === "function") imports.env[im.name] = () => 0;
                }
                return imports;
            },
            attach(made) {
                instance = made;
            },
            // Returns the exit code
            run(made) {
                instance = made;
                const e = instance.exports;
                try {
                    if (e._start) e._start();                       // a program: main()
                    else {
                        if (e._initialize) e._initialize();         // a page: its code, then the listeners
                        e.dsw_start();
                    }
                } catch (x) {
                    if (x instanceof Exit) return x.code;
                    throw x;
                }
                return 0;
            },
        };
    }

    function start(bytes, options) {
        const env = environment(options);
        const module = new WebAssembly.Module(bytes);
        return env.run(new WebAssembly.Instance(module, env.imports(module)));
    }

    function fromBase64(b64) {
        const raw = atob(b64);
        const bytes = new Uint8Array(raw.length);
        for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
        return bytes;
    }

    // A page's scripts run one after the other, in the page's order
    let queue = Promise.resolve();

    root.DessaWasm = {
        // A page's script (the WebAssembly as base64 or bytes). Browsers make
        // a big module only in the background, so it runs a moment later
        page: (wasm) => {
            const bytes = typeof wasm === "string" ? fromBase64(wasm) : wasm;
            queue = queue.then(async () => {
                const env = environment({});
                const module = await WebAssembly.compile(bytes);
                env.run(await WebAssembly.instantiate(module, env.imports(module)));
            }).catch((e) => console.error(e));
            return queue;
        },
        // Waits until the page's scripts so far have run
        ready: () => queue,
        // A program's bytes, run here with these options ({args, write(fd, text),
        // read() -> text or null, terminal}); returns the exit code. The
        // website's playground runs programs this way in a Web Worker.
        run: (bytes, options) => start(bytes, options || {}),
        // A program in Node: output to the terminal, input from it; returns the exit code
        main: (file, args) => {
            const fs = require("fs");
            // Node's own WASI, with files: the current folder (and / elsewhere than Windows)
            let NodeWasi = null;
            const warn = process.emitWarning;
            process.emitWarning = (w, ...rest) => { if (!String(w).includes("WASI")) warn.call(process, w, ...rest); };
            try {
                NodeWasi = require("wasi").WASI;
            } catch (e) {
                NodeWasi = null;
            }
            if (NodeWasi) {
                // The WASI C library reads "." and "/" as the same folder, so the current
                // folder gets a name of its own (/cwd), and the program starts in it
                // (DESSA_WASM_CWD, see ds_main); elsewhere than Windows "/" is the disk
                const preopens = { "/cwd": process.cwd() };
                if (process.platform !== "win32") preopens["/"] = "/";
                const variables = Object.assign({}, process.env, { DESSA_WASM_CWD: "/cwd" });
                const wasi = new NodeWasi({ version: "preview1", args: ["dessa", ...args], env: variables, preopens, returnOnExit: true });
                process.emitWarning = warn;
                const env = environment({ args });
                const module = new WebAssembly.Module(fs.readFileSync(file));
                const imports = env.imports(module);
                // getImportObject: Node 20+; wasiImport: older Node and Bun
                imports.wasi_snapshot_preview1 = typeof wasi.getImportObject === "function" ? wasi.getImportObject().wasi_snapshot_preview1 : wasi.wasiImport;
                const instance = new WebAssembly.Instance(module, imports);
                env.attach(instance);
                return wasi.start(instance);
            }
            process.emitWarning = warn;
            const out = { 1: [], 2: [] };
            const flush = (fd) => {
                if (out[fd].length) fs.writeSync(fd, out[fd].join(""));
                out[fd] = [];
            };
            const options = {
                args,
                terminal: !!process.stdout.isTTY,
                write: (fd, s) => {
                    out[fd].push(s);
                    if (fd === 2 || out[fd].length > 256) flush(fd);
                },
                read: () => {
                    flush(1);
                    const buffer = Buffer.alloc(65536);
                    let n = 0;
                    try {
                        n = fs.readSync(0, buffer, 0, buffer.length, null);
                    } catch (err) {
                        if (err.code !== "EOF") throw err;
                    }
                    return n > 0 ? buffer.subarray(0, n) : null;
                },
            };
            let code;
            try {
                code = start(fs.readFileSync(file), options);
            } finally {
                flush(1);
                flush(2);
            }
            return code;
        },
    };

    // `node dessa_wasm.js app.wasm`, not a built app.js that has this file inside it
    if (isNode && typeof require !== "undefined" && typeof module !== "undefined" && require.main === module &&
        /dessa_wasm\.js$/.test(__filename) && process.argv[2]) {
        process.exitCode = root.DessaWasm.main(process.argv[2], process.argv.slice(3));
    }
})(typeof globalThis !== "undefined" ? globalThis : this);
