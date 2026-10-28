# Editor support (VS Code)

The Dessa extension (`tools/vscode-dessa`) gets its information from the
compiler itself, so what it shows always matches what `dessa` does.

## What it does

- **Highlighting** for Dessa files, `.dhtml` pages, and Dessa scripts inside
  HTML (`<script type="text/dessa">` or `type="text/ds"`).
- **Errors as you type**, with the exact place. In pages too.
- **Unused imports** are shown faded, like in TypeScript (see `dessa.unusedImports`).
- **Completion** from the real types: `customer.` lists the members of
  customer's class, including inherited ones, and works after `is` checks, in
  lambdas, through `?.` and along call chains (`win.button("OK").onClick(...)`).
  It suggests names only (variables, functions, classes, members): no
  keywords, no snippets, and no `()` added after a function.
  `addEventListener("` suggests the event names.
- **Hover** shows the type at that spot, the documentation comment, and a
  "Defined in" link.
- **Go to definition** (F12 or ctrl+click) on variables, functions, classes,
  fields, methods (`win.label`), constructors (`new Window`), type names
  (`var[Window]`), builtins (`"text".upper`) and library functions.
- **Ctrl+click on imports**: `$("webutils")` or `$("utils.strings")` opens
  the module.
- **Parameter hints** while you type a call, with default values.
- **HTML in `.dhtml` pages**: outside the Dessa scripts the page works like
  an `.html` file: Emmet (`!`, `p` + Tab), tag and attribute completion,
  closing tags added for you, quotes after `=`, hover, matching tags, the
  outline, folding and Format Document. Inside the scripts it's all Dessa.
- **Run** (Ctrl+F5 or the play button) for `.ds` files and `.dhtml` pages.
  For a page, **Ctrl+R** saves and reloads it in the browser, and **Ctrl+C**
  in its terminal stops it and closes its tab.
- **Quick fixes**: remove an unused import, install a missing package with
  `riv add`, rewrite old `[1, 2]` lists as `<1, 2>`.
- **Typing helpers** (setting `dessa.autoBrackets`): `$` becomes `$(|)`;
  `var`, `const` and `func` get `[|]`, and picking a type there moves the
  cursor out (`var[Bul] |`, or `x[Bul]|` for a parameter); `init` and `super`
  get `()`; a space after `if`, `while`, `for`, `switch` or `catch` gives
  `if (|)`, after `loop` an empty body; `<` where a value starts gives `<|>`;
  Backspace on an opening bracket also removes its closing one. The full list
  is in the extension's README.
- Commands: **Dessa: Run File**, **Dessa: Show Generated C++**, **Dessa: Check File**,
  **Dessa: Reload the Running Page**.

PHL is a separate project with its own VS Code extension.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `dessa.compilerPath` | `dessa` | the `dessa` executable (set it if `dessa` isn't on PATH) |
| `dessa.checkOnType` | `true` | check for errors while typing (otherwise on open and save) |
| `dessa.unusedImports` | `dim` | imports the file never uses: `dim` (faded), `warning`, or `off` |
| `dessa.packageFolders` | `[]` | extra folders with packages (like `DESSA_PACKAGES`) |
| `dessa.autoBrackets` | `true` | the typing helpers above |

## Installing

Search for "Dessa" in VS Code's Extensions view (publisher d-ustindwayne), or
download the `.vsix` from a GitHub release and use "Install from VSIX...".

The installers can also install the extension. To do it by hand, copy (or link)
`tools/vscode-dessa` to your extensions folder as `dessa-0.1.0`, then restart
VS Code:

- Windows: `%USERPROFILE%\.vscode\extensions\`
- Linux, macOS: `~/.vscode/extensions/`

To make a `.vsix` file instead, run `npx @vscode/vsce package` in that folder.
