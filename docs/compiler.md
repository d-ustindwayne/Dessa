# Using the compiler

`dessa` checks, builds and runs Dessa programs and pages.

## Running and building

```
dessa run app.ds                     compile and run (the result is cached, so the second run starts at once)
dessa run app.ds -- one two          pass arguments to the program (read them with args() from $(<sys>))
dessa build app.ds                   make a program: app.exe on Windows, app elsewhere
dessa build app.ds -o tools/app.exe  choose the output file
dessa check app.ds                   only look for errors
```

`dessa run` compiles the program once and keeps the result in your temporary
folder (`%TEMP%\dessa` or `/tmp/dessa`). It only compiles again when the
program or the libraries it uses change.

A file in the folder (or a subfolder) can import other files with
`$("folder.file")`, and every file the program uses is compiled with it. There
is no project file to set up.

## Targets

By default Dessa makes a native program through C++. `--target js` (or its
alias `--target web`) makes JavaScript instead:

```
dessa run app.ds --target js                 run with Node
dessa build app.ds --target web -o app.js    one self-contained .js file
```

Both give the same output for the same program. Libraries with a C++ part
(`sql`, `request`, `ashwee`) only work natively. `webutils` only works in
JavaScript.

## Pages

```
dessa run page.dhtml                 serve the page's folder and open it in the browser
dessa build page.dhtml               write page.html with the Dessa scripts compiled
dessa check page.dhtml               errors in the page's scripts, with the page's line numbers
dessa serve [folder] [--port 8080]   a web server: .dhtml pages are compiled on each request
```

`dessa build` and `dessa serve` also work on a normal `.html` page that holds
`<script type="text/dessa">` scripts. See [web.md](web.md).

## Seeing the generated code

```
dessa emit app.ds                    the C++ (or JavaScript with --target js)
dessa emit app.ds -o app.cpp
```

## All commands

| Command | What it does |
|---|---|
| `run <file>` | compile and run a program, or serve and open a page |
| `build <file>` | write a program, a `.js` file, or a compiled page |
| `check <file>` | report errors and warnings only |
| `emit <file>` | print the generated C++ or JavaScript |
| `serve [folder]` | a local web server for pages |
| `symbols`, `complete`, `hover` | answers for editors, as JSON (used by the VS Code extension) |
| `tokens`, `ast` | show what the lexer and parser read (for working on Dessa itself) |
| `--version`, `help` | |

| Option | Meaning |
|---|---|
| `-o <file>` | output file for `build` and `emit` |
| `--target native\|js\|web` | what to make (native is the default) |
| `--port <n>` | port for `serve` and for `run page.dhtml` (default 8080; the next free one is used) |
| `--json` | `check`: print the errors as JSON |
| `--stdin` | read the file's text from standard input (editors use this for unsaved changes) |
| `--` | everything after it goes to the program |

## Settings (environment variables)

| Variable | What it does |
|---|---|
| `DESSA_CXX` | the C++ compiler to use (default `g++`), e.g. `C:/mingw64/bin/g++.exe` or `clang++` |
| `DESSA_HOME` | the Dessa folder, if `dessa` can't find its `runtime` and `include` folders by itself |
| `DESSA_PACKAGES` | extra folders to look for packages in (separated by `;` on Windows, `:` elsewhere) |
| `DESSA_VERBOSE` | set to `1` to see the C++ compiler's own messages when something goes wrong inside Dessa |
| `RIV_NETWORK` | the riv network database (default `~/.riv/network.db`) |

## Errors

Errors in your program are shown with their place:

```
app.ds:12:9: error: Cannot convert Str to Int in declaration of 'count'
```

A program that stops with an error that nobody caught shows the error and the
line it happened on, and exits with code 1:

```
Error: Index 3 is out of range for list of length 2
  at app.ds:14
```

You never see errors from the C++ compiler itself. If Dessa makes C++ that
doesn't compile, that's a bug in Dessa: you get a short message and the path
of a log file to report. `DESSA_VERBOSE=1` shows the details.

## Where libraries come from

`$(<name>)` looks in three places, in this order:

1. the core libraries that come with Dessa (`math`, `os`, `sys`, `time`, `webutils`),
2. `riv_modules/<name>` in the program's folder or any folder above it (installed with `riv add`),
3. each folder in `DESSA_PACKAGES`.

`$("name")` first looks for a file `name.ds` next to the importing file and in
the project folder, then for a package with that name in the same places.

If a library can't be found, the error says how to install it, for example:
`Unknown library '<sql>'; if it's a riv package, install it with: riv add sql`.
