# Dessa: what's done and what's left

Run `python tests/test.py` (native) and `python tests/test.py --js` (JavaScript)
to check that everything still works.

## Done

**Compiler**
- Lexer, parser, analyser, C++ generator, JavaScript generator, runtimes for both
- `dessa run | build | check | emit | serve`, editor commands (`symbols`, `complete`, `hover`)
- Error messages with file, line and column; runtime errors show the line
- C++ compiler errors are never shown: a bug in Dessa gives a short message and a log file
- Native and JavaScript output are identical on every test

**Language**
- Types: `Str Int Bul Flt Dbl Chr Emp Nil`, lists `T<>`, sets `T{}`, `Map[K, V]`, unions, `Int[2|8|16]`
- Variables, constants, functions (hoisted), named arguments, defaults, variadic parameters, spread
- Lambdas (also recursive), `Func[R, [P]]`, capture by value
- Classes, inheritance, interfaces, enums, `is` with narrowing
- `if`, `switch`, `for`, `for-in`, `while`, `loop`; `try` with several `catch` arms, `throw`
- Nil safety: `?.`, `??`, narrowing after `!= nil`
- Checked `Int` arithmetic, Euclidean `%`, `Int / Int` gives a decimal
- Unicode text counted in characters; template strings; multi-line strings
- `typeof`, `zip`, sort with a comparator
- Modules, `.dm` module files, riv packages

**Libraries**
- Core: `math` (with statistics and `Big`), `os`, `sys`, `time` (with `DateTime`, time zones, 11 languages), `webutils`
- Packages: `json`, `phl`, `sql`, `request`, `ashwee`, `numbers`, `codes`, `colors`, `textfx`
- Packages can include C++ through a `.native` file

**Web**
- `.dhtml` pages: `dessa build`, `dessa run`, `dessa serve`
- `dessa.js`: the compiler in WebAssembly, runs `<script type="text/dessa">` in a page
- `webutils`: the page's objects, events, dialogs, timers, `fetchText`
- npm package `dessalang` with a Node API and the `dessa-web` command

**Tools**
- riv: package manager with a simulated peer-to-peer network
- Installers for Windows (wizard), Linux and macOS; prebuilt releases
- VS Code: highlighting (also in HTML and `.dhtml`), errors as you type, completion, hover with links, go to definition, links on imports, parameter hints, HTML help in `.dhtml`
- CI on Windows, Linux and macOS; releases on tags

## Later

- Pipe operator `|>` (the token is reserved)
- `ashwee` on Linux and macOS
- riv: a real network between computers, binary files, version ranges, a lock file
