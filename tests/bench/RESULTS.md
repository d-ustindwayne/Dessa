# Benchmark results

Measured 2026-10-30 21:52 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 4,627 | 4,646 | 17,241 | 131,662 | 3,053 | 3,053 | 6,899 |
| fib | 3 | 16 | 54 | 410 | 15 | 10 | 32 |
| sieve | 33 | 95 | 140 | 1,916 | 120 | 73 | 257 |
| strings | 26 | 36 | 239 | 2,148 | 39 | 48 | 64 |
| sorting | 81 | 166 | 695 | 892 | 108 | 123 | 100 |
| maps | 292 | 288 | 492 | 713 | 301 | 306 | 411 |
| objects | 429 | 43 | 72 | 1,344 | 39 | 43 | 81 |
| mandelbrot | 56 | 69 | 77 | 2,730 | 74 | 42 | 67 |
| **all tasks** | **5,547** | **5,359** | **19,010** | **141,815** | **3,749** | **3,698** | **7,911** |
| times C++ | 1.0x | 1.0x | 3.4x | 25.6x | 0.7x | 0.7x | 1.4x |

Whole run, including starting the program (seconds): C++ 6.0, Java 5.6, JavaScript 19.2, Python 141.9, Dessa 3.9, Dessa (C++) 4.1, Dessa (wasm) 8.1.

## The tasks

- **loop**: 1,000,000,000 steps of acc = (acc * 31 + i) % 1000000007
- **fib**: naive recursive Fibonacci of 32 (about 7 million calls)
- **sieve**: primes up to 10,000,000 (sieve of Eratosthenes)
- **strings**: building one text from the numbers 0 to 999,999
- **sorting**: sorting 1,000,000 random numbers
- **maps**: 1,000,000 inserts and 1,000,000 lookups in a hash map
- **objects**: 3,000,000 steps that each make 2 small objects
- **mandelbrot**: 800 x 600 Mandelbrot set, up to 100 steps per point

## Versions

- C++: g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r1) 15.3.0
- Java: java version "26.0.1" 2026-04-21
- JavaScript: Node v24.15.0
- Python: Python 3.14.4
- Dessa: dessa 0.5.0 (native, through C: the default)
- Dessa (C++): dessa 0.5.0 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.5.0 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
