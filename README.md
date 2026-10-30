# Dessa

Dessa is a statically typed programming language that compiles to C, for
fast native programs, and to WebAssembly, for web pages faster than
JavaScript. The goal is C++ speed without C++'s learning curve.

Website and documentation: https://dessa.dustindwayn.ee/ (try it without installing: https://dessa.dustindwayn.ee/playground/)

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

## Features

- Static types with nil safety (`?.`, `??`, narrowing with `!= nil` and `is`)
- Classes, interfaces, enums, lambdas, default and named arguments
- Errors you can catch, including runtime errors such as overflow and bad indexes
- Unicode text that counts characters, not bytes
- Identical output everywhere: native (through C) and WebAssembly
- Web pages written in Dessa (`.dhtml`), compiled to WebAssembly, with every library
- `riv`, a package manager, and libraries for files, time zones, JSON, SQL, HTTP, desktop windows and more
- VS Code support driven by the compiler: errors, completion, hover, go to definition

## Installing

| System | Command (from the unpacked release or the repository) |
|---|---|
| Windows | `powershell -ExecutionPolicy Bypass -File install.ps1` |
| Linux, macOS | `sh install.sh` |
| Node only (WebAssembly) | `npm install dessalang` |

The installer checks for a C++ compiler, installs `dessa` and `riv`, and puts
them on your PATH. Details for every system: [docs/installation.md](docs/installation.md).

## Quick start

```
dessa run hello.ds                 compile and run
dessa build hello.ds               make hello.exe (or hello)
dessa check hello.ds               errors only
dessa run page.dhtml               a web page written in Dessa, opened in the browser

riv init myapp                     a new project
riv add sql                        add a package
riv run
```

## Documentation

| Page | Contents |
|---|---|
| [docs/installation.md](docs/installation.md) | Installing on Windows, Linux and macOS |
| [docs/language.md](docs/language.md) | The language: types, functions, classes, errors, modules |
| [docs/compiler.md](docs/compiler.md) | The `dessa` command, targets, settings |
| [docs/riv.md](docs/riv.md) | Packages: using, making and publishing them |
| [docs/libraries.md](docs/libraries.md) | The libraries and what they contain |
| [docs/web.md](docs/web.md) | `.dhtml` pages, WebAssembly, `webutils`, npm |
| [docs/overview.md](docs/overview.md) | Dessa in brief: goals, state, numbers, where help is needed |
| [docs/architecture.md](docs/architecture.md) | How the compiler, runtimes, riv and the editor work |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building, testing and releasing Dessa |

## Repository layout

| Folder | What is in it |
|---|---|
| `src/` | the compiler: lexer, parser, analyser, C and C++ generators |
| `runtime/` | the support code every program uses (C, C++, and the WebAssembly loader) |
| `include/` | the core libraries (`math`, `os`, `sys`, `time`, `random`, `dsds`, `dcrypto`, `webutils`) |
| `tools/riv/` | riv and the bundled packages |
| `tools/vscode-dessa/` | the VS Code extension |
| `web/` | an example page written in Dessa |
| `npm/` | the npm package: Dessa to WebAssembly from Node |
| `website/` | the website, https://dessa.dustindwayn.ee/ (Angular) |
| `tests/` | the test suite |
| `selfhost/` | the compiler being rewritten in Dessa (the lexer is done; see `selfhost/PLAN.md`) |

## Working on Dessa

```
cmake -S . -B build
cmake --build build
python tests/test.py               the test suite (native)
python tests/test.py --wasm        the same tests through WebAssembly
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the rest.

## License

Apache 2.0. See [LICENSE](LICENSE).
