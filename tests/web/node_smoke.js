// Smoke test of web/dessa.js (the compiler built for the browser), in Node:
//     node tests/web/node_smoke.js
// Compiles programs with the WebAssembly compiler, runs them, checks output.
// The browser pages in this folder (webutils.html, ../../web/example) are
// checked by hand or with a browser driver.
const path = require("path");
const DessaCompiler = require(path.join(__dirname, "..", "..", "web", "dessa.js"));
const { Dessa } = DessaCompiler;

const cases = [
    {
        name: "values and classes",
        source: `
class Point {
    var[Int] x = 0;
    init(x[Int]) {
        this.x = x;
    }
}
print(<new Point(1), new Point(2)>.map((p[Point]) := p.x * 10), 7 / 2, 2 ^ 62, "é".length());`,
        expect: "<10, 20> 3.5 4611686018427387904 1",
    },
    {
        name: "imports, json, time",
        files: { "lib/util.ds": "func[Int] twice(n[Int]) { return n * 2; }\n" },
        source: `
$("lib.util") { twice }
$("json") { parseJson }
$("time") { dateOf }
print(twice(21), parseJson("[1, 2]"), dateOf(2026, 1, 1, zone: "UTC").format("EEEE", "fil"));`,
        expect: "42 [1,2] Huwebes",
    },
    {
        name: "runtime error",
        source: `var[Int<>] xs = <>;\nprint(xs<1>);`,
        expectError: "Index 1 is out of range for list of length 0",
    },
];

(async () => {
    let failed = 0;
    for (const c of cases) {
        const lines = [], errors = [];
        globalThis.DessaHooks = { onLine: (l) => lines.push(l), onErrorLine: (l) => errors.push(l) };
        const savedWrite = process.stdout.write.bind(process.stdout), savedErr = process.stderr.write.bind(process.stderr);
        let out = "", err = "";
        process.stdout.write = (t) => { out += t; return true; };
        process.stderr.write = (t) => { err += t; return true; };
        try {
            const result = await Dessa.compile(c.source, { files: c.files || {} });
            if (!result.ok) throw new Error(result.diagnostics.map((d) => d.message).join("; "));
            Dessa.execute(result.js);
        } catch (e) {
            err += String(e.message || e);
        } finally {
            process.stdout.write = savedWrite;
            process.stderr.write = savedErr;
        }
        const ok = c.expectError ? err.includes(c.expectError) : out.trim() === c.expect;
        console.log(`${ok ? "ok  " : "FAIL"} ${c.name}${ok ? "" : `\n     got: ${out.trim()} ${err.trim()}`}`);
        if (!ok) failed++;
    }
    const bad = await Dessa.compile('var[Int] x = "no";');
    const diagnosticOk = !bad.ok && bad.diagnostics[0].line === 1 && /Cannot convert Str to Int/.test(bad.diagnostics[0].message);
    console.log(`${diagnosticOk ? "ok  " : "FAIL"} compile errors`);
    if (!diagnosticOk) failed++;
    process.exitCode = failed ? 1 : 0;
})();
