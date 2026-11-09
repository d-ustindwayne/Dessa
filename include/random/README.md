# random

Random numbers, choices and identifiers, with a seed for repeatable runs.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("random") { randInt, pick, shuffle, seed, uuid }

print(randInt(1, 6));                        // 1 to 6
print(pick(<"rock", "paper", "scissors">));
print(shuffle(<1, 2, 3, 4, 5>));
print(uuid());
seed(42);                                    // the same numbers every run
```

## What it has

| Name | Gives |
|---|---|
| `seed(value)` | sets the starting point |
| `rand()` | a `Dbl` from 0 to 1 |
| `randInt(low, high)` | a whole number, both ends included |
| `randDbl(low, high)` | a `Dbl` in that range |
| `chance(p)` | `true` with probability `p` |
| `pick(items)` | one item of any list |
| `shuffle(items)` | the list in a random order |
| `sample(items, count)` | `count` items of the list |
| `randStr(length)` | letters and digits |
| `uuid()` | a random identifier |

A seed gives the same numbers in a native program and in WebAssembly.

These numbers are predictable. For passwords and keys use `dcrypto`.
