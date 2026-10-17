# riv, the package manager

riv installs libraries ("packages") into your project. It is written in Dessa
itself.

## Starting a project

```
mkdir shop
cd shop
riv init            # writes riv.pkg (and main.ds if there is none)
riv add sql         # adds a dependency and installs it into riv_modules/
riv run             # runs the entry file (main.ds)
```

`riv.pkg` describes the project:

```
name = shop
version = 0.1.0
entry = main.ds
description = A small shop

[dependencies]
sql = 1.0.0
json = 1.0.0
```

Programs then import the packages like libraries:

```dessa
$(<sql>);
{ red, banner } from $("colors");
```

## Commands

| Command | What it does |
|---|---|
| `riv init [name]` | create `riv.pkg` (and `main.ds`) in the current folder |
| `riv add <name>` | add a dependency (the newest version) and install it |
| `riv add <name>@1.0.0` | add a specific version |
| `riv remove <name>` | remove a dependency |
| `riv install` | install every dependency (and what they need) into `riv_modules/` |
| `riv list` | show the dependencies and whether they are installed |
| `riv search [text]` | list the packages that are available |
| `riv publish [folder]` | share a package (`--all <folder>` for every package in a folder) |
| `riv network [churn\|heal]` | show the network's peers (see below) |
| `riv run [args]` | run the entry file |

Commit `riv.pkg` with your project, but not `riv_modules/`: anyone can recreate
that folder with `riv install`.

## Packages that come with Dessa

| Package | What it has |
|---|---|
| `json` | `Json` values, `parseJson`, `stringify` |
| `phl` | read and write PHL documents |
| `sql` | SQLite and SQL Server (or any ODBC database) |
| `request` | HTTP requests (`get`, `post`, ...) through curl |
| `ashwee` | desktop windows and controls (Windows, Linux, macOS) |
| `numbers` | Roman numerals, numbers in words, spreadsheet columns, digits, fractions, temperatures |
| `codes` | check digits (UPC, ISBN, USPS, credit cards) and simple ciphers |
| `colors` | terminal colours and colour codes |
| `textfx` | boxes, padding, palindromes, anagrams, camelCase splitting |

The installers share all of these, so `riv add <name>` works right away. See
[libraries.md](libraries.md) for what each one contains.

## Making a package

A package is a folder with a `riv.pkg` and Dessa files. The entry file
(`lib.ds`, or `<name>.dm`) is what `$("<name>")` or `$(<name>)` loads.

```
greet/
    riv.pkg          name = greet, version = 1.0.0, entry = lib.ds
    lib.ds           func[Str] hello(name[Str]) { return $"Hello, {name}!"; }
```

```
cd greet
riv publish
```

Raise `version` in `riv.pkg` every time you publish again. Names starting with
`_` stay private to the package.

### Packages with C++ code

A package can include C++ for things Dessa can't do by itself (talking to the
system, using a C library). Add a `<name>.native` file that describes it:

```
// sql.native
header sql.hpp                      the C++ declarations
source sqlite/sqlite3.c             C or C++ files compiled once (cached) and linked in
source sql.cpp
link.windows -lodbc32               link flags (link = every system, link.windows / link.unix = one)
func[Int] _sqliteOpen(path[Str])    each function: a Dessa signature
func[Str<>] _query(handle[Int], sql[Str], params[Str<>])
```

The functions live in the C++ namespace `dessa::<name>`. Usually they start
with `_` and the package's `.dm` file wraps them in friendly Dessa classes.
`tools/riv/packages/sql` and `tools/riv/packages/ashwee` are complete examples.

## The network

riv is meant to share packages peer to peer, like a torrent, instead of
downloading them from one central server. Until that exists it is simulated
in a SQLite database: `~/.riv/network.db` (set `RIV_NETWORK` to use another
file).

- `riv publish` cuts each file into pieces of 64,000 characters, hashes every
  piece and gives copies to 4 of the 8 simulated peers.
- `riv install` asks online peers for each piece and checks it against its
  hash. If a peer sends a damaged piece, riv asks another one. Each finished
  file is checked again before it is written.
- `riv network` shows the peers. `riv network churn` takes random peers
  offline to show the swarm still works with fewer of them, and
  `riv network heal` brings them all back.

```
riv add sql
  + sql@1.0.0: 7 file(s), 154 piece(s) from 8 peer(s), 2 damaged piece(s) caught by the hash check and fetched again
  + json@1.0.0: 2 file(s), 2 piece(s) from 2 peer(s)
```

Not done yet: a real network between computers, binary files (packages are
text only), version ranges and a lock file.
