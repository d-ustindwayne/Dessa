# Dessa on the web

Dessa runs in web pages as WebAssembly, the browser's engine for
native-speed code. There are two ways to use it.

## 1. .dhtml pages (recommended)

A `.dhtml` file is a normal HTML page in which a `<script>` without a type
holds Dessa code:

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
    <script type="text/javascript">
        console.log("scripts with a type stay JavaScript");
    </script>
</body>
</html>
```

```
dessa run page.dhtml        serves the folder and opens the page in your browser
                            (Ctrl+R reloads the page, Ctrl+C stops and closes it)
dessa build page.dhtml      writes page.html with the scripts compiled
dessa serve .               a server for the whole folder; .dhtml pages are compiled on each request
```

The scripts are compiled to **WebAssembly**, the browser's engine for
native-speed code: no JavaScript is made from your Dessa. They run faster
than the same program written in JavaScript (see the benchmark in
`tests/bench/RESULTS.md`). With `webutils`, `document`, elements, events and
timers are the browser's own, called straight from Dessa.

WebAssembly needs a compiler for it, downloaded once:

```
dessa setup wasm            about 50 MB, into Dessa's folder
```

The installers get it for you. If it's missing, `dessa` says so and how to
get it.

The built `page.html` is one file with everything inside (the WebAssembly too).
It runs anywhere, even opened straight from the disk, and doesn't need Dessa.
Error messages point at the line and column in the `.dhtml` file. Imports
(`$("utils")`) are relative to the page.

## 2. WebAssembly files

A program (not a page) compiled to WebAssembly runs with Node, and in any
other WebAssembly engine:

```
dessa build app.ds --target wasm              app.wasm, and app.js to run it: node app.js
dessa run app.ds --target wasm                run it with Node
```

## webutils

`$("webutils")` gives Dessa the page. Its classes are the browser's own
objects, so setting a field changes the page right away:

```dessa
$("webutils") { document, window, Element, Event, alert, prompt, confirm, log, setTimeout, fetchText }

var[Element] item = document.createElement("li");
item.textContent = "New item";
item.classList.add("fresh");
item.style.color = "rebeccapurple";
document.querySelector("ul")?.appendChild(item);

var[Str | Nil] name = prompt("Your name?", "Ann");      // nil when cancelled
if (name != nil && confirm($"Save {name}?")) {
    window.localStorage.setItem("name", name);
}
setTimeout(() := log("one second later"), 1000);
var[Str] text = fetchText("data.json");                  // waits for the answer
```

| Part | What it has |
|---|---|
| `document` | `getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `body`, `title`, `addEventListener` |
| `Element` | `textContent`, `innerHTML`, `value`, `checked`, `disabled`, `style`, `classList`, `children`, `appendChild`, `remove`, attributes, `addEventListener`, `onclick`, `click`, `focus`, ... |
| `Event` | `type`, `target`, `key`, `clientX`, `clientY`, `preventDefault`, `stopPropagation` |
| `window` | `innerWidth`, `innerHeight`, `location`, `localStorage`, `addEventListener` |
| Dialogs | `alert`, `prompt`, `confirm` |
| Console | `log`, `warn`, `error` |
| Timers | `setTimeout`, `setInterval`, `clearTimer` |
| Network | `fetchText(url)` |

An error inside an event listener or a timer is reported like any other Dessa
error, with its line.

## Libraries in the browser

Every library works in WebAssembly: `math` (with `Big`), `random`, `sys`,
`time` (time zones come from the browser), `dcrypto`, `webutils`, and the
packages `json`, `sql` (SQLite, in memory or in a file in Node), `request`,
`numbers`, `codes`, `colors` and `textfx`. A few things depend on where the
WebAssembly runs:

- The file functions of `os` and SQLite database files work in Node; a page
  has no files (an in-memory database, `openSqlite(":memory:")`, works).
- `request` in a page is an ordinary browser request, so another site
  answers only if it allows pages from elsewhere (CORS).
- SQL Server (ODBC) and `ashwee` (desktop windows) work only natively, and
  the compiler says so.

## npm

The `dessalang` package on npm is Dessa for Node: it compiles programs and
pages to WebAssembly and runs them, without a C compiler.

```
npm install dessalang
npx dessalang build page.dhtml
npx dessalang run app.ds
```

```js
const dessa = require("dessalang");
await dessa.build("app.ds");                 // app.wasm and app.js
const html = await dessa.buildPage("page.dhtml");
```

The first time, it downloads Dessa for your system and the WebAssembly
compiler (once). See `npm/README.md`.
