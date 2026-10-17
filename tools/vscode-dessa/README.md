# Dessa for VS Code

Language support for [Dessa](https://github.com/d-ustindwayne/Dessa), a
statically typed language that compiles to C++ (native programs) and to
JavaScript (the browser and Node).

The extension asks the `dessa` compiler itself for errors, types and
definitions, so what you see in the editor is exactly what the compiler does.

## Requirements

The `dessa` compiler. Get it from the
[releases page](https://github.com/d-ustindwayne/Dessa/releases) or build it
from source (see the
[installation guide](https://github.com/d-ustindwayne/Dessa/blob/main/docs/installation.md)).

If `dessa` isn't on your PATH, set `dessa.compilerPath` to the executable.
Highlighting works without the compiler; everything else needs it.

## Files

| Files | What they are |
|---|---|
| `.ds` | Dessa programs (also `.dx`, `.dz`, `.dsx`, `.dxx`, `.dzx`, `.dds`, `.dss`) |
| `.dm` | Dessa modules |
| `.dhtml` | HTML pages with Dessa scripts, built with `dessa build page.dhtml` |
| `.html` | Dessa scripts in `<script type="text/dessa">` are highlighted too |

## Features

**Highlighting** for Dessa files, `.dhtml` pages, and Dessa scripts inside
HTML pages.

**Errors as you type.** The compiler checks the file while you type and marks
the exact spot. In `.dhtml` pages every script is checked, with positions in
the page itself.

**Unused imports** are faded, like in TypeScript. Set `dessa.unusedImports` to
`warning` to see them as warnings, or `off`.

**Completion from the real types.** `customer.` lists the members of
customer's class, inherited ones included. It also works after `is` checks,
inside lambdas, through `?.` and along call chains such as
`win.button("OK").onClick(...)`. Library imports complete too. Only names are
suggested (no keywords), and picking a function doesn't add `()`. Snippets
are in **Insert Snippet**.

**Hover** shows the type at that spot, the documentation comment, and a
"Defined in" link.

**Go to definition** (F12 or ctrl+click) on variables, functions, classes,
fields, methods, constructors (`new Window`), type names (`var[Window]`),
builtins (`"text".upper`) and library functions.

**Links on imports.** Ctrl+click `$("webutils")` or `$("utils.strings")` to
open the module.

**Parameter hints** while you type a call, with the default values.

**HTML help in `.dhtml` pages.** Outside the Dessa scripts you get what
VS Code gives in `.html` files: tag and attribute completion, hover with the
tag's description, matching tag highlights and folding.

**Run** with Ctrl+F5 or the play button in the editor title. `.ds` files run
as native programs; `.dhtml` pages are served locally (`dessa run page.dhtml`).

**Snippets** for common forms (functions, classes, loops, imports).

## Commands

| Command | What it does |
|---|---|
| Dessa: Run File | runs the file or page (Ctrl+F5) |
| Dessa: Show Generated C++ | opens the C++ the compiler makes from the file |
| Dessa: Check File | checks the file for errors now |

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `dessa.compilerPath` | `dessa` | the `dessa` executable, if it isn't on PATH |
| `dessa.checkOnType` | `true` | check while typing (otherwise on open and save) |
| `dessa.unusedImports` | `dim` | imports the file never uses: `dim` (faded), `warning`, or `off` |
| `dessa.packageFolders` | `[]` | extra folders with riv packages for `$("lib")` imports (like `DESSA_PACKAGES`) |

## More

- [The language](https://github.com/d-ustindwayne/Dessa/blob/main/docs/language.md)
- [Libraries](https://github.com/d-ustindwayne/Dessa/blob/main/docs/libraries.md)
- [Web pages](https://github.com/d-ustindwayne/Dessa/blob/main/docs/web.md)
- [Issues](https://github.com/d-ustindwayne/Dessa/issues)
