# Benchmark results

Measured 2026-11-09 14:09 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,402 | 3,294 | 13,439 | 96,392 | 3,000 | 3,154 | 6,490 |
| fib | 7 | 15 | 22 | 396 | 10 | 20 | 14 |
| sieve | 44 | 81 | 66 | 1,759 | 102 | 83 | 231 |
| strings | 23 | 40 | 213 | 1,927 | 45 | 38 | 61 |
| sorting | 79 | 150 | 763 | 739 | 80 | 82 | 110 |
| maps | 242 | 209 | 458 | 667 | 239 | 294 | 381 |
| objects | 245 | 23 | 57 | 1,627 | 26 | 56 | 94 |
| mandelbrot | 41 | 53 | 64 | 2,930 | 87 | 52 | 124 |
| **all tasks** | **4,083** | **3,865** | **15,082** | **106,437** | **3,589** | **3,779** | **7,505** |
| times C++ | 1.0x | 0.9x | 3.7x | 26.1x | 0.9x | 0.9x | 1.8x |

Whole run, including starting the program (seconds): C++ 4.4, Java 4.0, JavaScript 15.2, Python 106.5, Dessa 3.7, Dessa (C++) 4.2, Dessa (wasm) 7.6.

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
- Dessa: dessa 0.6.1 (native, through C: the default)
- Dessa (C++): dessa 0.6.1 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.6.1 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
