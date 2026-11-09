# colors

Terminal colours and backgrounds, and colour codes in hex and RGB.

**Install:** Install with `riv add colors`.

**Runs in:** Native programs and WebAssembly in Node: anything with a terminal.

## Example

```dessa
$("colors") { red, textColor, background, banner, stripColors, rgbToHex, hexToRgb }

print(red("failed"));
print(textColor("done", "green"));
print(background("note", "yellow"));
print(banner("Results"));
print(stripColors(red("plain again")));
print(rgbToHex(20, 116, 60), hexToRgb("#14743C"));
```

## What it has

| Name | Does |
|---|---|
| `red`, `green`, `yellow`, `blue`, `magenta`, `cyan` | colours text |
| `textColor(text, color, bright)` | colours text by name |
| `background(text, color, bright)` | colours behind the text |
| `paint(text, code)` | any terminal colour code |
| `bold(text)` | bold text; import it with `$("colors.styles") { bold }` |
| `banner(title)` | a heading |
| `stripColors(text)` | removes the colours |
| `clearLines(count)` | clears lines already printed |
| `rgbToHex(red, green, blue)`, `hexToRgb(hex)` | colour codes |
