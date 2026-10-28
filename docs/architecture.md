# How Dessa works

A technical tour of the repository for people working on Dessa itself. For
the summary, see [overview.md](overview.md); for building, testing and
releasing, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## The pipeline

```
 app.ds ──► loader ──► lexer ──► parser ──► analyser ──┬─► C generator ──────► cc  ──► program
            (imports,                      (types,     │   (cgen*.cpp)        (clang or gcc)
             prelude)                       errors,    │      │ can't? / C fails to compile
                                            generics)  │      ▼
                                                       ├─► C++ generator ────► g++ ──► program
                                                       │   (codegen.cpp)
                                                       └─► JavaScript generator ──► app.js
                                                           (jsgen.cpp)             (Node, browser)
```

Everything up to the analyser is shared by all targets, by the editor
commands, and by the browser compiler. The compiler itself takes about 0.1 s;
the rest of `dessa run` is the C or C++ compiler, which is why its results are
cached.

## The compiler (`src/`)

| Folder | Files | What it does |
|---|---|---|
| `src/main.cpp` | | The command line: `run`, `build`, `check`, `emit`, `serve`, `fix-lists`, and the editor queries `symbols`, `complete`, `hover`. Chooses the backend. |
| `src/common` | `program.hpp` | `Module` (one file: source, AST, resolved imports), `Program` (all modules, dependencies first, the entry file last), `Diagnostic`. |
| `src/lexer` | `lexer.cpp`, `token.hpp` | Text to tokens. Handles template strings (`$"..{x}.."`), triple-quoted strings, number bases, `!!`. |
| `src/parser` | `parser.cpp`, `ast.hpp` | Tokens to an AST (`Stmt` and `Expr` nodes). Decides between `<` as a list and as less-than. Keeps generic declarations as token lists (`Module::templates`) so they can be parsed again for each set of type arguments. |
| `src/driver` | `loader.cpp` | Reads the entry file and every file it imports, and adds the prelude (`include/prelude/errors.ds`: `Error`, `IndexError`, ...) to every program. |
| | `toolchain.cpp` | Finds the C and C++ compilers and the `runtime/` folder, compiles, caches, runs. |
| | `web.cpp` | `.dhtml` and `.html` pages: finds the Dessa scripts, compiles each to JavaScript, `dessa serve`. |
| `src/analyser` | `analyser.cpp` | Modules, declarations, statements; scopes; unused imports. |
| | `analyser_expr.cpp` | Expressions, calls, members, lambdas, builtin methods (`"x".upper()`). |
| | `generics.cpp` | Fills in generic classes and functions (`Box:Int`, `first:Int(...)`), infers type arguments. |
| | `semtype.cpp` | `SemanticType`: the types the checker works with (unions, lists, maps, functions, `Int[16]`). |
| | `scope.cpp` | Symbols and scopes, including narrowing after `!= nil` and `is`. |
| | `builtins.cpp` | The builtin functions and the libraries with a native part, with their signatures. |
| `src/codegen` | `cgen.cpp`, `cgen_expr.cpp` | The C generator (the default). |
| | `cglue.cpp` | The bridge that lets C programs call libraries written in C++. |
| | `codegen.cpp` | The C++ generator (the fallback). |
| | `jsgen.cpp` | The JavaScript generator. |
| `src/web` | `wasm_main.cpp` | The entry point of the browser compiler (`dessa.js`). |

### Loading modules

`$("name")` is resolved in this order: a file `name.ds` (or `.dm`) next to the
importing file or in the project folder; a core library in `include/name/`;
a riv package in `riv_modules/name/` in the project folder or any folder
above; the folders in `DESSA_PACKAGES`. `$("my.file")` means `my/file.ds`.
Modules are ordered so that each one comes after everything it imports, and
only the entry file's top-level code runs.

### The analyser

The analyser checks the whole program and annotates the AST with types. The
generators trust it: they don't report errors, they only translate. Things to
know:

- **Narrowing.** After `if (x != nil)` or `if (x is Dog)`, `x` has the
  narrower type in that branch. Assigning to `x` undoes it.
- **Generics** are filled in on use. `Box:Int` makes a class `Box$1` (shown to
  the user as `Box:Int`) from the token template; the instances are added to
  the program as extra declarations (`Analyser::takeInstances()`).
- **Diagnostics** carry file, line, column and optionally a length and a tag
  (`unused` for imports the file never uses, shown faded in the editor).
- **The editor queries** (`dessa complete`, `dessa hover`) run the analyser
  on the unsaved text with a placeholder at the cursor and report what is in
  scope there.

### The three generators

All three must give the same output for the same program. The differences are
in how values are represented:

