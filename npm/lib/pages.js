// Dessa pages: finding the Dessa scripts of a .dhtml / .html page.
// The same rules as the native compiler (src/driver/web.cpp):
//   .dhtml  every <script> without a type is Dessa (and type="text/dessa" / "text/ds")
//   .html   only <script type="text/dessa"> / "text/ds" / "application/dessa"
"use strict";
const path = require("path");

const DESSA_TYPES = new Set(["text/dessa", "text/ds", "application/dessa", "text/x-dessa"]);
const DESSA_EXTENSIONS = new Set([".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss"]);

function isDessaPage(file) {
    return path.extname(file).toLowerCase() === ".dhtml";
}

function isPage(file) {
    return [".dhtml", ".html", ".htm"].includes(path.extname(file).toLowerCase());
}

function attribute(attrs, name) {
    const m = new RegExp(`(?:^|\\s)${name}\\s*=\\s*(?:"([^"]*)"|'([^']*)'|([^\\s>]+))`, "i").exec(attrs);
    return m ? (m[1] ?? m[2] ?? m[3]) : null;
}

// Every <script> of a page (HTML comments skipped)
function scripts(file, page) {
    const dhtml = isDessaPage(file);
    const low = page.toLowerCase();
    const found = [];
    let pos = 0;
    while (true) {
        const open = low.indexOf("<script", pos);
        if (open < 0) break;
        const comment = low.indexOf("<!--", pos);
        if (comment >= 0 && comment < open) {
            const end = low.indexOf("-->", comment + 4);
            if (end < 0) break;
            pos = end + 3;
            continue;
        }
        const tagEnd = page.indexOf(">", open);
        if (tagEnd < 0) break;
        const close = low.indexOf("</script", tagEnd);
        if (close < 0) break;
        const end = page.indexOf(">", close);
        const closeEnd = end < 0 ? page.length : end + 1;
        const attrs = page.slice(open + 7, tagEnd);
        const type = attribute(attrs, "type");
        const src = attribute(attrs, "src");
        const dessa = (type !== null && DESSA_TYPES.has(type.toLowerCase())) ||
                      (dhtml && type === null && (src === null || DESSA_EXTENSIONS.has(path.extname(src).toLowerCase())));
        found.push({ open, tagEnd, close, closeEnd, dessa, src });
        pos = closeEnd;
    }
    return found;
}

// The script's source with blank lines and spaces in front, so that error
// positions are those of the page itself.
function padded(page, script) {
    const before = page.slice(0, script.tagEnd + 1);
    const line = (before.match(/\n/g) || []).length;
    const column = script.tagEnd + 1 - (before.lastIndexOf("\n") + 1);
    return "\n".repeat(line) + " ".repeat(column) + page.slice(script.tagEnd + 1, script.close);
}

// JavaScript inside <script>...</script>
function safeForScript(js) {
    return js.replace(/<\/script/gi, "<\\/script");
}

module.exports = { isDessaPage, isPage, scripts, padded, safeForScript };
