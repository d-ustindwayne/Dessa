# os

Files, folders, paths, environment variables and starting other programs.

**Install:** Comes with Dessa, nothing to install.

**Runs in:** Native programs, and WebAssembly in Node for files. A page has no files or programs to start.

## Example

```dessa
$("os") { writeFile, readFile, joinPath, open, File, execute, ProcessResult }

var[Str] path = joinPath("logs", "today.txt");
writeFile("note.txt", "hello");
print(readFile("note.txt"), path);

var[File] log = open("log.txt", "a");
log.writeLine("started");
log.close();

var[ProcessResult] result = execute("git", <"--version">);
if (result.ok()) print(result.output);
```

## What it has

| Part | Names |
|---|---|
| Files | `readFile`, `writeFile`, `appendFile`, `copyFile`, `moveFile`, `deleteFile`, `fileExists`, `fileSize`, `modifiedTime` |
| Folders | `isDir`, `listDir`, `makeDir`, `deleteAll`, `cwd` |
| Paths | `joinPath`, `baseName`, `dirName`, `extension` |
| Environment | `getEnv`, `setEnv` |
| Programs | `execute(program, args)`, `run(command)` |
| A piece at a time | `open(path, mode)` gives a `File` |

`open`'s mode is `"r"` to read (the default), `"w"` to write (replacing what was
there) or `"a"` to add to the end. A `File` has `readLine()` (nil at the end),
`lines()`, `read()` (the rest), `atEnd()`, `write(text)`, `writeLine(text)`,
`flush()`, `isOpen()` and `close()`.


## Starting programs safely

`execute` involves no shell, so its arguments can hold anything. `run` hands a whole
line to the shell: use it only for commands you wrote yourself, and never put text
from a user, a file or the network in it.
