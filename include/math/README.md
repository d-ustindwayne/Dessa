# math

Numbers: roots, powers, trigonometry, rounding, simple statistics and `Big`, whole numbers of any size.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("math") { sqrt, hypot, mean, PI, Big }

print(sqrt(16.0), hypot(3.0, 4.0), PI);
print(mean(<1.0, 2.0, 6.0>));

var[Big] f = new Big(1);
for (var[Int] i = 1; i <= 30; i++) f = f.mul(i);
print(f, f.sci());     // 265252859812191058636308480000000 2.65E32
```

## What it has

| Part | Names |
|---|---|
| Roots and powers | `sqrt`, `cbrt`, `pow`, `exp`, `log`, `log2`, `log10`, `hypot` |
| Rounding and limits | `floor`, `ceil`, `round`, `trunc`, `abs`, `sign`, `min`, `max`, `clamp` |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh` |
| Whole numbers | `gcd`, `lcm`, `idiv` |
| Statistics on a `Dbl<>` | `sum`, `mean`, `median`, `mode`, `stdev` |
| Random numbers | `random`, `randomInt` |
| Constants | `PI`, `E` |

`idiv(a, b)` divides whole numbers exactly. `a / b` goes through a `Dbl`, so past
2^53 (about 9 quadrillion) it loses digits.

## Big

`Big` has `add`, `sub`, `mul`, `div`, `mod`, `pow`, `negate`, `abs`, `gcd`, `lcm`,
`modPow`, `isqrt`, `compare`, `equals`, `sign`, `isEven`, `isZero`, `isNegative`,
`length`, `sci`, `toInt`, `toDbl` and `toStr`, and the operators
`+ - * / % ^ == < >`.

For exact decimals and fractions, see the `numbers` package.
