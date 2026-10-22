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

The benchmark (`tests/bench/RESULTS.md`, 2026-10-23): Dessa native takes 4.9 s,
Java 5.2 s, the same program in C++ 5.3 s; Dessa compiled to JavaScript 31 s.

Done in October 2026:

- **Objects**: counted with a plain (not atomic) count, and small objects come
  from a pool instead of the system allocator (891 ms became about 50 ms).
- **`%`**: positive numbers take the fast unsigned path.
- **Errors**: the code that builds error messages is kept out of the fast path.
- **JavaScript**: Int variables inside functions are plain numbers while they
  fit, BigInts beyond (the whole benchmark: 13.7 times C++ became 5.9).

Still possible:

- **Lists of Bul** (sieve): C++'s `std::vector<bool>` packs 8 values in a byte
  and fits in the cache better; the C runtime uses a byte each.
- **List bounds checks** could be skipped in `for (i = 0; i < list.length(); i++)`
  loops where the compiler can prove `i` is in range.
- **JavaScript**: objects and calls still turn small Ints back into BigInts
  when they pass them on.

## Bigger language ideas (to decide)

The operators `?=`, `?!`, `|>`, `@` (annotations) and `!!` (what a function
throws), classes usable as values, operator methods (`_add`, `_eq`, ...),
`Obj` improvements and the new import syntax are in the review that goes with
the next round of fixes.
