// Tests of the npm package (run: npm test, after npm run build)
"use strict";
const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const dessa = require(".");

(async () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "dessa-npm-"));
    fs.mkdirSync(path.join(tmp, "lib"));
    fs.writeFileSync(path.join(tmp, "lib", "util.ds"), "func[Int] twice(n[Int]) { return n * 2; }\n");
    fs.writeFileSync(path.join(tmp, "app.ds"), '{ twice } from $("lib.util");\nprint(twice(21), 7 / 2);\n');
    fs.writeFileSync(path.join(tmp, "bad.ds"), 'var[Int] x = "no";\n');
    fs.writeFileSync(path.join(tmp, "page.dhtml"), '<p id="x"></p>\n<script>\n    print("page");\n</script>\n<script type="text/javascript">let y = 1;</script>\n');
    fs.writeFileSync(path.join(tmp, "broken.dhtml"), '<p></p>\n<script>\n    var[Int] x = "no";\n</script>\n');

    assert.strictEqual(await dessa.version(), "0.0.6");
    const result = await dessa.compile('print("hi", [1, 2]);');
    assert.ok(result.ok && result.js.includes("$.print"));

    const js = await dessa.compileFile(path.join(tmp, "app.ds"));
    assert.ok(js.length > 1000);

    await assert.rejects(dessa.compileFile(path.join(tmp, "bad.ds")), (e) => e instanceof dessa.DessaError && /bad\.ds:1:14/.test(e.message));

    const html = await dessa.buildPage(path.join(tmp, "page.dhtml"));
    assert.ok(html.includes("<p id=\"x\"></p>") && html.includes("$.print") && html.includes("let y = 1;"));

    const diagnostics = await dessa.check(path.join(tmp, "broken.dhtml"));
    assert.strictEqual(diagnostics[0].line, 3, "error position inside the page");

    let out = "";
    const write = process.stdout.write.bind(process.stdout);
    process.stdout.write = (t) => { out += t; return true; };
    try {
        await dessa.runFile(path.join(tmp, "app.ds"));
    } finally {
        process.stdout.write = write;
    }
    assert.strictEqual(out, "42 3.5\n");
    console.log("all npm package tests passed");
})().catch((e) => { console.error(e); process.exit(1); });
