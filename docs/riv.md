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
sql = ^2.0.0
json = 1.2.0
```

A dependency is a version or a range of versions; riv takes the newest one
that fits:

| Written | Means |
|---|---|
| `1.2.0` | exactly 1.2.0 |
| `^1.2.0` | 1.2.0 or newer, below 2.0.0 (for 0.x: `^0.4.1` stays below 0.5.0) |
| `~1.2.0` | 1.2.0 or newer, below 1.3.0 |
| `>=1.0 <2.0` | every condition (`>`, `>=`, `<`, `<=`, `=`), separated by spaces |
| `1.x`, `1.2.x` | any version starting that way |
| `*` | any version |

`riv install` writes `riv.lock`: the exact version of every package it
installed (dependencies of dependencies too) and the sha256 of its files.
Later installs take those same versions while they still fit `riv.pkg`, and
stop when a package's files are not the ones the lock remembers, so a package
can't change under you. `riv upgrade` (or `riv upgrade <name>`) takes the
newest versions `riv.pkg` allows and writes a new lock.

Programs then import the packages like libraries:

```dessa
$("sql");
$("colors") { red, banner }
```

## Commands

| Command | What it does |
|---|---|
| `riv init [name]` | create `riv.pkg` (and `main.ds`) in the current folder |
| `riv add <name>` | add a dependency (`^` the newest version) and install it |
| `riv add <name>@1.0.0` | add a specific version, or a range: `riv add sql@~2.0` |
| `riv remove <name>` | remove a dependency |
| `riv install` | install every dependency (and what they need) into `riv_modules/`, as in `riv.lock` |
| `riv upgrade [name]` | the newest versions `riv.pkg` allows, instead of `riv.lock`'s |
| `riv list` | show the dependencies and whether they are installed |
| `riv search [text]` | list the packages that are available |
| `riv info <name>` | versions, size, infohash and magnet link of a package |
| `riv publish [folder]` | put a package in your index and store, ready to share (see below) |
| `riv seed` | share your packages with others until you stop it |
| `riv bundled <folder>` | use the packages that came with Dessa from that folder (the installers do this) |
| `riv run [args]` | run the entry file |
| `riv update` | install the newest Dessa (the compiler and riv) from GitHub; `--force` reinstalls it |

Commit `riv.pkg` and `riv.lock` with your project, but not `riv_modules/`:
anyone can recreate that folder, with the same versions, with `riv install`.

## Keeping Dessa up to date

`riv update` looks at the newest release on GitHub. If it's newer than the
Dessa you have, it downloads the release for your computer and runs its
installer into the same folder (on Windows in a window of its own, since riv
can't replace itself while it runs).

You don't have to remember to check: once a day, the first riv command you run
asks GitHub whether there is a new release (it gives up after a few seconds
when there's no internet) and says so:

```
riv: Dessa 0.2.1 is out (you have 0.2.0). Update with: riv update
```

`dessa run` and `dessa build` show the same line, at most once a day, from what
riv found (the compiler itself never goes online). Set `RIV_NO_UPDATE_CHECK=1`
to turn both off.

## Packages that come with Dessa

| Package | What it has |
|---|---|
| `json` | `Json` values, `parseJson`, `stringify` |
| `sql` | SQLite and SQL Server (or any ODBC database) |
| `request` | HTTP requests (`get`, `post`, ...) through curl |
| `ashwee` | desktop windows and controls (Windows, Linux, macOS) |
| `numbers` | Roman numerals, numbers in words, spreadsheet columns, digits, fractions, temperatures |
| `codes` | check digits (UPC, ISBN, USPS, credit cards) and simple ciphers |
| `colors` | terminal colours and colour codes |
| `textfx` | boxes, padding, palindromes, anagrams, camelCase splitting |

`riv add` installs these straight from the Dessa folder, without
downloading anything. See [libraries.md](libraries.md) for what each one
contains.

## Making a package

A package is a folder with a `riv.pkg` and Dessa files. The entry file
(`lib.ds`, or `<name>.dm`) is what `$("name")` loads.

```
greet/
    riv.pkg          name = greet, version = 1.0.0, entry = lib.ds
    lib.ds           func[Str] hello(name[Str]) { return $"Hello, {name}!"; }
```

```
cd greet
riv publish          # into your index (~/.riv/index) and store (~/.riv/store)
riv seed             # share it; keep it running
riv publish --pr     # ask for it to be added to the official index (needs gh, the GitHub CLI)
```

Raise `version` in `riv.pkg` every time you publish again. Names starting with
`_` stay private to the package. `--webseed <url>` adds a web address that
holds `<name>/...` (a folder on GitHub works), used when nobody is sharing.

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

## How packages are shared

There is no package server: packages travel like torrents, with the
BitTorrent protocol.

- **The index** says which packages and versions exist, each with its
  torrent. It is a handful of JSON files. The official one is in the Dessa
  repository (`tools/riv/index/`) and riv reads it from GitHub. Yours is in
  `~/.riv/index`. Set `RIV_INDEX` to use other ones (folders or web
  addresses, separated like `PATH`).
- **Installing** looks for the package on your computer first: in your store
  (`~/.riv/store`, everything installed or published before) and in the
  packages that came with Dessa. If it isn't there, riv downloads it from the
  people sharing it. It finds them through public trackers (free servers that
  only pass addresses around) and on your local network (no internet needed,
  handy in a classroom). If no one is sharing it, the files come from the
  torrent's web seed: for the packages that come with Dessa, the package
  folder on GitHub. Every piece is checked with SHA-1, whoever sent it.
- **Sharing**: `riv seed` shares everything in your store and the packages
  that came with Dessa, until you stop it.

```
riv add sql
  ~ sql@1.0.0: looking for people sharing it...
  2 peer(s) found
  + sql@1.0.0: 149 piece(s): 149 from 2 peer(s)
  + json@1.0.0: from your store
```

Any torrent program can share a package too: `riv info sql` prints its magnet
link.

Things to know:

- Computers at home are usually behind a router, so two of them on different
  networks can't always reach each other. On the same network they always can.
  When no one can be reached, the web seed is used.
- The web seed of the official index points at the `main` branch, so it only
  serves the newest version of each bundled package. Older versions need
  someone sharing them.

Settings (environment variables): `RIV_HOME` (instead of `~/.riv`),
`RIV_INDEX`, `RIV_PEERS` (`host:port,host:port`, peers to try first),
`RIV_TRACKERS=0`, `RIV_LAN=0`, `RIV_WEBSEED=0` (turn a way of finding pieces
off), `RIV_TIMEOUT` (seconds to wait for peers, default 60).

For whoever keeps an index: `riv index <packages folder> --out <index folder>
--webseed <url>` writes the entries of every package in a folder, and
`--check` reports packages whose files changed without a new index entry (CI
runs it for the official index). Packages named in `index-skip.txt` in the
packages folder (one per line) are left out of the index and of `riv bundled`;
`phl` is there for now, while its syntax is being revised.

Not done yet: version ranges (`^1.0`) and a lock file, and signing packages.
