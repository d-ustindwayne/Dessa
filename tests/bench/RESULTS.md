# Benchmark results

Measured 2026-10-27 (the code of 0.2.9, before its version number changed) on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (JS) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,219 | 3,207 | 10,612 | 71,374 | 2,998 | 3,094 | 16,629 |
| fib | 2 | 7 | 17 | 342 | 13 | 8 | 36 |
| sieve | 25 | 50 | 58 | 1,405 | 83 | 51 | 446 |
| strings | 20 | 20 | 106 | 1,466 | 19 | 29 | 107 |
| sorting | 60 | 96 | 403 | 551 | 60 | 63 | 143 |
| maps | 160 | 176 | 200 | 347 | 157 | 189 | 293 |
| objects | 185 | 22 | 25 | 873 | 19 | 30 | 84 |
| mandelbrot | 35 | 38 | 39 | 1,489 | 50 | 35 | 49 |
| **all tasks** | **3,706** | **3,616** | **11,460** | **77,847** | **3,399** | **3,499** | **17,787** |
| times C++ | 1.0x | 1.0x | 3.1x | 21.0x | 0.9x | 0.9x | 4.8x |

Whole run, including starting the program (seconds): C++ 3.8, Java 3.7, JavaScript 11.5, Python 77.9, Dessa 3.5, Dessa (C++) 3.8, Dessa (JS) 17.9.

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
- Dessa: dessa 0.2.9 (native, through C: the default)
- Dessa (C++): dessa 0.2.9 (native, through C++: --cpp)
- Dessa (JS): dessa 0.2.9 compiled to JavaScript, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to JavaScript keeps Int exact to 64 bits: an Int is a plain number while it
  fits exactly (up to 2^53 - 1) and a BigInt beyond, and every Int operation is checked.
  bench.js only uses JavaScript's own numbers.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
