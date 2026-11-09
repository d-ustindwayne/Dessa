# sys

The running program: its arguments, the platform, and leaving with an exit code.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs and WebAssembly (pages and Node).

`printErr(text)` writes to stderr, apart from the output. `programPath()` is the running program's file.

## Example

```dessa
$("sys") { args, exit, platform }

print(platform());
if (args().isEmpty()) {
    print("usage: greet <name>");
    exit(2);
}
print($"Hello, {args()<0>}!");
```

## What it has

| Name | Gives |
|---|---|
| `args()` | the program's arguments, as a `Str<>` |
| `exit(code)` | ends the program with that exit code |
| `platform()` | which system this is running on |
