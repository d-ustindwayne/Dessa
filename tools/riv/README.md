# riv: the Dessa package manager

riv is written in Dessa itself (`riv.ds`, using the `torrent`, `json` and `request` packages). It
installs packages into a project's `riv_modules/` folder.

There is no package server: riv shares packages **peer to peer with the BitTorrent protocol**
(see [Sharing](#sharing)).

## Build

```
cmake --build build --target riv          # -> build/riv.exe
```

`install.ps1` / `install.sh` in the repo root build it, put it on PATH and run
`riv bundled <packages folder>` so the bundled packages install without downloading.

## A package

A package is any folder with a `riv.pkg` file:

```
name = colors
version = 1.0.0
entry = lib.ds
description = Terminal colours

[dependencies]
textfx = ^0.2.0
```

A dependency is a version or a range (`^1.2.0`, `~1.2.0`, `>=1.0 <2.0`, `1.x`,
`*`); `riv install` writes the exact versions and the sha256 of their files to
`riv.lock` (see docs/riv.md).

A package with C++ code also has a `<name>.native` file (see `include/README.md`).
`packages/` holds the packages that come with Dessa:

| Package | What it is |
|---|---|
| `json` | a data format (Dessa only) |
| `request` | HTTP through curl |
| `sql` | SQLite (bundled, ~9 MB of C) + SQL Server / ODBC |
| `ashwee` | desktop GUI (Windows, Linux, macOS), with `examples/todo.ds` |
| `torrent` | the BitTorrent engine riv uses (C++: trackers, local network discovery, web seeds) |
| `colors`, `textfx`, `codes`, `numbers` | small packages written in Dessa |

## Commands

| Command | What it does |
|---|---|
| `riv init [name]` | create `riv.pkg` (and `main.ds`) in the current folder |
| `riv add sql` / `riv add sql@1.0.0` | add a dependency (`^` the latest version by default, or a version or range) and install it |
| `riv remove sql` | remove a dependency |
| `riv install` | install every dependency (and theirs) into `riv_modules/`, as in `riv.lock`; skips ones already there |
| `riv upgrade [sql]` | the newest versions `riv.pkg` allows, instead of `riv.lock`'s |
| `riv list` | show the dependencies and whether they're installed |
| `riv search [text]` | list the packages in the index |
| `riv info sql` | versions, size, infohash and magnet link |
| `riv publish [folder]` | put a package in your index and store (`--webseed <url>`, `--pr` for the official index) |
| `riv seed` | share your packages (`--port <n>`, `--minutes <n>`) |
| `riv bundled <folder>` | use the bundled packages from that folder without downloading |
| `riv index <folder> --out <index>` | write index entries for every package in a folder (`--check` to verify) |
| `riv run [args]` | run the package's entry file |

## Using an installed package

```dessa
$("sql");                              // riv_modules/sql/sql.dm, plus its C++ part
$("ashwee") { Window, Button }
$("colors") { red, banner }      // riv_modules/colors/lib.ds
```

The compiler finds `riv_modules/` in the program's folder or any folder above it, so files in
`src/` work too.

## Sharing

- **The index** (`index/` here, read from GitHub) lists every package version with its torrent:
  `index.json` for `riv search`, and `<name>.json` with the versions. Your own packages go in
  `~/.riv/index`. After changing a bundled package, raise its version and run
  `riv index tools/riv/packages --out tools/riv/index --webseed https://raw.githubusercontent.com/d-ustindwayne/Dessa/main/tools/riv/packages/`;
  `python tests/test.py riv` checks that the index matches the packages.
- **`riv install`** uses a copy already on the computer (the store `~/.riv/store`, or the bundled
  packages) when its files match the torrent. Otherwise it downloads the torrent: peers come from
  public trackers, the local network (BEP 14 multicast) and `RIV_PEERS`; when none of them has a
  piece, the torrent's web seed (the package folder on GitHub) is used. Pieces are checked with
  SHA-1.
- **`riv seed`** shares the store and the bundled packages. Any BitTorrent program can join in:
  `riv info <name>` prints the magnet link.
- Text files are shared with LF line endings, so a Windows checkout, a Linux one and the files on
  GitHub all give the same torrent.

```
riv add sql
  ~ sql@1.0.0: looking for people sharing it...
  2 peer(s) found
  + sql@1.0.0: 149 piece(s): 149 from 2 peer(s)
```

## Not done yet

- version ranges (`^1.0`) and a lock file; versions are exact today
- signing packages (today the index, with its SHA-1 piece hashes, is what you trust)
- `riv remove` doesn't clean up packages that were only needed by the removed one
