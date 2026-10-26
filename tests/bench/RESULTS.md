# Benchmark results

Measured 2026-10-27 06:45 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (JS) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,287 | 3,279 | 10,743 | 75,051 | 3,448 | 3,072 | 26,550 |
| fib | 3 | 7 | 57 | 577 | 16 | 10 | 196 |
| sieve | 26 | 54 | 75 | 2,524 | 117 | 71 | 704 |
| strings | 19 | 22 | 126 | 2,712 | 29 | 41 | 205 |
| sorting | 63 | 103 | 423 | 1,116 | 88 | 99 | 956 |
| maps | 171 | 173 | 218 | 854 | 274 | 305 | 1,708 |
| objects | 180 | 23 | 33 | 2,276 | 36 | 42 | 406 |
| mandelbrot | 36 | 40 | 43 | 3,950 | 79 | 59 | 77 |
| **all tasks** | **3,785** | **3,701** | **11,718** | **89,060** | **4,087** | **3,699** | **30,802** |
| times C++ | 1.0x | 1.0x | 3.1x | 23.5x | 1.1x | 1.0x | 8.1x |

Whole run, including starting the program (seconds): C++ 4.0, Java 3.8, JavaScript 11.8, Python 89.1, Dessa 4.2, Dessa (C++) 4.0, Dessa (JS) 30.9.

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
- Dessa: dessa 0.2.8 (native, through C: the default)
- Dessa (C++): dessa 0.2.8 (native, through C++: --cpp)
- Dessa (JS): dessa 0.2.8 compiled to JavaScript, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to JavaScript keeps Int exact to 64 bits: Int variables inside functions are
  plain numbers while they fit (below 2^53) and BigInts beyond; everywhere else Int is a BigInt.
  bench.js only uses JavaScript's own numbers.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
