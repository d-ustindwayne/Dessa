/***
 * $("os") -- the Dessa part: open files that are read or written a piece at
 * a time.
 *
 *     $("os") { open, File }
 *
 *     var[File] out = open("notes.txt", "w");      // "w" write, "a" add to the end
 *     out.writeLine("first");
 *     out.writeLine("second");
 *     out.close();
 *
 *     var[File] notes = open("notes.txt");         // "r" read (the default)
 *     while (!notes.atEnd()) print(notes.readLine());
 *     notes.close();
 *
 * For a whole file at once, readFile and writeFile are simpler.
 *
 * Also here: execute (runs a program without a shell) and the path helpers
 * joinPath, baseName, dirName and extension.
 * The native helpers are in os.hpp (declared in os.ds).
 */

$("os") { _fileOpen, _fileReadLine, _fileRead, _fileAtEnd, _fileWrite, _fileFlush, _fileClose, _execute }

// An open file. Close it when done (writes are saved by then at the latest).
class File {
    var[Str] path = "";
    var[Str] mode = "r";
    var[Int] _handle = -1;

    init(path[Str], mode[Str]) {
        this.path = path;
        this.mode = mode;
        this._handle = _fileOpen(path, mode);
    }

    // The next line (without its line break), or nil at the end of the file.
    func[Str | Nil] readLine() {
        return _fileReadLine(this._handle);
    }

    // Everything not read yet.
    func[Str] read() {
        return _fileRead(this._handle);
    }

    // The lines not read yet.
    func[Str<>] lines() {
        var[Str<>] all = <>;
        var[Str | Nil] line = this.readLine();
        while (line != nil) {
            all.push(line);
            line = this.readLine();
        }
        return all;
    }

    // true when everything has been read.
    func[Bul] atEnd() {
        return _fileAtEnd(this._handle);
    }

    // Writes text as it is.
    func[Emp] write(text[Str]) {
        _fileWrite(this._handle, text);
    }

    // Writes text and a line break.
    func[Emp] writeLine(text[Str]) {
        _fileWrite(this._handle, text + "\n");
    }

    // Saves what was written so far.
    func[Emp] flush() {
        _fileFlush(this._handle);
    }

    func[Bul] isOpen() {
        return this._handle >= 0;
    }

    func[Emp] close() {
        if (this._handle >= 0) _fileClose(this._handle);
        this._handle = -1;
    }

    func[Str] toStr() {
        return $"<File {this.path} ({this.mode}){this._handle < 0 ? " closed" : ""}>";
    }
}

// Opens a file: "r" to read (the default), "w" to write (replacing what was
// there), "a" to add to the end.
func[File] open(path[Str], mode[Str] = "r") {
    return new File(path, mode);
}

// ============================================================
// Running programs
// ============================================================

// What a program did: its exit code (0 usually means success) and what it wrote
// (its output and errors together).
class ProcessResult {
    var[Int] code = 0;
    var[Str] output = "";
    func[Bul] ok() { return this.code == 0; }
    func[Str] toStr() { return $"ProcessResult({this.code})"; }
}

// Runs a program and waits for it. No shell is involved, so the arguments
// are safe whatever they hold:
//     var[ProcessResult] r = execute("git", <"log", "--author", whoever>);
//     if (r.ok()) print(r.output);
// Error if the program can't be started. On Windows, batch files (.bat, .cmd)
// are refused, because they go through the command shell. Not in WebAssembly.
func[ProcessResult] execute(program[Str], args[Str<>] = <>) {
    var[Str<>] raw = _execute(program, args);
    var[ProcessResult] result = new ProcessResult();
    result.code = Int(raw<0>);
    result.output = raw<1>;
    return result;
}

// ============================================================
// Paths
// ============================================================

func[Bul] _isSeparator(c[Chr]) { return c == '/' || c == '\\'; }

// The place of the last / or \ in a path, or -1
func[Int] _lastSeparator(path[Str]) {
    var[Chr<>] all = path.chars();
    for (var[Int] i = all.length() - 1; i >= 0; i--) {
        if (_isSeparator(all<i>)) return i;
    }
    return -1;
}

// joinPath("logs", "2026", "a.txt") is "logs/2026/a.txt". / works on every system.
func[Str] joinPath(parts[Str*]) {
    var[Str] out = "";
    for (var[Str] part in parts) {
        if (part == "") continue;
        if (out == "" || _isSeparator(out<out.length() - 1>)) out += part;
        else out += "/" + part;
    }
    return out;
}

// The last part: baseName("logs/a.txt") is "a.txt"
func[Str] baseName(path[Str]) {
    var[Int] at = _lastSeparator(path);
    return path.substr(at + 1, path.length() - at - 1);
}

// All but the last part: dirName("logs/a.txt") is "logs", dirName("a.txt") is ""
func[Str] dirName(path[Str]) {
    var[Int] at = _lastSeparator(path);
    return at < 0 ? "" : at == 0 ? path.substr(0, 1) : path.substr(0, at);
}

// With its dot: extension("a.tar.gz") is ".gz", extension("notes") is ""
func[Str] extension(path[Str]) {
    var[Str] name = baseName(path);
    var[Chr<>] all = name.chars();
    for (var[Int] i = all.length() - 1; i > 0; i--) {
        if (all<i> == '.') return name.substr(i, name.length() - i);
    }
    return "";
}
