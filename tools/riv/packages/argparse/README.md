# argparse

Command-line arguments, with `--help` made for you.

**Install:** Install with `riv add argparse`.

**Runs in:** Native programs and WebAssembly in Node.

## Example

```dessa
$("argparse") { ArgParser, Args }
$("sys") { args }

var[ArgParser] parser = new ArgParser("greet", "Says hello");
parser.flag("loud", "l", "shout it");
parser.option("name", "n", "who to greet", "World");
parser.positional("file", "where to write it", false);

var[Args] a = parser.parse(args());
print(a.has("loud"), a.get("name"), a.find("file"));
```

## What it has

| `ArgParser` | |
|---|---|
| `flag(name, short, help)` | on or off |
| `option(name, short, help, fallback)` | takes a value |
| `positional(name, help, required)` | a word by position |
| `rest(name, help)` | the words left over |
| `parse(arguments)` | gives the `Args` |

An `Args` has `has(name)`, `get(name)`, `find(name)`, `getInt`, `getDbl` and `list(name)`.

Options are written `--name Ann`, `--name=Ann` or `-n Ann`, and short flags join (`-lv`).

A mistake prints what is wrong with the usage line and exits with code 2.
