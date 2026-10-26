// webutils.js -- the JavaScript part of $("webutils") (see webutils.dm).
// Added to programs built with --target web that import webutils. Values
// arrive as Dessa represents them (Int = a number, nil = null).
$.webutils = (() => {
    const browser = typeof window !== "undefined" && typeof document !== "undefined";
    const need = (what) => { if (!browser) $.fail(`${what} needs a web browser (it isn't available in Node)`); };
    const text = (parts) => parts.map((p) => $.str(p)).join(" ");
    return {
        _document: () => browser ? document : null,
        _window: () => browser ? window : null,
        alert: (message) => { need("alert()"); window.alert(message); },
        prompt: (question, answer) => { need("prompt()"); return window.prompt(question, answer) ?? null; },
        confirm: (question) => { need("confirm()"); return window.confirm(question); },
        log: (parts) => console.log(text(parts)),
        warn: (parts) => console.warn(text(parts)),
        error: (parts) => console.error(text(parts)),
        setTimeout: (action, ms) => { need("setTimeout()"); return window.setTimeout($.callback(action), Number(ms)); },
        setInterval: (action, ms) => { need("setInterval()"); return window.setInterval($.callback(action), Number(ms)); },
        clearTimer: (id) => { if (browser) { window.clearTimeout(Number(id)); window.clearInterval(Number(id)); } },
        fetchText: (url) => {
            need("fetchText()");
            const request = new XMLHttpRequest();
            try {
                request.open("GET", url, false);   // Dessa code runs straight through: wait for the answer
                request.send();
            } catch (e) {
                $.fail(`Could not download ${url}: ${e && e.message ? e.message : e}`);
            }
            if (request.status >= 400 || request.status === 0) $.fail(`Could not download ${url} (HTTP ${request.status})`);
            return request.responseText;
        },
    };
})();
