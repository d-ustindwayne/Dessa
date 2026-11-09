# chimb

Tests for your code. `dessa test` finds and runs them.

**Install:** Install with `riv add chimb`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("chimb") { test, group, expect, expectTrue, expectThrows, done }

test("adds", () := { expect(2 + 3).toBe(5); });
test("lists", () := { expect(<1, 2>.map((x[Int]) := x * 2)).toBe(<2, 4>); });
test("errors", () := { expectThrows(() := { var[Int<>] e = <>; print(e<0>); }); });
done();
```

Each test prints `ok` or `FAIL` with what went wrong. `done()` prints the totals and exits
with code 1 when a test failed.

## What it has

| Name | Does |
|---|---|
| `test(name, body)` | one test |
| `group(name, body)` | keeps tests together |
| `beforeEach`, `afterEach` | inside a group, run around each of its tests, also the failed ones |
| `expect(x)` | `.toBe`, `.notToBe`, `.toBeIn` |
| `expectTrue`, `expectFalse` | conditions |
| `expectNear` | decimals |
| `expectThrows(body)` | that the body fails |
| `fail(message)` | fails the test right away |
| `done()` | the totals |

## Running them

`dessa test` runs every `*_test.ds` and `*.test.ds` file under the folder (not
`riv_modules/` or `build/`) and adds up the results.
