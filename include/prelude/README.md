# prelude

The error classes. Runtime errors are objects of these classes, so a `catch` can choose by kind.

**Install:** Every file gets it automatically: there is nothing to import.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
class TooCold(Error) {}

try {
    var[Int<>] empty = <>;
    print(empty<0>);
} catch (e[IndexError]) {
    print("no such item:", e.message);
}

try {
    throw new TooCold("below zero");
} catch (e[Error]) {
    print(e);
}
```

## What it has

| Class | Parent | When |
|---|---|---|
| `Error` | | the base of them all; has `message` |
| `DessaError` | `Error` | the base of the errors the runtime raises |
| `IndexError` | `DessaError` | a list or text position that doesn't exist |
| `KeyError` | `DessaError` | a map key that doesn't exist |
| `ValueError` | `DessaError` | a value that can't be used, like `Int("abc")` |
| `OverflowError` | `DessaError` | an `Int` too big to hold |
| `DivisionError` | `DessaError` | dividing a whole number by zero |
| `NilError` | `DessaError` | using `nil` as a value |
| `FileError` | `DessaError` | a file that can't be read or written |

Libraries make their own errors the same way, such as `JsonError` and `SqlError`.
`catch (e[Str])` also works, and gets the message.
