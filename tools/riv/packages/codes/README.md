# codes

Check digits for barcodes, books, mail and cards, and two simple ciphers.

**Install:** Install with `riv add codes`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("codes") { isValidCard, isValidIsbn13, isbn13CheckDigit, rot13, caesar }

print(isValidCard("4539 1488 0343 6467"));
print(isValidIsbn13("978-0-306-40615-7"));
print(isbn13CheckDigit("978030640615"));
print(rot13("hello"), caesar("hello", 3));
```

## What it has

| Kind | Make the digit | Check a code |
|---|---|---|
| UPC | `upcCheckDigit(first11)` | `isValidUpc(code)` |
| ISBN-10 | `isbn10CheckDigit(first9)` | `isValidIsbn10(code)` |
| ISBN-13 | `isbn13CheckDigit(first12)` | `isValidIsbn13(code)` |
| USPS | `uspsCheckDigit(first10)` | `isValidUsps(code)` |
| Luhn (credit cards) | `luhnCheckDigit(digits)` | `isValidCard(number)` |

Spaces and dashes in codes are ignored.

`rot13(text)` and `caesar(text, shift)` are for puzzles, not secrets: use `dcrypto` for
those.
