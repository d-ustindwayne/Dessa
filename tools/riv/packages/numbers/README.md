# numbers

Numbers beyond what `Int` and `Dbl` can do: exact decimals and fractions, complex numbers, NumPy-style arrays, statistics, primes, and numbers as words.

**Install:** Install with `riv add numbers`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("numbers") { Decimal, Fraction, NDArray, array, arange, words, roman }

print(new Decimal("0.1") + new Decimal("0.2"));    // 0.3
print(new Fraction(1, 3) + new Fraction(1, 6));    // 1/2

var[NDArray] a = array(<1.0, 2.0, 3.0>);
print(a * 2);                                      // <2, 4, 6>
var[NDArray] m = arange(0, 6).reshape(<2, 3>);
print(m.sumAlong(0));                              // <3, 5, 7>

print(words.of(78056), roman.of(1994));
```

## Exact numbers

`Decimal` is for money and anywhere `0.1` must be exactly `0.1`. `Fraction` keeps
fractions as fractions. Both have the operators `+ - * / == <` and any size.

`Complex` has `+ - * /`, `magnitude`, `angle`, `conjugate`, `pow` and `squareRoot`.

## NDArray

An array of `Dbl` with any number of dimensions. Operators work element by element, and
shapes are matched by NumPy's rule ("broadcasting").

| Part | What it has |
|---|---|
| Making | `array`, `array2d`, `zeros`, `ones`, `full`, `eye`, `arange`, `linspace`, `concat` |
| Shape | `shape`, `ndim`, `size`, `reshape` (one size may be -1), `flatten`, `transpose`, `row`, `column`, `at`, `set`, `item`, `toList` |
| Element by element | `+ - * / ^`, `absolute`, `squareRoot`, `exponential`, `logarithm`, `sine`, `cosine`, `clip`, `rounded`, `map` |
| Summing up | `sum`, `mean`, `std`, `variance`, `min`, `max`, `median`, `product`, each also `...Along(axis)`; `percentile`, `argmin`, `argmax`, `cumsum`, `sorted`, `unique` |
| Masks | `greater`, `less`, `equal`, `select(mask)` |
| Matrices | `dot`, `inner`, `det`, `inverse`, `solve` |

It's written in Dessa, so large arrays are slower than NumPy, whose loops are in C.

## Ready-made objects

| Object | What it has |
|---|---|
| `stats` | `sum`, `mean`, `median`, `mode`, `variance`, `stdev`, `smallest`, `largest`, `range`, `percentile`, `covariance`, `correlation`, `histogram`, `zscores` |
| `primes` | `isPrime`, `upTo`, `next`, `factorize`, for numbers of any size |
| `words` | `words.of(78056)` is "seventy-eight thousand fifty-six", up to about 10^3000; `words.parse(...)` reads them back |
| `roman` | `roman.of(1994)`, `roman.parse("MCMXCIV")` |
| `digits` | `of`, `count`, `sum`, `root`, `squares`, `sorted`, `reversed`, `isPalindrome`, of any size |
| `radix` | `encode(255, 16)`, `decode("ff", 16)`, any base from 2 to 36, of any size |
| `units` | `convert(10, "km", "mi")`: length, mass, time, area, volume, speed, data, temperature |
| `columns` | `name(28)` is "AB", `number("AB")` is 28 |
| `numformat` | `commas`, `ordinal`, `percent`, `compact` (1.5M), `bytes` (1.5 MB) |

Mistakes are a `NumberError`. The functions of version 1 (`toRoman`, `toWords`,
`sumDigits`, `excelColumn`, ...) still work, and will go in a later version.
