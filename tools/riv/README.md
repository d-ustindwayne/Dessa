# riv: the Dessa package manager (prototype)

riv is written in Dessa itself (`riv.ds`, using the `sql` and `json` packages). It installs
packages into a project's `riv_modules/` folder.

The plan is for riv to share packages **peer to peer, like a torrent**, instead of downloading
them from one central server. That network doesn't exist yet, so this prototype **simulates it in
a SQLite database** (see [The network](#the-network-simulated)).

## Build

```
cmake --build build --target riv          # -> build/riv.exe
```

`install.ps1` / `install.sh` in the repo root build it, put it on PATH and share the bundled
packages for you.

## A package

A package is any folder with a `riv.pkg` file:

```
name = colors
version = 1.0.0
entry = lib.ds
description = Terminal colours

[dependencies]
textfx = 0.2.0
```

A package with C++ code also has a `<name>.native` file (see `include/README.md`).
`packages/` holds the packages that come with Dessa:

| Package | What it is |
|---|---|
| `json`, `phl` | data formats (Dessa only) |
| `request` | HTTP through curl |
| `sql` | SQLite (bundled, ~9 MB of C) + SQL Server / ODBC |
| `ashwee` | desktop GUI (Windows, Linux, macOS), with `examples/todo.ds` |
| `colors`, `textfx` | tiny examples |

## Commands

| Command | What it does |
|---|---|
| `riv init [name]` | create `riv.pkg` (and `main.ds`) in the current folder |
| `riv add sql` / `riv add sql@1.0.0` | add a dependency (latest version by default) and install it |
| `riv remove sql` | remove a dependency |
| `riv install` | install every dependency (and theirs) into `riv_modules/`; skips ones already there |
| `riv list` | show the dependencies and whether they're installed |
| `riv search [text]` | list the packages on the network |
| `riv publish [folder]` | share a package on the network (`--all <folder>` for every package in a folder) |
| `riv network` | show the peers; `churn` takes random ones offline, `heal` brings them back |
| `riv run [args]` | run the package's entry file |

## Using an installed package

```dessa
$(<sql>);                              // riv_modules/sql/sql.dm, plus its C++ part
{ Window, Button } from $(<ashwee>);
{ red, banner } from $("colors");      // riv_modules/colors/lib.ds
```

The compiler finds `riv_modules/` in the program's folder or any folder above it, so files in
`src/` work too.

## The network (simulated)

The network is the database `~/.riv/network.db`. Set `RIV_NETWORK` to use another file. It has
8 made-up peers (manila, tokyo, berlin, ...). A few of them are unreliable and sometimes send
damaged data.

- **`riv publish`** cuts every file into pieces of 64K characters and hashes each piece (two
  32-bit FNV-1a hashes, written as 16 hex digits). It gives copies of each piece to 4 random
  peers. A piece is stored once even if several package versions share it.
- **`riv install`** downloads each piece from a random online peer that has it and checks it
  against its hash. If the hash doesn't match, it asks another peer, for up to 3 rounds. Each
  finished file is checked against its own hash before it's written.
- **`riv network churn`** takes random peers offline to show the swarm still works with fewer
  peers. **`riv network heal`** brings them all back.

```
riv publish --all tools/riv/packages
mkdir C:\temp\shop && cd C:\temp\shop
riv init shop
riv add sql
  + sql@1.0.0: 7 file(s), 154 piece(s) from 8 peer(s), 3 damaged piece(s) caught by the hash check and fetched again
  + json@1.0.0: 2 file(s), 2 piece(s) from 2 peer(s)
```

## Not done yet (it's a prototype)

- a real peer-to-peer network (peer discovery, transfers between machines, signing packages)
- only text files can be packaged (no images or other binary files yet)
- version ranges (`^1.0`) and a lock file; versions are exact today
- `riv remove` doesn't clean up packages that were only needed by the removed one
