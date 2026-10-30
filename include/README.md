# Libraries - `$("name")`

`$("name")` loads a library. It looks in three places, in this order:

1. **`include/<name>/`**: the core libraries that ship inside Dessa.
2. **`riv_modules/<name>/`**: a riv package installed with `riv add <name>`. riv looks in the
   project folder or any folder above it.
3. **The folders in `DESSA_PACKAGES`**: extra package folders. The tests and the riv build use this
   to load `tools/riv/packages` straight from the source tree.

If none of them has it, the error says `riv add <name>`.

## Core libraries (`include/`)

| Library | What it does |
|---|---|
| `math` | numbers, rounding, trigonometry, random, `Big` (whole numbers of any size, in `math.dm`) |
| `os` | files, folders, environment, shell commands, `File` (open files, in `os.dm`) |
| `random` | `randInt`, `pick`, `shuffle`, `sample`, `uuid`, ... (seedable, same numbers in C++ and JS) |
| `dsds` | data structures written in Dessa: `Stack`, `Queue`, `Deque`, `LinkedList`, `PriorityQueue`, `SearchTree`, `Graph` |
| `dcrypto` | hashes (md5, sha1, sha256, sha512, hmac), passwords, AES-256-GCM `encrypt` / `decrypt`, base64, hex |
| `sys` | command-line arguments, exit, platform |
| `time` | clocks, sleep, `DateTime` (time zones, formatting, month / day names in 11+ languages; in `time.dm`) |
| `webutils` | the web page: `document`, elements, events, `alert` / `prompt` / `confirm`, `log`, timers, `fetchText` (pages only; done by the page bridge in `runtime/dessa_web.c` and `dessa_wasm.js`, see `docs/web.md`) |

A core library is a folder `include/<name>/` containing:

| File | What it is |
|---|---|
| `<name>.hpp` | **native part** in C++. Its functions are registered in `src/analyser/builtins.cpp` |
| `<name>.c`   | the same native part in C (`dsx_<name>_<function>`), used by programs built through C and by WebAssembly |
| `<name>.ds`  | declarations of the native functions, for people and for Ctrl+click in VS Code |
| `<name>.dm`  | **Dessa part**: classes and functions written in Dessa, loaded automatically |

## riv packages (`tools/riv/packages/`)

These libraries are packages, so programs that don't use them stay small. Install one with
`riv add <name>`; the bundled ones install from the Dessa folder without downloading.

| Package | Native | What it does |
|---|---|---|
| `json` | | `Json` values, `parseJson`, `stringify`, builders |
| `request` | ✓ (curl natively, the browser or Node in WebAssembly) | HTTP: `get`, `post`, `put`, `patch`, `delete`, `Response` |
| `sql` | ✓ (`sql.c` / `sql.cpp` + SQLite) | databases: `connectMssql`, `connect` (any ODBC), `openSqlite` (also in WebAssembly) |
| `ashwee` | ✓ | desktop GUI: `Window`, `Button`, `TextBox`, `ListBox`, ... (Windows, Linux with GTK 3, macOS) |
| `torrent` | ✓ | BitTorrent: share folders peer to peer (riv uses it) |
| `colors`, `textfx` | | small examples of plain Dessa packages |

A package's native part is described by a **`<name>.native`** file next to its `.hpp`:

```
// comments
header sql.hpp                        the C++ declarations
source sqlite/sqlite3.c               C/C++ files compiled once (cached) and linked in
source sql.cpp
link.windows -lodbc32                 link flags (link = every OS, link.windows / link.unix = one OS)
link.unix -lodbc
namespace dessa::sql                  optional; default dessa::<name>
c sql.c                               the same functions in C, for C programs and WebAssembly
wasm.cflags -DSQLITE_THREADSAFE=0     extra flags for compiling the C for WebAssembly
wasm.link -lwasi-emulated-mman        extra flags for linking it into WebAssembly
func[Int] _sqliteOpen(path[Str])      each function: a Dessa signature
const[Dbl] PI                         a constant
```

Functions whose names start with `_` are helpers for the package's own `.dm` file.
`$("sql");` doesn't import them.

## Adding a library

- **Pure Dessa:** `riv init` a folder and write `<name>.dm` (or `lib.ds`). Share it with `riv publish`.
- **With native code:** add `<name>.hpp` (namespace `dessa::<name>`), any `.cpp` / `.c` files, and a
  `<name>.native` that lists them. To run in WebAssembly too, write the functions in C as well
  (`c <file>.c`): arguments are borrowed runtime values (`ds_str*`, `ds_int`, `ds_list*`), results
  are new ones, and errors go through `ds_fail`.
- **Core library:** put it in `include/<name>/` and register its functions in `builtinLibraries()` in
  `src/analyser/builtins.cpp`. Only do this for things nearly every program needs.
- Add a test in `tests/cases/run/`.
