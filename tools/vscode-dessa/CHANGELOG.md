# Changelog

## Unreleased

- The code inside `$"... {expr} ..."` is coloured as code in every theme (it took the string's colour, or grey, before)
- `with` (a reserved word) is coloured as a keyword, not greyed out as deprecated

## 0.5.0

- The extension's home page is now https://dessa.dustindwayn.ee/ (Dessa 0.5.0: everything compiles to C and WebAssembly; the JavaScript backend is gone)

## 0.3.6

- No changes to the extension (Dessa 0.3.6: WebAssembly programs in Node can use files)

## 0.3.5

- No changes to the extension (Dessa 0.3.5 fixes `dessa setup wasm` on Linux and macOS)

## 0.3.4

- No changes to the extension (Dessa 0.3.4 fixes `dessa setup wasm` on Linux and macOS)

## 0.3.3

- No changes to the extension (Dessa 0.3.3 runs `.dhtml` pages as WebAssembly)

## 0.3.0

- `init` in a class body becomes `init(|)`, `super` in an `init` becomes `super(|);` (a `.` typed right after gives `super.` instead)
- `loop` + space adds an empty body with the cursor inside; Backspace before anything is typed in it takes it away again
- Picking a type from the suggestions in `var[|]`, `const[|]` or `func[|]` moves the cursor out with a space (`var[Bul] |`); in a parameter's `x[|]` without one (`x[Bul]|`). Typing `<`, `|`, `:` or `!` right after puts it back inside the brackets: `var[Int<>] |`, `var[Int | |]`
- Backspace on an opening bracket (`(`, `[`, `{`, `<`) also removes the closing one right after it, including the ones these helpers add; Backspace in an import's `{ | }` removes both braces
- A `)`, `]` or `>` typed in front of the same one steps over it when the line doesn't need both
- `<` where a value starts (`x = <`, `f(<`, `return <`) becomes `<|>`, and `Int<` in a type becomes `Int<>`
- `Map` and `Func` picked from the type suggestions come with `[]`
- Quick fixes (the light bulb): remove an unused import, install a missing package with `riv add`, write an old `[1, 2]` list as `<1, 2>` (one, or all in the file)
- `.dhtml` pages work like `.html` outside the scripts: Emmet (`!`, `p` + Tab), `<div>` closed as `<div>|</div>`, `</` finished, quotes after `attribute=`, renaming both tags of a pair, the outline, smart selection and Format Document (scripts left as they are)
- `addEventListener("` suggests the event names
- Ctrl+R reloads a page started with Run (from its editor or its terminal), after saving; Run on a page that is already running reloads it
- The snippets are gone: Dessa doesn't do snippets

## 0.2.9

- No changes to the extension (Dessa 0.2.9 makes programs compiled to JavaScript faster)

## 0.2.8

- Inside `var[|]`, `func[|]`, a parameter's `x[|]` (and `Map[Str, |]`, after `|`) only types are suggested: the built-in ones, `Map`, `Func`, and the classes, interfaces and enums in scope; no variables. Typing `[` opens the list
- `if`, `while`, `for`, `switch` or `catch` + space becomes `if (|)`
- `{` after an import becomes `{ | }`: `$("math") { | }`
- `!!` typed in a type gets spaces: `func[Dbl !! |]`
- `!!` is coloured like `throw`
- `$("` suggests riv packages in `riv_modules` (and in `dessa.packageFolders`) before the file imports them, and `$("colors") {` lists their names
- ```` ```dessa ```` code blocks in Markdown are highlighted (also ```` ```ds ````)
- `var`, `const` and `func` at the start of a statement get `[]` as soon as the word is typed (a lowercase letter typed right after takes them away again, for names like `variable`)
- Enter inside `$("math") { PI, | }` puts the names over several lines; Enter after a name there adds the comma
- `:` after a generic's name (`func[T] first:`, `class Box:`, `new Box:`) or in a type (`var[Box:|]`) suggests the types
- Generics are highlighted: `class Box:T`, `func[T] first:T(`, `Box:Int`, `first:Int(`; `Lst` is a type

## 0.2.1

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
