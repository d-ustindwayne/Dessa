# Benchmark results

Measured 2026-11-02 18:18 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 3,862 | 3,686 | 12,862 | 113,204 | 2,980 | 3,017 | 5,694 |
| fib | 3 | 7 | 20 | 318 | 8 | 8 | 12 |
| sieve | 28 | 61 | 73 | 1,448 | 81 | 60 | 156 |
| strings | 20 | 21 | 117 | 1,958 | 17 | 35 | 37 |
| sorting | 66 | 101 | 513 | 694 | 76 | 68 | 79 |
| maps | 241 | 208 | 258 | 460 | 192 | 233 | 247 |
| objects | 291 | 27 | 29 | 1,067 | 18 | 35 | 64 |
| mandelbrot | 50 | 46 | 46 | 2,270 | 70 | 39 | 76 |
| **all tasks** | **4,561** | **4,157** | **13,918** | **121,419** | **3,442** | **3,495** | **6,365** |
| times C++ | 1.0x | 0.9x | 3.1x | 26.6x | 0.8x | 0.8x | 1.4x |

Whole run, including starting the program (seconds): C++ 5.0, Java 4.3, JavaScript 14.0, Python 121.5, Dessa 3.5, Dessa (C++) 3.8, Dessa (wasm) 6.4.

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
- Dessa: dessa 0.5.6 (native, through C: the default)
- Dessa (C++): dessa 0.5.6 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.5.6 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
