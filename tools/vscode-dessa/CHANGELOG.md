# Changelog

## Unreleased

- `var`, `const` or `func` + space also gets `[]` when something follows the cursor: `for (var |)`, or typed in front of a name (`var[|] x = 5`)
- Enter inside `$("dcrypto"|)` puts the list over several lines (`"dcrypto",` on its own line, the cursor on the next) and suggests libraries and files again; Enter after a library on its own line adds the comma
- `toHex`, `toDec`, `toOct`, `toBin` are suggested on Ints

## 0.2.0

- After `$("dcrypto") {` the names the library or file offers are suggested (not the ones already listed)
- Typing `$` at the start of a line adds `()` and opens the list of libraries and files
- A space right after `var`, `const` or `func` becomes `[]` with the cursor inside, for the type (setting `dessa.autoBrackets`)
- `from` is an ordinary name again (no "reserved" error); the import snippet is `importnames` and writes `$("file") { Name }`

## 0.1.5

- No hover or go to definition on words inside strings and comments
- `$` at the start of a line offers an import; inside `$(` the libraries and the project's own files are suggested, with the quotes
- Highlighting for `$("math" as m)`, `?=`, `?!`, `|>`, `!!` and `@override`; signatures show what a function throws

## 0.1.0

- New builtins `fieldsOf`, `getField`, `setField` are known to completion and hover
- The import snippet uses `$("name")`

## 0.0.6

- Unused imports are shown faded (setting `dessa.unusedImports`: dim, warning or off)
- Completion suggests names only: no keywords, and no `()` added after a function
- Imports are written `$("math")`; `$("` suggests the libraries
- `this` and `Obj` are highlighted

## 0.0.5

First release on the Marketplace.

- Highlighting for Dessa files, `.dhtml` pages and Dessa scripts inside HTML
- Errors as you type, completion, hover, go to definition and parameter hints from the compiler
- Links on imports
- HTML help in `.dhtml` pages
- Run, Show Generated C++ and Check commands
