# Future plans

Ideas for later, with what each one would really change. Measured on an
11th Gen Intel Core i5-1155G7, Windows 11, g++ 15.3, on 2026-10-19.

## Where the time goes today

| Step | Time |
|---|---|
| Dessa itself: reading, checking and translating a program (`dessa check`, `dessa emit`) | about 0.1 s |
| g++ compiling the C++ for hello-world at -O1 (`dessa run`) | 2.6 s |
| the same, with the runtime precompiled once | 1.3 s |
| linking | 0.5 s |
| `dessa build` (-O2, everything) | about 14 s |

So the Dessa compiler is already fast. Almost all of the waiting is g++
compiling the generated C++, mostly the runtime header (`dessa_runtime.hpp`,
templates included in every program).

## Rewriting the compiler in C

**Not recommended.** The compiler is about 0.1 s of a 3 to 14 s wait, so a
faster compiler wouldn't be noticed. C has no strings, lists or maps of its
own, so the 9,000 lines would grow by several times and gain memory bugs. The
better long-term rewrite is the one already started: the compiler in Dessa
itself (`selfhost/PLAN.md`).

**Done (October 2026):** Dessa now **generates C instead of C++** by default
(runtime/dessa_rt.c, src/codegen/cgen*.cpp), falling back to C++ for what the
C backend doesn't do yet. Still to move to C: unions other than `T | Nil`,
`Obj` and anonymous objects, lambdas that call themselves, a builtin passed as
a function value, `?.` on Str/List/Map, and libraries that call back into
Dessa (ashwee events).

## Making `dessa run` and `dessa build` faster

In order of value for the effort:

1. **Done (0.1.x):** the runtime header is precompiled once and reused, and
   `dessa build` uses -O1 unless `--release` is given. Hello-world went from
   about 14 s to 1.5 s (5 s the very first time, while the runtime is prepared).
2. (part of 1)
3. **Split the runtime**: Obj, Map, text helpers and conversions in separate
   headers, included only when a program uses them (the analyser knows).
4. **clang (done, October 2026):** the C that Dessa makes is compiled with
   clang when it's installed (the Windows installer offers LLVM MinGW).
   Measured on the development PC: clang compiles a little faster than gcc,
   and gcc's programs run about 10% faster (`DESSA_CC=gcc` to use it). The
   C++ fallback and the library bridges stay on g++ (its standard library has
   the time zones Dessa needs).
5. **Link without `-static` while developing** (`dessa run`), static only for
   `dessa build`.

## Making programs faster

The benchmark (`tests/bench/RESULTS.md`, 2026-10-28, best of 3 runs): Dessa
native takes 3.4 s, Java 3.9 s, the same program in C++ 3.9 s; Dessa as
WebAssembly 8.9 s, plain JavaScript 17.6 s, Dessa compiled to JavaScript
27.8 s. (On 2026-10-27 the same PC ran plain JavaScript in 11.5 s and Dessa's
JavaScript in 17.8 s, 31 s before the changes below: Node's times vary more
from day to day on this laptop than native ones.)

Done in October 2026:

- **Objects**: counted with a plain (not atomic) count, and small objects come
  from a pool instead of the system allocator (891 ms became about 50 ms).
- **`%`**: positive numbers take the fast unsigned path.
- **Errors**: the code that builds error messages is kept out of the fast path.
- **JavaScript**: Int variables inside functions are plain numbers while they
  fit, BigInts beyond (the whole benchmark: 13.7 times C++ became 5.9).
- **JavaScript, again**: every Int is a plain number while it fits (fields,
  lists, map keys, parameters too), whole Dbls in an Obj or a union are boxed
  instead; the line for error messages is two plain variables; the runtime's
  helpers are local constants; `sort()` on an Int<> uses a Float64Array; a
  Str's length doesn't copy it (30.8 s became 17.8 s; plain JavaScript 11.5 s).

Still possible:

- **Lists of Bul** (sieve): C++'s `std::vector<bool>` packs 8 values in a byte
  and fits in the cache better; the C runtime uses a byte each.
- **List bounds checks** could be skipped in `for (i = 0; i < list.length(); i++)`
  loops where the compiler can prove `i` is in range.
- **JavaScript, the loop task** (most of the remaining gap): every Int
  operation checks for overflow, plain JavaScript checks nothing. The compiler
  could prove where a value can't overflow (`acc % 1000000007` stays small)
  and leave the check out.
- **JavaScript, lists of Bul** (sieve: 446 ms, plain JavaScript 58 ms with a
  Uint8Array): Bul<> as a typed array touches every list operation.
- **WebAssembly** (done in 0.3.3): pages and `--target wasm` programs run the
  C backend's code in the browser's WebAssembly engine, faster than plain
  JavaScript. See below.

## Bigger language ideas (to decide)

The operators `?=`, `?!`, `|>`, `@` (annotations) and `!!` (what a function
throws), classes usable as values, operator methods (`_add`, `_eq`, ...),
`Obj` improvements and the new import syntax are in the review that goes with
the next round of fixes.

## Dessa in the browser without JavaScript (done in 0.3.3)

Pages written in Dessa now run as **WebAssembly**: the C backend's output is
compiled with clang (from `dessa setup wasm`, a cut-down wasi-sdk) and the
page loads it. `webutils` is Dessa's own copy of the browser's API, not
something compiled to JavaScript: page objects are kept in a table by a fixed
bridge (`runtime/dessa_wasm.js`, `runtime/dessa_web.c`), and Dessa calls them
directly. In the benchmark, Dessa as WebAssembly is faster than plain
JavaScript (`tests/bench/RESULTS.md`).

A browser add-on with its own Dessa engine isn't possible: browsers no longer
let add-ons bring native code (Chrome removed NaCl in favour of WebAssembly),
and visitors without the add-on couldn't run the page.

Still to do:

- What the C backend can't do yet goes to JavaScript instead: unions other
  than `T | Nil`, `Obj`, and libraries with a C++ part (`dcrypto`, time zones).
- The browser compiler (`dessa.js`) makes JavaScript; making WebAssembly in
  the browser would need the C compiler there too (or a backend that writes
  WebAssembly directly, without C).
- With Emscripten instead of wasi-sdk, programs run in Node can't open files.

## A website for Dessa (planned)

A public website for Dessa, so people don't have to start from the GitHub
repository. It would show what Dessa is, with short examples and the
benchmark, and have clear links to:

- **Download**: the latest release for Windows, Linux and macOS, the VS Code
  extension, `npm install dessalang`, and the installation guide.
- **Documentation**: the language, the libraries, riv, pages and the editor.
- **Try it**: a playground in the browser, using `dessa.js`.
- **Contributing**: the overview for new maintainers, the architecture guide,
  `CONTRIBUTING.md`, and the issues where help is wanted.
- **Support**: where to report bugs and ask questions, and how to support the
  project.

Still to decide: where it is hosted (GitHub Pages is the simplest), its
address, and whether the pages are written in Dessa (`.dhtml`) to show it off.
