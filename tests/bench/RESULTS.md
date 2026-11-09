# Benchmark results

Measured 2026-11-10 10:50 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,287 | 3,345 | 10,866 | 94,478 | 2,934 | 2,933 | 8,111 |
| fib | 4 | 8 | 18 | 437 | 7 | 7 | 35 |
| sieve | 26 | 47 | 61 | 1,776 | 98 | 55 | 281 |
| strings | 18 | 19 | 106 | 2,026 | 23 | 30 | 49 |
| sorting | 60 | 99 | 399 | 651 | 75 | 81 | 189 |
| maps | 164 | 169 | 204 | 614 | 206 | 249 | 446 |
| objects | 188 | 20 | 27 | 1,573 | 29 | 37 | 123 |
| mandelbrot | 36 | 38 | 38 | 3,219 | 88 | 44 | 127 |
| **all tasks** | **3,783** | **3,745** | **11,719** | **104,774** | **3,460** | **3,436** | **9,361** |
| times C++ | 1.0x | 1.0x | 3.1x | 27.7x | 0.9x | 0.9x | 2.5x |

Whole run, including starting the program (seconds): C++ 4.0, Java 3.8, JavaScript 11.8, Python 104.8, Dessa 3.6, Dessa (C++) 3.8, Dessa (wasm) 9.4.

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
- Dessa: dessa 0.7.0 (native, through C: the default)
- Dessa (C++): dessa 0.7.0 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.7.0 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