| | C (`runtime/dessa_rt.c/.h`) | C++ (`runtime/dessa_runtime.hpp`) | JavaScript (`runtime/dessa_runtime.js`) |
|---|---|---|---|
| `Int` | `int64_t`, overflow checked | `int64_t`, overflow checked | a number while it fits exactly (up to 2^53 - 1), a BigInt beyond; checked to 64 bits |
| `Str` | counted UTF-8, shared until changed | UTF-8 `std::string` | JavaScript string (lengths count characters, not UTF-16 units) |
| lists, maps | counted, copied when changed while shared (value semantics) | the same | arrays and `Map`s, copied where value semantics need it |
| class instances | counted objects, a pool for small ones | `Ref<T>`: intrusive count, a pool for small objects | JavaScript objects |
| errors | `setjmp` / `longjmp` | C++ exceptions | JavaScript exceptions |
| unions, `Obj` | not yet (the program goes to C++) | tagged values | values with their runtime type (whole `Dbl`s are boxed so they don't look like `Int`s) |

**C first, C++ when needed.** `CCodegen::generate` returns the C source, or an
empty string and a reason (`unions`, `Obj`, ...). The driver then uses the C++
generator. If the C source fails to compile, `retryAsCpp` builds the program
from C++ instead, so a gap in the C backend never stops the user.
`DESSA_VERBOSE=1` shows when and why this happens. `--cpp` forces C++,
`--target c` forces C.

**Line numbers for errors.** Every statement records its line, so an error
that nobody caught can say `at app.ds:14`.

## Runtimes and libraries

| Folder | What it holds |
|---|---|
| `runtime/dessa_rt.h`, `dessa_rt.c` | The C runtime. `dessa_rt.c` is compiled once and cached as an object file. |
| `runtime/dessa_runtime.hpp` | The C++ runtime (header only, precompiled once and cached). |
| `runtime/dessa_runtime.js` | The JavaScript runtime, pasted in front of every generated program. |
| `include/<lib>/` | A core library: `<lib>.dm` (written in Dessa), `<lib>.hpp` (C++ part, registered in `builtins.cpp`), `<lib>.js` (JavaScript part), `<lib>.ds` (declarations, for reading and Ctrl+click). |
| `include/prelude/errors.ds` | The error classes every program gets. The runtimes call into it to turn their own errors (index out of range, overflow) into `IndexError`, `OverflowError`, ... |
| `tools/riv/packages/<pkg>/` | Packages that ship with Dessa. A native part is described by a `<pkg>.native` file (header, sources, link flags, signatures). |

Libraries with a C++ part (`sql`, `request`, `dcrypto`, the time zones of
`time`) are reached from C through a small generated C++ file (`cglue.cpp`):
one `extern "C"` function per library function, converting values in and
errors out. It is compiled once per library and cached.

## The toolchain and the cache

`toolchain.cpp` decides which compiler to use (`DESSA_CC`, else a MinGW clang
when there is one, else gcc; `DESSA_CXX` or g++ for C++), compiles, and keeps
the results in the temporary folder (`%TEMP%\dessa` or `/tmp/dessa`), keyed by
a hash of the generated code and of the runtime:

- the C runtime object and the precompiled C++ runtime header,
- each library bridge and each package's native sources,
- each program, so a second `dessa run` of an unchanged program starts at once.

The output of the C or C++ compiler is never shown to the user. When
compilation fails, Dessa writes a log file and prints its path.

## The browser and npm

- **`dessa.js`** is the compiler (lexer to JavaScript generator) built with
  Emscripten (`emcmake cmake -S . -B build-wasm`). It compiles
  `<script type="text/dessa">` in a page, and is what the npm package
  `dessalang` uses (`npm/`).
- **`.dhtml` pages** are HTML with Dessa scripts. `dessa build page.dhtml`
  compiles each script to JavaScript; `dessa serve` does it on each request.
- **`webutils`** is the library for the page (elements, events, timers). It
  only exists in JavaScript (`include/webutils/webutils.js`).

## riv, the package manager (`tools/riv/`)

riv is written in Dessa (`riv.ds`) and built with the compiler. It reads a
project's `riv.pkg`, chooses versions from ranges (`^1.2.0`, `~2.0`, `>=1.0`),
writes `riv.lock` with a hash of each package, and installs into
`riv_modules/`. There is no package server: the index of packages and
versions is a set of JSON files in this repository (`tools/riv/index/`), and
packages travel over BitTorrent (the `torrent` package), from peers on the
internet or the local network, with GitHub as a web seed when nobody is
sharing. `riv update` updates Dessa itself from the latest GitHub release.

## The editor (`tools/vscode-dessa/`)

Plain JavaScript, no build step. Everything language-aware comes from the
compiler, with the unsaved text sent on stdin:

| VS Code feature | Compiler command |
|---|---|
| Errors as you type | `dessa check <file> --json --stdin` |
| Outline, fallback completion, imports | `dessa symbols <file> --stdin` |
| Completion | `dessa complete <file> --stdin --at L:C` |
| Hover, parameter hints | `dessa hover <file> --stdin --at L:C` |

The extension adds what doesn't need the compiler: highlighting (TextMate
grammars in `syntaxes/`), the HTML side of `.dhtml` pages (a bundled copy of
`vscode-html-languageservice`, plus Emmet through `emmet.includeLanguages`),
typing helpers and quick fixes. There are no snippets, on purpose.

## The self-hosted compiler (`selfhost/`)

The compiler is being rewritten in Dessa, one stage at a time, each stage
giving exactly the same output as its C++ twin. The lexer is done
(`selfhost/lexer.dm`); `python selfhost/check.py` compares its tokens with
`dessa tokens` on every Dessa file in the repository, and CI runs it. The plan
is in `selfhost/PLAN.md`.

## Where to start for common changes

| Change | Where |
|---|---|
| A new builtin method (`"x".something()`) | `Analyser::builtinMethod` in `analyser_expr.cpp`, then each runtime and generator, then the editor's `METHODS` list in `extension.js` |
| A new statement or expression | `ast.hpp`, `parser.cpp`, the analyser, all three generators, the selfhost lexer if there is a new token |
| A new core library function | `include/<lib>/` (Dessa or C++ part), `builtins.cpp` for a C++ part, the JavaScript part if it should run in the browser |
| A new error message | the analyser; add a case to `tests/cases/errors/` |
| A runtime behaviour | all three runtimes; a test in `tests/cases/run/` checks they agree |

The documentation for users is in `docs/`; update it with the change.
