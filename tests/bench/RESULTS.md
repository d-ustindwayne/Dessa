# Benchmark results

Measured 2026-10-28 19:54 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster), the best of 3 runs taken in turns. Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (JS) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,271 | 3,244 | 16,110 | 88,507 | 2,920 | 2,942 | 26,096 | 7,889 |
| fib | 4 | 10 | 24 | 311 | 12 | 7 | 42 | 30 |
| sieve | 30 | 82 | 93 | 1,540 | 94 | 48 | 605 | 310 |
| strings | 23 | 28 | 166 | 1,864 | 22 | 30 | 130 | 66 |
| sorting | 78 | 180 | 793 | 628 | 73 | 82 | 192 | 99 |
| maps | 239 | 280 | 340 | 438 | 223 | 214 | 516 | 355 |
| objects | 256 | 29 | 43 | 964 | 22 | 37 | 140 | 71 |
| mandelbrot | 43 | 50 | 62 | 1,879 | 61 | 41 | 91 | 53 |
| **all tasks** | **3,944** | **3,903** | **17,631** | **96,131** | **3,427** | **3,401** | **27,812** | **8,873** |
| times C++ | 1.0x | 1.0x | 4.5x | 24.4x | 0.9x | 0.9x | 7.1x | 2.2x |

Whole run, including starting the program (seconds): C++ 4.0, Java 4.1, JavaScript 17.9, Python 96.2, Dessa 3.5, Dessa (C++) 3.4, Dessa (JS) 28.3, Dessa (wasm) 9.0.

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
- Dessa: dessa 0.3.3 (native, through C: the default)
- Dessa (C++): dessa 0.3.3 (native, through C++: --cpp)
- Dessa (JS): dessa 0.3.3 compiled to JavaScript, Node
- Dessa (wasm): dessa 0.3.3 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to JavaScript keeps Int exact to 64 bits: an Int is a plain number while it
  fits exactly (up to 2^53 - 1) and a BigInt beyond, and every Int operation is checked.
  bench.js only uses JavaScript's own numbers.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (JS), Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
