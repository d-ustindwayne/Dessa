# Dessa on the web

Dessa programs can run in a web page. There are three ways to do it.

## 1. .dhtml pages (recommended)

A `.dhtml` file is a normal HTML page in which a `<script>` without a type
holds Dessa code:

```html
<!doctype html>
<html>
<body>
    <button id="hello">Say hello</button>
    <script>
        { document, Element, Event, alert } from $(<webutils>);
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
dessa build page.dhtml      writes page.html with the scripts compiled to JavaScript
dessa serve .               a server for the whole folder; .dhtml pages are compiled on each request
```

The built `page.html` is plain HTML and JavaScript. It runs anywhere (even
opened straight from the disk) and doesn't need Dessa. Error messages point at
the line and column in the `.dhtml` file. Imports (`$("utils")`) are relative
to the page.

## 2. The browser compiler (dessa.js)

`dessa.js` is the Dessa compiler built for WebAssembly, with a loader. Like
Babel's standalone build, it compiles `.ds` files in the browser:

```html
<script src="dessa.js"></script>                          <!-- or https://unpkg.com/dessa -->
<script type="text/dessa" src="app.ds"></script>
<script type="text/dessa">
    { document } from $(<webutils>);
    document.body.textContent = "Hello from Dessa!";
</script>
```

`type="text/ds"` works too. The scripts run in page order, and each one is its
own program. `print()` writes to the browser console, and also into an element
with `id="dessa-output"` if the page has one. Compile errors show up in the
same places.

The page has to be served over HTTP, because browsers block loading files
from `file://` pages: use `dessa serve .`, then open
`http://localhost:8080/`. The example in `web/example/` works this way.

## 3. JavaScript files

```
dessa build app.ds --target web -o app.js     one self-contained .js file (the runtime is inside)
dessa run app.ds --target js                  run with Node
```

## webutils

`$(<webutils>)` gives Dessa the page. Its classes are the browser's own
objects, so setting a field changes the page right away:

```dessa
{ document, window, Element, Event, alert, prompt, confirm, log, setTimeout, fetchText } from $(<webutils>);

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

`math`, `time` (with time zones), `sys`, `webutils`, and the packages written
in Dessa (`json`, `phl`, `numbers`, `codes`, `colors`, `textfx`) work in the
browser. The file functions of `os` work in Node only. Packages with C++ code
(`sql`, `request`, `ashwee`) can't run in a browser, and the compiler says so.

## npm

The `dessa` package on npm has the browser compiler and a Node API:

```
npm install dessa
```

```js
const dessa = require("dessa");
const { js, diagnostics } = await dessa.compile('print("hi");');
await dessa.runFile("app.ds");
const html = await dessa.buildPage("page.dhtml");
```

It also has a command, `dessa-web` (`run`, `build`, `check`, `serve`), for
machines without the native compiler. See `npm/README.md`.

## Building dessa.js

With the [Emscripten SDK](https://emscripten.org):

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm            # writes web/dessa.js (about 1.2 MB, one file)
node tests/web/node_smoke.js        # quick check
```

Releases include a ready-made `dessa.js`.
