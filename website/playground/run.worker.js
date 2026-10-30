// Runs one compiled Dessa program in the playground (a Web Worker of its own,
// so Stop can end it at any moment). It uses the same loader as programs
// built with `dessa build --target wasm` (runtime/dessa_wasm.js).
//
// Message in:   {wasm, args, input}  input: a SharedArrayBuffer for input(), or null
// Messages out: {type: "out", fd, text}, {type: "input"} (the program waits for a line),
//               {type: "exit", code}
//
// The input buffer: Int32 [0] = 1 when a line is ready, [1] = its byte length,
// then the bytes from byte 8. Without one (the page isn't cross-origin
// isolated), input() gets the end of the input.
importScripts("dessa_wasm.js");

onmessage = (event) => {
    const { wasm, args, input } = event.data;
    const flag = input ? new Int32Array(input, 0, 2) : null;
    const bytes = input ? new Uint8Array(input, 8) : null;
    let pending = [];
    const flush = () => {
        for (const p of pending) postMessage(p);
        pending = [];
    };
    let code;
    try {
        code = DessaWasm.run(wasm, {
            args,
            terminal: true,
            write: (fd, text) => {
                pending.push({ type: "out", fd, text });
                if (pending.length > 64) flush();
            },
            read: () => {
                flush();
                if (!flag) return null;
                Atomics.store(flag, 0, 0);
                postMessage({ type: "input" });
                Atomics.wait(flag, 0, 0);
                const length = Atomics.load(flag, 1);
                if (length < 0) return null;   // Ctrl+D: the end of the input
                return bytes.slice(0, length);
            },
        });
    } catch (e) {
        pending.push({ type: "out", fd: 2, text: `The program stopped: ${e && e.message ? e.message : e}\n` });
        code = 1;
    }
    flush();
    postMessage({ type: "exit", code });
};
