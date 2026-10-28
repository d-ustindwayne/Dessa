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

**Highlighting** for Dessa files, `.dhtml` pages, Dessa scripts inside
HTML pages, and ```` ```dessa ```` code blocks in Markdown.

**Errors as you type.** The compiler checks the file while you type and marks
the exact spot. In `.dhtml` pages every script is checked, with positions in
the page itself.

**Unused imports** are faded, like in TypeScript. Set `dessa.unusedImports` to
`warning` to see them as warnings, or `off`.

**Completion from the real types.** `customer.` lists the members of
customer's class, inherited ones included. It also works after `is` checks,
inside lambdas, through `?.` and along call chains such as
`win.button("OK").onClick(...)`. Library imports complete too. Only names are
suggested (no keywords, no snippets), and picking a function doesn't add `()`.
`addEventListener("` suggests the event names.

**Hover** shows the type at that spot, the documentation comment, and a
"Defined in" link.

**Go to definition** (F12 or ctrl+click) on variables, functions, classes,
fields, methods, constructors (`new Window`), type names (`var[Window]`),
builtins (`"text".upper`) and library functions.

**Links on imports.** Ctrl+click `$("webutils")` or `$("utils.strings")` to
open the module.

**Parameter hints** while you type a call, with the default values.

**HTML in `.dhtml` pages.** Outside the Dessa scripts the page works like an
`.html` file: Emmet (`!` + Tab for a whole page, `p` + Tab for `<p></p>`),
tag and attribute completion, `<div>` closed for you as `<div>|</div>`, `</`
finished, `class=` given its quotes (`class="|"`), hover with the tag's
description, matching tag highlights, renaming both tags of a pair (with
`editor.linkedEditing`), the outline, folding, and Format Document (the Dessa
scripts are left as they are). Inside the scripts everything is Dessa.

**Run** with Ctrl+F5 or the play button in the editor title. `.ds` files run
as native programs; `.dhtml` pages are served locally (`dessa run page.dhtml`)
and open in the browser. **Ctrl+R** (in the page's editor or its terminal)
saves and reloads the page in the browser; **Ctrl+C** in the terminal stops
the server and closes the page's tab.

**Quick fixes** (the light bulb): remove an unused import, install a missing
package with `riv add`, rewrite old `[1, 2]` lists as `<1, 2>`.

**Typing helpers** (`dessa.autoBrackets`, on by default; `|` is where the
cursor ends up):

| You type | You get |
|---|---|
| `$` at the start of a line | `$(\|)` and the libraries and files |
| `var`, `const`, `func` | `var[\|]` and the types (a lowercase letter next takes the `[]` away) |
| a type picked in `var[\|]` / `func[\|]` | `var[Bul] \|`; in a parameter `x[Bul]\|` |
| `<`, `\|`, `:`, `!` right after that | back inside: `var[Int<>] \|`, `var[Int \| \|]` |
| `init` in a class, `super` in an `init` | `init(\|)`, `super(\|);` |
| `if`, `while`, `for`, `switch`, `catch` + space | `if (\|)` |
| `loop` + space | `loop {` with the cursor in an empty body (Backspace takes it away) |
| `<` where a value starts, `Int<` in a type | `<\|>`, `Int<>` |
| `{` after an import | `$("math") { \| }` |
| `!!` in a type | `func[Dbl !! \|]` |
| Enter inside `$("x")` or `{ PI, E }` | the list over several lines |
| Backspace on `(`, `[`, `{`, `<` | the closing one right after it goes too |

## Commands

| Command | What it does |
|---|---|
| Dessa: Run File | runs the file or page (Ctrl+F5) |
| Dessa: Show Generated C++ | opens the C++ the compiler makes from the file |
| Dessa: Check File | checks the file for errors now |
| Dessa: Reload the Running Page | saves and reloads the page started with Run (Ctrl+R) |

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `dessa.compilerPath` | `dessa` | the `dessa` executable, if it isn't on PATH |
| `dessa.checkOnType` | `true` | check while typing (otherwise on open and save) |
| `dessa.autoBrackets` | `true` | the typing helpers below |
| `dessa.unusedImports` | `dim` | imports the file never uses: `dim` (faded), `warning`, or `off` |
| `dessa.packageFolders` | `[]` | extra folders with riv packages for `$("lib")` imports (like `DESSA_PACKAGES`) |

## More

- [The language](https://github.com/d-ustindwayne/Dessa/blob/main/docs/language.md)
- [Libraries](https://github.com/d-ustindwayne/Dessa/blob/main/docs/libraries.md)
- [Web pages](https://github.com/d-ustindwayne/Dessa/blob/main/docs/web.md)
- [Issues](https://github.com/d-ustindwayne/Dessa/issues)
