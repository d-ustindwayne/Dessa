# How Dessa works

A technical tour of the repository for people working on Dessa itself. For
the summary, see [overview.md](overview.md); for building, testing and
releasing, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## The pipeline

```
 app.ds ──► loader ──► lexer ──► parser ──► analyser ──┬─► C generator ──────► cc  ──► program
            (imports,                      (types,     │   (cgen*.cpp)        (clang or gcc)
             prelude)                       errors,    │      │ │ can't? / C fails to compile
                                            generics)  │      │ ▼
                                                       │      │ C++ generator ────► g++ ──► program
                                                       │      │ (codegen.cpp)
                                                       │      └► wasm clang ─────────────► app.wasm
                                                       │         (pages, --target wasm)    (browser, Node)
```

Everything up to the analyser is shared by all targets and by the editor
commands. The compiler itself takes about 0.1 s;
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
| | `web.cpp` | `.dhtml` and `.html` pages: finds the Dessa scripts, compiles each to WebAssembly, `dessa serve` (with Ctrl+R reloading). |
| `src/analyser` | `analyser.cpp` | Modules, declarations, statements; scopes; unused imports. |
| | `analyser_expr.cpp` | Expressions, calls, members, lambdas, builtin methods (`"x".upper()`). |
| | `generics.cpp` | Fills in generic classes and functions (`Box:Int`, `first:Int(...)`), infers type arguments. |
| | `semtype.cpp` | `SemanticType`: the types the checker works with (unions, lists, maps, functions, `Int[16]`). |
| | `scope.cpp` | Symbols and scopes, including narrowing after `!= nil` and `is`. |
| | `builtins.cpp` | The builtin functions and the libraries with a native part, with their signatures. |
| `src/codegen` | `cgen.cpp`, `cgen_expr.cpp` | The C generator (the default). |
| | `cglue.cpp` | The bridge that lets C programs call libraries written in C++. |
| | `cgen_web.cpp` | For WebAssembly: webutils' page objects and functions through the page bridge. |
| | `codegen.cpp` | The C++ generator (the fallback). |

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

Both must give the same output for the same program. The differences are
in how values are represented:

| | C, also WebAssembly (`runtime/dessa_rt.c/.h`) | C++ (`runtime/dessa_runtime.hpp`) |
|---|---|---|
| `Int` | `int64_t`, overflow checked | `int64_t`, overflow checked |
| `Str` | counted UTF-8, shared until changed | UTF-8 `std::string` |
| lists, maps | counted, copied when changed while shared (value semantics) | the same |
| class instances | counted objects, a pool for small ones | `Ref<T>`: intrusive count, a pool for small objects |
| errors | `setjmp` / `longjmp` | C++ exceptions |
| unions, `Obj` | a counted `ds_head*`: text, lists, maps and objects as they are, numbers in a small box, nil as NULL | tagged values |

**C first, C++ when needed.** `CCodegen::generate` returns the C source, or an
empty string and a reason. The driver then uses the C++ generator. If the C source fails to compile, `retryAsCpp` builds the program
from C++ instead, so a gap in the C backend never stops the user.
`DESSA_VERBOSE=1` shows when and why this happens. `--cpp` forces C++,
`--target c` forces C.

**WebAssembly is the C backend's output** (`CCodegen::setWasm`), compiled by
a clang for WebAssembly with the WASI C library (`toolchain::compileWasm`).
`try`/`catch` (setjmp) is made of WebAssembly exceptions. Page objects from
`webutils` (`Element`, `Event`, ...) are counted `ds_js` values holding a
number in the bridge's table; `cgen_web.cpp` turns a field read, a field set
and a method call into pushes to the bridge, a call by name, and taking the
result back (`runtime/dessa_web.h`). The bridge (`runtime/dessa_wasm.js`) is
fixed: nothing is generated per program. Event listeners and timers call back
into `dsw_invoke`, which reports an error nobody caught and keeps the page
going.

**Line numbers for errors.** Every statement records its line, so an error
that nobody caught can say `at app.ds:14`.

## Runtimes and libraries

| Folder | What it holds |
|---|---|
| `runtime/dessa_rt.h`, `dessa_rt.c` | The C runtime. `dessa_rt.c` is compiled once and cached as an object file. |
| `runtime/dessa_runtime.hpp` | The C++ runtime (header only, precompiled once and cached). |
| `runtime/dessa_web.h`, `dessa_web.c` | WebAssembly: the C side of the page bridge. |
| `runtime/dessa_wasm.js` | WebAssembly: runs a module in Node (`node dessa_wasm.js app.wasm`) or a page; a small WASI (output, input, clock, random) and the page bridge. |
| `include/<lib>/` | A core library: `<lib>.dm` (written in Dessa), `<lib>.hpp` (C++ part, registered in `builtins.cpp`), `<lib>.c` (the same in C, for C programs and WebAssembly), `<lib>.ds` (declarations, for reading and Ctrl+click). |
| `include/prelude/errors.ds` | The error classes every program gets. The runtimes call into it to turn their own errors (index out of range, overflow) into `IndexError`, `OverflowError`, ... |
| `tools/riv/packages/<pkg>/` | Packages that ship with Dessa. A native part is described by a `<pkg>.native` file (header, sources, its C version, link flags, signatures). |

A library's native functions are called from C as `dsx_<lib>_<name>`
(`cglue.hpp`). When the library has a C version of them (`cImpl`: `math.c`,
`os.c`, `time.c`, `dcrypto.c`, `sql.c`, `request.c`), that file is compiled
once and linked in, natively with gcc and for WebAssembly with clang. Otherwise
(`ashwee`, `torrent`) a small generated C++ file (`cglue.cpp`) does it: one
`extern "C"` function per library function, converting values in and errors
out. It is compiled once per library and cached, and such a library can't run
in WebAssembly. The C file's line 2, `/* dessa-glue: sql time */`, tells the
toolchain which libraries a program calls.

In WebAssembly, the few things C can't do there go to JavaScript through the
same bridge as `webutils` (`dessa_wasm.js`): time zones (`Intl`) and HTTP
requests (a synchronous `XMLHttpRequest` in a page, a short Node process
running `fetch` in Node).

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

- **`.dhtml` pages** are HTML with Dessa scripts. `dessa build page.dhtml`
  compiles each script to WebAssembly, embedded as base64 with the loader
  (`runtime/dessa_wasm.js`), so the page is one file; `dessa serve` does it
  on each request.
- **`webutils`** is the library for the page (elements, events, timers):
  declarations in `include/webutils/webutils.dm` and the page bridge in
  `runtime/dessa_web.c` and `runtime/dessa_wasm.js`.
- **The WebAssembly compiler** comes from `dessa setup wasm` (the installers
  run it): wasi-sdk cut down by `tools/make_wasm_tools.py` to clang, the
  linker and the WASI C library, attached to each release. `toolchain.cpp`
  also finds a wasi-sdk (`WASI_SDK_PATH`) or Emscripten (no files then).
- **The npm package** `dessalang` (`npm/`) is Dessa for Node: on first use it
  downloads the release for the computer's system, runs `dessa setup wasm`,
  and then drives `dessa` to build and run WebAssembly.

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
| A new core library function | `include/<lib>/` (Dessa, or a native part in C++ and C), `builtins.cpp` for a native part |
| A new error message | the analyser; add a case to `tests/cases/errors/` |
| A runtime behaviour | all three runtimes; a test in `tests/cases/run/` checks they agree |

The documentation for users is in `docs/`; update it with the change.
