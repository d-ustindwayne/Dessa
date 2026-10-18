# Installing Dessa

Dessa turns your programs into C++, so it needs a C++ compiler on the computer
(g++ 13 or newer, or clang). The installers check for one and tell you what to
do if it's missing.

There are two ways to install:

- **Prebuilt download.** Get the archive for your system from the
  [releases page](https://github.com/d-ustindwayne/Dessa/releases), unpack it
  and run the installer inside. Nothing gets built.
- **From the source.** Clone the repository and run the installer. It builds
  Dessa first, so it also needs CMake.

Either way you end up with `dessa` (the compiler) and `riv` (the package
manager) on your PATH.

## Windows

1. Unpack the release zip (or clone the repository).
2. In that folder, run:

   ```
   powershell -ExecutionPolicy Bypass -File install.ps1
   ```

The installer walks you through these steps:

| Step | What happens |
|---|---|
| Tools | Looks for g++ (MinGW) and CMake. If one is missing it offers to install it with `winget`. |
| Build | Builds `dessa` and `riv` (skipped for a prebuilt download). |
| Install | Copies everything to `%LOCALAPPDATA%\Dessa` (you can choose another folder). |
| PATH | Adds `dessa` and `riv` to your PATH. If g++ isn't on PATH, it offers to add that too. |
| Packages | Runs `riv bundled` so `riv add sql` installs the bundled packages without downloading. |
| File types | Optional: Dessa icons for `.ds`, `.dm`, `.dhtml` files, and "Run with Dessa" in their right-click menu. |
| VS Code | Optional: installs the Dessa extension. |

Open a **new** terminal afterwards so it sees the new PATH, then check:

```
dessa --version
```

Options for the installer:

| Option | Meaning |
|---|---|
| `-Yes` | Accept every default and ask nothing |
| `-Prefix <folder>` | Install somewhere else |
| `-NoPath` | Don't change PATH |
| `-NoFileTypes` | Don't register file icons |
| `-NoVSCode` | Don't install the VS Code extension |
| `-Uninstall` | Remove Dessa (the folder, the PATH entry and the file types) |

If you would rather install a compiler yourself, [WinLibs](https://winlibs.com)
(MinGW-w64, UCRT) works well. Unpack it to `C:\mingw64` and the installer will
find it.

## Linux

Install a compiler and CMake with your package manager:

| Distribution | Command |
|---|---|
| Ubuntu, Debian | `sudo apt install g++ cmake make` |
| Fedora | `sudo dnf install gcc-c++ cmake make` |
| Arch | `sudo pacman -S gcc cmake make` |

Then, from the unpacked release (or the repository):

```
sh install.sh
```

Dessa goes to `~/.local/share/dessa`, with links to `dessa` and `riv` in
`~/.local/bin`. If that folder isn't on your PATH, the installer adds it to
your shell profile. Open a new terminal and run `dessa --version`.

To use SQL Server or other ODBC databases from the `sql` package, also
install unixODBC (`sudo apt install unixodbc-dev`). SQLite works without it.

Options: `--yes` (ask nothing), `--prefix <folder>`, `--uninstall`.

## macOS

Install the Xcode command line tools (they include clang) and CMake:

```
xcode-select --install
brew install cmake
```

Then run `sh install.sh` as on Linux.

## Building by hand

If you only want to build and try Dessa without installing:

```
cmake -S . -B build
cmake --build build
cmake --build build --target riv
build/dessa run sample/sample.ds
```

On Windows with MinGW add `-G "MinGW Makefiles"` to the first command.

## The browser build and npm

`dessa.js`, the compiler built for WebAssembly, comes with the releases and
with the npm package (`npm install dessalang`). To build it yourself you need the
[Emscripten SDK](https://emscripten.org):

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm            # writes web/dessa.js
```

See [web.md](web.md).

## VS Code

The installer can install the extension for you. To do it by hand, copy (or
link) `tools/vscode-dessa` to your VS Code extensions folder as
`dessa-0.1.0`:

- Windows: `%USERPROFILE%\.vscode\extensions\`
- Linux, macOS: `~/.vscode/extensions/`

Then restart VS Code. See [editor.md](editor.md).

## Uninstalling

- Windows: `powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall`
- Linux, macOS: `sh install.sh --uninstall`

Your projects, their `riv_modules` folders and riv's store in `~/.riv` are
left alone.
