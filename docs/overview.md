# Dessa in brief

A one-page summary for people thinking of helping maintain Dessa. The
technical details are in [architecture.md](architecture.md), and how to build,
test and release is in [CONTRIBUTING.md](../CONTRIBUTING.md).

## What it is

Dessa is a statically typed programming language that compiles to **native
programs** (through C, or C++ for the few things C can't do yet) and to
**WebAssembly** (web pages and Node). It aims for C++ speed without C++'s
learning curve: the syntax reads like a scripting language, but every value has
a type that is checked before the program runs.

```dessa
class Item {
    var[Str] name = "";
    var[Dbl] price = 0;
    init(name[Str], price[Dbl]) {
        this.name = name;
        this.price = price;
    }
}

var[Item<>] cart = <new Item("Mouse", 25.99), new Item("Desk", 149.5)>;
var[Dbl] total = 0;
for (var[Item] item in cart) total += item.price;
print($"{cart.length()} items, total {total.toFixed(2)}");
```

## Why it exists

- **Fast without being hard.** Programs run as fast as the same program in C++
  or Java, with no manual memory management, no headers and no build files.
- **One language for the desktop and the browser.** The same source gives the
  same output whether it runs natively or as WebAssembly. Web
  pages written in Dessa (`.dhtml`) run as WebAssembly, faster than
  JavaScript.
- **Safe by default.** Nil safety (`?.`, `??`, narrowing), checked 64-bit
  integer arithmetic, bounds-checked lists, and errors you can catch, including
  runtime ones such as overflow.
- **Friendly tools.** The user never sees a C or C++ compiler error, error
  messages say what to do, and the editor support comes from the compiler
  itself.

## What is there today (version 0.2.9)

| Part | State |
|---|---|
| Language | Types, nil safety, unions, classes, interfaces, enums, generics, lambdas, error classes, modules, operator methods |
| Native backend | C by default (fast to compile), C++ fallback for unions, `Obj` and a few other features |
| WebAssembly | The C backend's output for pages and `--target wasm`; `webutils` calls the page directly through a fixed bridge; the compiler comes from `dessa setup wasm` |
| Browser | `.dhtml` pages, compiled to WebAssembly; every library works there except desktop windows and SQL Server |
| Libraries | Core: `math`, `os`, `sys`, `time` (time zones), `random`, `dsds` (data structures), `dcrypto`, `webutils`. Packages: `json`, `sql` (SQLite), `request` (HTTP), `ashwee` (desktop windows), `torrent`, and smaller ones |
| Package manager | `riv`, written in Dessa: version ranges, lock file, packages shared over BitTorrent with a GitHub web seed |
| Editor | VS Code extension: errors as you type, completion from the real types, hover, go to definition, quick fixes, typing helpers |
| Distribution | Installers for Windows, Linux and macOS; GitHub releases; npm package `dessalang` (Dessa to WebAssembly from Node) |
| Self-hosting | The lexer is rewritten in Dessa and checked against the C++ one in CI; the parser is next |

## Performance

The benchmark in `tests/bench` runs the same eight tasks in each language
(milliseconds, lower is faster, best of 3 runs, 2026-10-28):

| | Total | Compared with C++ |
|---|---:|---:|
| C++ | 3,944 | 1.0x |
| Java | 3,903 | 1.0x |
| **Dessa (native)** | **3,427** | **0.9x** |
| **Dessa (WebAssembly)** | **8,873** | **2.2x** |
| JavaScript | 17,631 | 4.5x |
| Python | 96,131 | 24.4x |

Pages written in Dessa run as WebAssembly: twice as fast as the same program
written in JavaScript.

## Size

| Part | Lines |
|---|---:|
| Compiler (`src/`, C++20) | about 19,800 |
| Runtimes (`runtime/`: C, C++, and the WebAssembly loader) | about 6,300 |
| Core libraries (`include/`, Dessa plus their C and C++ parts) | about 3,400 |
| riv (`tools/riv/riv.ds`, Dessa) | about 1,200 |
| VS Code extension (`tools/vscode-dessa/extension.js`) | about 1,900 |

Tests: 50 programs whose output is compared in every backend, 21 files of
expected compile errors, a browser smoke test, the npm package test, and the
self-hosted lexer check. CI runs on Windows, Linux and macOS for every push.

## Principles every change keeps

1. **Same output everywhere.** A program prints the same thing through C,
   C++ and WebAssembly. A feature isn't done until it works, or gives a clear
   error, in every backend.
2. **No C or C++ compiler errors reach the user.** If generated C fails to
   compile, Dessa rebuilds through C++; if that fails too, it's a Dessa bug
   and the user gets a short message and a log file.
3. **The compiler is the single source of truth.** The editor asks `dessa`
   for errors, completions and types instead of guessing.
4. **Plain-language documentation.** The docs describe what the user sees,
   with short sentences and examples.

## Where help is most useful

| Area | What it involves | Skills |
|---|---|---|
| Self-hosting | The parser in Dessa, matching `dessa ast` output exactly | Dessa |
| WebAssembly | More of the browser's API in `webutils`; `ashwee` and `torrent` in C; a backend writing WebAssembly directly, without clang | C, C++, JavaScript |
| Libraries | New packages, more of the standard library | Dessa |
| riv | Signed packages, a Dessa-version requirement for packages | Dessa, networking |
| Editor | Rename, find references, more quick fixes (needs new compiler queries) | JavaScript, C++ |
| Docs and examples | Tutorials, sample programs | Writing |

## Status and governance

- One maintainer so far; the project is looking for co-maintainers.
- Pre-1.0: the language can still change, though changes come with a
  migration path (for example `dessa fix-lists` for the switch to `<1, 2>` lists).
- License: Apache 2.0.
- `main` is the only long-lived branch. Every release is a tag (`v0.2.9`) that
  CI turns into a GitHub release.
