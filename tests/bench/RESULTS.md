# Benchmark results

Measured 2026-10-18 22:35 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (JS) |
|---|---:|---:|---:|---:|---:|---:|
| loop | 3,376 | 3,431 | 13,778 | 149,345 | 5,845 | 53,320 |
| fib | 4 | 15 | 32 | 667 | 25 | 177 |
| sieve | 42 | 65 | 75 | 2,889 | 237 | 1,981 |
| strings | 35 | 31 | 149 | 2,791 | 56 | 306 |
| sorting | 75 | 136 | 687 | 1,044 | 120 | 922 |
| maps | 275 | 245 | 383 | 709 | 391 | 1,640 |
| objects | 412 | 34 | 49 | 1,503 | 891 | 210 |
| mandelbrot | 62 | 89 | 49 | 3,053 | 92 | 215 |
| **all tasks** | **4,281** | **4,046** | **15,202** | **162,001** | **7,657** | **58,771** |
| times C++ | 1.0x | 0.9x | 3.6x | 37.8x | 1.8x | 13.7x |

Whole run, including starting the program (seconds): C++ 4.4, Java 4.2, JavaScript 15.3, Python 162.1, Dessa 8.1, Dessa (JS) 59.0.

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
- Dessa: dessa 0.0.6 (native, through g++)
- Dessa (JS): dessa 0.0.6 compiled to JavaScript, Node

## Notes

- C++ and Dessa are compiled with g++ -O2. Dessa checks every Int operation for overflow
  and every list index, which C++ doesn't; that is most of the difference between them.
- Dessa compiled to JavaScript keeps Int exact to 64 bits with BigInt, which is slower
  than JavaScript's own numbers (bench.js only uses numbers below 2^53).
- Java's times include the JIT warming up; Python runs in the standard interpreter.
