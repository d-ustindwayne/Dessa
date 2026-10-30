# dessalang: Dessa for Node

The [Dessa](https://dessa.dustindwayn.ee/) programming language from Node.
This package compiles Dessa programs and web pages to WebAssembly and runs
them. It drives the real Dessa compiler, and no C compiler is needed.

```
npm install dessalang
```

The first time it's used, it downloads Dessa for your system and the
WebAssembly compiler (about 60 MB, once) into `~/.dessa/npm/<version>`.
Windows x64, Linux x64 and macOS on Apple silicon are supported.

## From the command line

```
npx dessalang run app.ds                 run it as WebAssembly in Node
npx dessalang build app.ds               app.wasm, and app.js to run it: node app.js
npx dessalang build page.dhtml           page.html: one file, its scripts compiled to WebAssembly
npx dessalang run page.dhtml             serve the page and open it in the browser
npx dessalang check app.ds               errors only
npx dessalang serve . --port 8080        a web server; .dhtml pages compiled on each request
npx dessalang setup                      download everything now (otherwise on first use)
```

## In Node

```js
const dessa = require("dessalang");

await dessa.build("app.ds");                  // app.wasm and app.js
const code = await dessa.run("app.ds");       // runs it here; resolves to the exit code
const html = await dessa.buildPage("page.dhtml");
const problems = await dessa.check("app.ds"); // [{ file, line, column, severity, message }]
```

A failed build rejects with a `DessaError` holding the compiler's message.

## Pages

A `.dhtml` file is an HTML page whose plain `<script>` holds Dessa:

```html
<!doctype html>
<html>
<body>
    <button id="hello">Say hello</button>
    <script>
        $("webutils") { document, Element, Event, alert }
        var[Element | Nil] button = document.getElementById("hello");
        if (button != nil) button.addEventListener("click", (e[Event]) := alert("Hello from Dessa!"));
    </script>
</body>
</html>
```

`npx dessalang build page.dhtml` turns it into `page.html`, one file with the
WebAssembly inside that runs in any browser.

## Settings

| Variable | Meaning |
|---|---|
| `DESSALANG_DESSA` | a `dessa` you already have, used instead of downloading one |
| `DESSALANG_HOME` | where to keep the download (default `~/.dessa/npm/<version>`) |

Documentation: https://dessa.dustindwayn.ee/docs/

License: Apache 2.0.
