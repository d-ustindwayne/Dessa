# dessalang: Dessa for JavaScript

The [Dessa](https://github.com/d-ustindwayne/Dessa) programming language for
JavaScript. This package holds the real Dessa compiler built for WebAssembly.
It compiles Dessa to JavaScript in Node or in the browser, without a C++
compiler.

```
npm install dessalang
```

## In Node

```js
const dessa = require("dessalang");

// Source to JavaScript
const { ok, js, diagnostics } = await dessa.compile('print("Hello", 2 ^ 10);');

// A file and everything it imports (also packages in riv_modules)
const code = await dessa.compileFile("app.ds");

// Run it here (print goes to stdout)
await dessa.runFile("app.ds");

// A .dhtml page with its Dessa scripts compiled: plain HTML that runs anywhere
const html = await dessa.buildPage("page.dhtml");
```

Errors throw a `DessaError` whose `diagnostics` hold the file, line, column
and message of each problem.

## From the command line

```
npx -p dessalang dessa-web run app.ds       # or install it: npm install -g dessalang, then dessa-web ...
npx -p dessalang dessa-web build app.ds -o app.js          # one self-contained .js file
npx -p dessalang dessa-web build page.dhtml                # page.html
npx -p dessalang dessa-web check page.dhtml
npx -p dessalang dessa-web serve . --port 8080             # .dhtml pages compiled on each request
```

## In the browser

```html
<script src="https://unpkg.com/dessalang"></script>
<script type="text/dessa">
    $("webutils") { document }
    document.body.textContent = "Hello from Dessa";
</script>
<script type="text/dessa" src="app.ds"></script>
```

The scripts are compiled in the page, in order. `print()` goes to the console
and to an element with `id="dessa-output"` if there is one.

## .dhtml pages

A `.dhtml` file is HTML where a plain `<script>` holds Dessa code, so no
`type` is needed. `dessa-web build page.dhtml` turns it into `page.html`, and
the result doesn't need this package to run.

## Libraries

These run in JavaScript: `math`, `time`, `sys`, `webutils` (the page: DOM,
events, `alert`, `prompt`, timers, `fetchText`), and the packages written in
Dessa: `json`, `phl`, `numbers`, `codes`, `colors`, `textfx`. Libraries with a
C++ part (`sql`, `request`, `ashwee`) need the native Dessa compiler.

License: Apache 2.0.
