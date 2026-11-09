# Benchmark results

Measured 2026-11-09 15:39 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 5,383 | 4,535 | 18,847 | 111,409 | 3,026 | 3,025 | 7,341 |
| fib | 11 | 13 | 28 | 381 | 10 | 14 | 19 |
| sieve | 61 | 102 | 199 | 2,034 | 100 | 93 | 261 |
| strings | 50 | 32 | 262 | 2,079 | 26 | 33 | 70 |
| sorting | 154 | 187 | 1,028 | 836 | 85 | 84 | 100 |
| maps | 583 | 297 | 501 | 749 | 260 | 272 | 420 |
| objects | 616 | 38 | 62 | 1,561 | 24 | 61 | 93 |
| mandelbrot | 75 | 103 | 103 | 2,729 | 78 | 50 | 112 |
| **all tasks** | **6,933** | **5,307** | **21,030** | **121,778** | **3,609** | **3,632** | **8,416** |
| times C++ | 1.0x | 0.8x | 3.0x | 17.6x | 0.5x | 0.5x | 1.2x |

Whole run, including starting the program (seconds): C++ 7.3, Java 5.5, JavaScript 21.2, Python 121.9, Dessa 3.7, Dessa (C++) 4.0, Dessa (wasm) 8.5.

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
- Dessa: dessa 0.6.7 (native, through C: the default)
- Dessa (C++): dessa 0.6.7 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.6.7 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
