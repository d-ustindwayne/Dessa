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
 * The native helpers are in os.hpp (declared in os.ds).
 */

$("os") { _fileOpen, _fileReadLine, _fileRead, _fileAtEnd, _fileWrite, _fileFlush, _fileClose }

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
