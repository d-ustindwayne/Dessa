# Working on Dessa

How to build Dessa, test a change, and make a release. What Dessa is:
[docs/overview.md](docs/overview.md). How it works inside:
[docs/architecture.md](docs/architecture.md).

## What you need

| Tool | For |
|---|---|
| CMake 3.20+ and a C++20 compiler (g++ 13+, MinGW-w64 on Windows, clang on macOS) | building the compiler, and compiling programs |
| Python 3 | the test suite and the release scripts |
| Node.js 20.16+ | the JavaScript tests |
| Optional: clang (LLVM MinGW on Windows) | the C backend uses it when present (`DESSA_CC` chooses) |
| Optional: the Emscripten SDK | building the browser compiler `web/dessa.js` |

## Building

```
cmake -S . -B build
cmake --build build                 # build/dessa (dessa.exe on Windows)
cmake --build build --target riv    # build/riv, compiled from tools/riv/riv.ds by dessa
```

Release builds: `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`.

The browser compiler:

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm            # writes web/dessa.js
```

To use the built compiler in VS Code, set `dessa.compilerPath` to
`build/dessa.exe` (or `build/dessa`); an older `dessa` on PATH gives
confusing errors.

## Testing

| Command | What it checks |
|---|---|
| `python tests/test.py` | every test, native (C first, C++ when needed) |
| `python tests/test.py --c` | only through C (programs C can't do yet are skipped) |
| `python tests/test.py --cpp` | only through C++ |
| `python tests/test.py --js` | the run tests through the JavaScript backend, with Node |
| `python tests/test.py --wasm` | the run tests through WebAssembly, with Node (needs `dessa setup wasm` or wasi-sdk) |
| `node tests/web/wasm_page.js` | a page compiled to WebAssembly: page objects, events, errors |
| `python tests/test.py <filter>` | only tests whose name contains the filter |
| `python tests/test.py --update` | rewrite the expected `.out` files (check the diff!) |
| `node tests/web/node_smoke.js` | `web/dessa.js` compiles and runs programs (needs the wasm build) |
| `cd npm && node scripts/copy-build.js && node test.js` | the npm package |
| `python selfhost/check.py` | the Dessa lexer gives the same tokens as the C++ one |
| `python tests/bench/run_bench.py` | the benchmark (about 4 minutes; writes `tests/bench/RESULTS.md`) |

There are two kinds of tests in `tests/cases`:

- **Run tests** (`tests/cases/run/x.ds` with `x.out`): the program's output
  must equal the `.out` file, in every backend.
- **Error tests** (`tests/cases/errors/x.ds`): each line that should give an
  error or a warning ends with a comment saying so, and `dessa check` must
  report exactly those, on those lines, each message containing the text:

  ```dessa
  var[Int] x = "text";      // error: Cannot convert Str to Int
  ```

A run test whose first line is `// exit: N` expects exit code N instead of 0.

A change is ready when the default, `--c`, `--cpp`, `--js` and `--wasm` runs all pass.
CI runs the default and `--js` suites on Windows, Linux and macOS, the
selfhost check, and the browser build, on every push and pull request.

## Making a change

1. **Talk first about language changes.** New syntax or a change in meaning
   goes in an issue before code: the language is small on purpose.
2. **Keep the backends in step.** A feature works in C, C++ and JavaScript
   with the same output, or gives a clear compile error where it can't
   (libraries with a C++ part don't run in the browser, for example). If the C
   backend can't do it yet, `CCodegen` returns a reason and the program goes
   through C++.
3. **Add tests**: a run test for behaviour, an error test for each new error.
4. **Update the docs** in `docs/` (and the extension's README and CHANGELOG
   for editor changes). The docs describe what the user sees, in short, plain
   sentences with an example.
5. **Error messages** say what is wrong and, when there is one, what to do:
   `Unknown library 'sql'; if it's a riv package, install it with: riv add sql`.

### Rules that don't bend

- The user never sees an error from the C or C++ compiler. Generated code that
  doesn't compile is a Dessa bug.
- `Int` is 64 bits and checked in every backend. `%` is Euclidean (never
  negative for a positive divisor); `Int / Int` gives a `Dbl`.
- Text is counted in characters, not bytes.
- `tools/riv/packages/phl` is frozen while its syntax is being revised.

### Code style

- C++20, four spaces, braces on the same line, `camelCase` functions,
  `PascalCase` types. Match the file you are in.
- Comments explain why, in plain sentences, and show the Dessa being handled
  when that helps (`// $("math") { PI }: ...`).
- No new dependencies without discussion. The compiler uses only the
  standard library.
- The VS Code extension is plain JavaScript with no build step and no npm
  dependencies (the HTML service is a single bundled file).

### Commits and branches

- `main` is the only long-lived branch. Work on a branch and open a pull
  request; CI must pass.
- One subject per commit, with a message that says what changed for the
  user: `Lists print as <1, 2>`, not `fix printer`.

## Releasing

Releases are made by a maintainer.

1. Choose the version (`0.3.6`, or `0.4.0` for language changes) and change
   it everywhere it appears:

   | File | Where |
   |---|---|
   | `src/main.cpp` | `kVersion` |
   | `src/web/wasm_main.cpp` | the version string |
   | `tools/make_release.py`, `.github/workflows/release.yml` | the examples in the comments |
   | `npm/package.json`, `npm/test.js` | `version` and the expected version |
   | `tools/vscode-dessa/package.json` | `version` |
   | `tools/vscode-dessa/CHANGELOG.md` | the `Unreleased` heading becomes the version |

   `git grep -n "0\.3\.5"` (the old version) finds them all
   (`tests/bench/RESULTS.md` changes with the benchmark instead).
2. Run every test suite, and the benchmark; commit the new
   `tests/bench/RESULTS.md` and `results.csv`.
3. Tag and push: `git tag v0.3.6 && git push origin main v0.3.6`.
4. The Release workflow builds the Windows, Linux and macOS archives,
   `dessa.js`, the VS Code extension and the WebAssembly compiler downloads
   (`dessa-wasm-<platform>.tar.gz`, made by `tools/make_wasm_tools.py`; `dessa
   setup wasm` takes them from the release of its own version), and makes the
   GitHub release.
5. Publishing to npm (`cd npm && npm publish --access public`) and uploading
   the `.vsix` to the VS Code Marketplace are done by hand by the maintainer
   (npm asks for a one-time password, so the workflow's npm step fails).

Users of `riv` get the new version with `riv update`.

## Packages in the official index

Bundled packages live in `tools/riv/packages/<name>/` with a `riv.pkg`. When
a package changes, give it a new version in `riv.pkg` and regenerate the
index:

```
build/riv index tools/riv/packages --out tools/riv/index --webseed https://raw.githubusercontent.com/d-ustindwayne/Dessa/main/tools/riv/packages/
build/riv index tools/riv/packages --out tools/riv/index --check    # packages changed without a new entry
```

See [docs/riv.md](docs/riv.md) for the package format.
