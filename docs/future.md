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

What *could* help is the opposite direction: **generating C instead of C++**.
C compiles many times faster than template-heavy C++. But the runtime (lists,
maps, text, Obj, classes) would have to be rewritten in C, which is a large
job. Worth it only if compile time is still a problem after the cheaper steps
below.

## Making `dessa run` and `dessa build` faster

In order of value for the effort:

1. **Done (0.1.x):** the runtime header is precompiled once and reused, and
   `dessa build` uses -O1 unless `--release` is given. Hello-world went from
   about 14 s to 1.5 s (5 s the very first time, while the runtime is prepared).
2. (part of 1)
3. **Split the runtime**: Obj, Map, text helpers and conversions in separate
   headers, included only when a program uses them (the analyser knows).
4. **Use a faster C++ compiler when there is one** (clang is usually faster
   than g++ on templates); keep g++ as the default.
5. **Link without `-static` while developing** (`dessa run`), static only for
   `dessa build`.

## Making programs faster

From the benchmark (`tests/bench/RESULTS.md`), Dessa native is 1.8 times C++
overall. Where the difference comes from:

- **Objects** (891 ms against 34 ms in Java): every object is a
  `std::shared_ptr` with an atomic reference count. A non-atomic count (Dessa
  programs are single-threaded) or a small object pool would cut most of it.
- **Lists** (sieve 237 ms against 42 ms): every `list<i>` is bounds-checked.
  Keep the check, but skip it inside `for (i = 0; i < list.length(); i++)`
  loops where the compiler can prove `i` is in range.
- **Int arithmetic**: every `+ - *` checks for overflow. The checks use the
  CPU's overflow flag already; the remaining cost is small and the safety is
  worth it.
- **Compiled to JavaScript** (13.7 times C++): Int is a BigInt so it stays
  exact to 64 bits. A faster path would use plain numbers when the compiler
  can tell a value stays small (loop counters, lengths, indexes).

## Bigger language ideas (to decide)

The operators `?=`, `?!`, `|>`, `@` (annotations) and `!!` (what a function
throws), classes usable as values, operator methods (`_add`, `_eq`, ...),
`Obj` improvements and the new import syntax are in the review that goes with
the next round of fixes.
