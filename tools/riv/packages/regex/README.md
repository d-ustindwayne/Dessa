# regex

Regular expressions, written in Dessa.

**Install:** Install with `riv add regex`.

**Runs in:** Native programs and WebAssembly (pages and Node).

## Example

```dessa
$("regex") { Regex, Match }

var[Regex] date = new Regex("""(\d{4})-(\d\d)-(\d\d)""");
print(date.test("due 2026-11-02"));                 // true
print(date.find("due 2026-11-02")?.group(1));       // 2026
print(date.replaceAll("2026-11-02", "$3/$2/$1"));   // 02/11/2026
print(new Regex(""",\s*""").split("a, b,c"));       // <"a", "b", "c">
```

Write patterns in `"""triple quotes"""`, where a backslash stays a backslash.

## What it has

| Name | Gives |
|---|---|
| `test(text)` | whether it appears anywhere |
| `matches(text)` | whether it is the whole text |
| `find(text)` | a `Match`, with `text`, `start`, `end`, `group(n)`; or nil |
| `findAll(text)` | every `Match` |
| `replace`, `replaceAll` | new text, with `$0`, `$1`, `$$` in the replacement |
| `split(text)` | the pieces between matches |
| `escape(text)` | text that matches itself |

Patterns: `. [abc] [^a-z] \d \w \s \b ^ $ * + ? {n,m}` (add `?` for as few as possible),
groups `( )` and `(?: )`, and `|`.

Flags: `"i"` (ignore case), `"m"` (`^ $` per line), `"s"` (`.` takes newlines).
