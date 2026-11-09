# webutils

The web page from Dessa: the browser's own objects, and the utilities most pages end up writing for themselves.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Web pages only (`.dhtml`, or `--target wasm` in a browser). `dessa run` of a plain `.ds` file can't use it.

## Example

```dessa
$("webutils");

must("#save").textContent = "Save";

var[Element] card = h("div", {"class": "card"},
    h("h2", {:}, "Title"),
    h("a", {"href": "/more"}, "Read more"),    // a javascript: link becomes #
    "Some text");
must("#app").appendChild(card);

on(must("#list"), "click", "li", (e[Event]) := log("a row"));

fetchAsync("data.json", (r[FetchResult]) := {
    if (r.ok) log(r.text); else log("failed:", r.error);
});
```

## The browser's objects

Setting a field changes the page right away.

| Part | What it has |
|---|---|
| `document` | `getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `body`, `title`, `addEventListener` |
| `Element` | `textContent`, `innerHTML`, `value`, `checked`, `disabled`, `style`, `classList`, `children`, `appendChild`, `remove`, `addEventListener`, `onclick`, `click`, `focus`, ... |
| `Event` | `type`, `target`, `key`, `clientX`, `clientY`, `preventDefault`, `stopPropagation` |
| `window` | `innerWidth`, `innerHeight`, `location`, `localStorage`, `addEventListener` |
| Dialogs | `alert`, `prompt`, `confirm` |
| Console | `log`, `warn`, `error` |
| Timers | `setTimeout`, `setInterval`, `clearTimer` |
| Drawing | `canvas.getContext("2d")` gives a `Context2D`; the `canvaslib` package is the simpler way |

`webutils.dm` has every class.

## Utilities

| Part | What it has |
|---|---|
| Finding | `must(selector)` (a clear error if missing), `find`, `findAll` |
| Building safely | `h(tag, attributes, children...)`, `safeHtml(template, values...)`, `escapeHtml`, `safeUrl` |
| Events | `on(parent, type, selector, listener)`, `once`, `onWindow`, each giving a `Listener` with `remove()` |
| Less often | `debounce(action, ms)`, `throttle(action, ms)` |
| Network | `fetchText(url)` (waits), `fetchAsync(url, done)`, `postAsync(url, body, done)` |
| Addresses | `new Url(text)`, `encodeUri`, `decodeUri`, `pushState(url)` |
| Forms | `formValues(form)`, `fillForm(form, values)`, `validate` (`isEmail`, `isUrl`, `isNumber`, `isWhole`, `isFilled`, `lengthBetween`) |
| Keeping things | `store`, `sessionStore` (`get`, `set`, `setFor`, `has`, `remove`, `clear`), `cookies` (`get`, `set`, `remove`) |
| Views | `Router`: `add("/users/:id", view)`, `otherwise`, `go`, `start` |
| Values the page follows | `State:T`: `get`, `set`, `update`, `subscribe`, `bindText(element)` |
| Small things | `toast(message)`, `copyText`, `downloadText(name, content)`, `readChosenFile(input, done)`, `mediaMatches(query)`, `nextFrame(action)`, `nowMs()` |

See [docs/web.md](../../docs/web.md) for pages, and [docs/security.md](../../docs/security.md)
for what to keep out of `store`.
