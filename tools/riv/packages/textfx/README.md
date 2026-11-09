# textfx

Small text helpers: boxes, padding, and word puzzles.

**Install:** Install with `riv add textfx`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("textfx") { box, padLeft, isPalindrome, isAnagram, splitCamel }

print(box("Hello", "World"));
print(padLeft("7", 3, "0"));              // 007
print(isPalindrome("Was it a car"));
print(isAnagram("listen", "silent"));     // true
print(splitCamel("userName"));            // user Name
```

## What it has

| Name | Gives |
|---|---|
| `box(lines...)` | the lines in a box |
| `padLeft(text, width, fill)`, `padRight(text, width, fill)` | text made wider |
| `isPalindrome(text)`, `isAnagram(first, second)` | word puzzles |
| `countConsonants(text)` | how many consonants |
| `splitCamel(text)` | `userName` as `user Name` |
