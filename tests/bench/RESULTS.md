# Benchmark results

Measured 2026-11-09 13:22 on 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz, Windows 11.

Times are milliseconds for each task alone (lower is faster). Every language ran the
same algorithm and gave the same result.

| Task | C++ | Java | JavaScript | Python | Dessa | Dessa (C++) | Dessa (wasm) |
|---|---:|---:|---:|---:|---:|---:|---:|
| loop | 4,302 | 3,557 | 14,460 | 98,219 | 3,010 | 3,090 | 6,503 |
| fib | 3 | 8 | 24 | 374 | 10 | 13 | 22 |
| sieve | 38 | 74 | 111 | 1,729 | 135 | 87 | 216 |
| strings | 31 | 26 | 151 | 1,896 | 37 | 44 | 52 |
| sorting | 103 | 138 | 672 | 712 | 101 | 114 | 115 |
| maps | 283 | 215 | 363 | 619 | 264 | 299 | 344 |
| objects | 341 | 34 | 49 | 1,282 | 39 | 45 | 70 |
| mandelbrot | 64 | 84 | 57 | 2,771 | 63 | 46 | 96 |
| **all tasks** | **5,165** | **4,136** | **15,887** | **107,602** | **3,659** | **3,738** | **7,418** |
| times C++ | 1.0x | 0.8x | 3.1x | 20.8x | 0.7x | 0.7x | 1.4x |

Whole run, including starting the program (seconds): C++ 6.3, Java 4.3, JavaScript 16.0, Python 107.7, Dessa 3.7, Dessa (C++) 4.0, Dessa (wasm) 7.5.

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
- Dessa: dessa 0.6.0 (native, through C: the default)
- Dessa (C++): dessa 0.6.0 (native, through C++: --cpp)
- Dessa (wasm): dessa 0.6.0 compiled to WebAssembly, Node

## Notes

- C++ is compiled with g++ -O2; Dessa with `dessa build --release` (-O2), through C (gcc)
  by default and through C++ with --cpp. Dessa checks every Int operation for overflow and
  every list index, which the C++ version doesn't.
- Dessa compiled to WebAssembly (`--target wasm`, what pages use) runs in Node's WebAssembly
  engine, the same one Chrome and Edge use for pages, with the same checks as native Dessa.
- The columns run with Node (JavaScript, Dessa (wasm)) vary more from day to day
  on a laptop than the native ones; compare them within one run.
- Java's times include the JIT warming up; Python runs in the standard interpreter.
