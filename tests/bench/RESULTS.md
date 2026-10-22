# Benchmark results

Measured 2026-10-23 11:33 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (JS) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 4,551 | 4,602 | 16,069 | 113,220 | 4,346 | 4,678 | 27,569 |
| fib | 4 | 15 | 23 | 386 | 20 | 10 | 129 |
| sieve | 35 | 72 | 86 | 1,883 | 104 | 62 | 640 |
| strings | 25 | 31 | 167 | 2,076 | 38 | 36 | 287 |
| sorting | 100 | 158 | 646 | 788 | 86 | 96 | 838 |
| maps | 261 | 233 | 331 | 420 | 218 | 279 | 1,441 |
| objects | 295 | 25 | 81 | 1,221 | 50 | 49 | 379 |
| mandelbrot | 48 | 49 | 76 | 2,024 | 73 | 50 | 71 |
| **all tasks** | **5,319** | **5,185** | **17,479** | **122,018** | **4,935** | **5,260** | **31,354** |
| times C++ | 1.0x | 1.0x | 3.3x | 22.9x | 0.9x | 1.0x | 5.9x |

Whole run, including starting the program (seconds): C++ 5.6, Java 5.4, JavaScript 17.6, Python 122.1, Dessa 5.1, Dessa (C++) 6.0, Dessa (JS) 31.5.

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
- Dessa: dessa 0.1.5 (native, through C: the default)
- Dessa (C++): dessa 0.1.5 (native, through C++: --cpp)
- Dessa (JS): dessa 0.1.5 compiled to JavaScript, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to JavaScript keeps Int exact to 64 bits: Int variables inside functions are
  plain numbers while they fit (below 2^53) and BigInts beyond; everywhere else Int is a BigInt.
  bench.js only uses JavaScript's own numbers.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
