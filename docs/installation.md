# Installing Dessa

Dessa turns your programs into C (and some into C++), so it needs gcc and g++
on the computer (13 or newer; they come together, e.g. MinGW on Windows). When
clang is installed too, Dessa uses it for the C (on Windows, the LLVM MinGW
kind; the installer offers it). The installers check for one and tell you what to
do if it's missing.

There are two ways to install:

- **Prebuilt download.** Get the archive for your system from the
  [releases page](https://github.com/d-ustindwayne/Dessa/releases), unpack it
  and run the installer inside. Nothing gets built.
- **From the source.** Clone the repository and run the installer. It builds
  Dessa first, so it also needs CMake.

Either way you end up with `dessa` (the compiler) and `riv` (the package
manager) on your PATH.

Web pages run as WebAssembly, which has its own compiler. The installers
download it (about 50 MB, once); to get it yourself:

```
dessa setup wasm
```

It downloads about 50 MB into Dessa's folder (`~/.dessa/wasm` if that folder
can't be written). See [web.md](web.md).

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

## Node only: npm

Without installing anything else, the npm package compiles Dessa programs and
pages to WebAssembly and runs them with Node:

```
npm install dessalang
npx dessalang run app.ds
```

See [web.md](web.md#npm).

## VS Code

The Dessa extension gets its information from the compiler itself, so what it
shows always matches what `dessa` does: highlighting (also in `.dhtml` pages),
errors as you type, completion from the real types, hover, go to definition,
parameter hints, quick fixes and a Run button. The extension's README lists
everything.

The installer can install the extension for you. Otherwise download the
`.vsix` from a GitHub release and use "Install from VSIX..." in VS Code's
Extensions view. To use the copy in this repository instead, copy (or link)
`tools/vscode-dessa` to your extensions folder as `dessa-0.1.0` and restart
VS Code:

- Windows: `%USERPROFILE%\.vscode\extensions\`
- Linux, macOS: `~/.vscode/extensions/`

| Setting | Default | Meaning |
|---|---|---|
| `dessa.compilerPath` | `dessa` | the `dessa` executable (set it if `dessa` isn't on PATH) |
| `dessa.checkOnType` | `true` | check for errors while typing (otherwise on open and save) |
| `dessa.unusedImports` | `dim` | imports the file never uses: `dim` (faded), `warning`, or `off` |
| `dessa.packageFolders` | `[]` | extra folders with packages (like `DESSA_PACKAGES`) |
| `dessa.autoBrackets` | `true` | typing helpers: `$` becomes `$()`, `var` gets `[]`, and so on |

## Uninstalling

- Windows: `powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall`
- Linux, macOS: `sh install.sh --uninstall`

Your projects, their `riv_modules` folders and riv's store in `~/.riv` are
left alone.
