/***
 * $("phl") -- read and write PHL, Dessa's data format. Written in Dessa.
 *
 *     name: "Dustin Dwayne"
 *     Services: [Database: [ServerName: "Server", Port: 1341]]
 *     product: {ID, Name, Price} [
 *         101, "Wireless Mouse", 25.99;
 *         102, "Ergonomic Chair", 149.50;
 *     ]
 *
 * Rules:
 *   - a document is `key: value` entries (one per line, or separated by commas)
 *   - values: "strings", numbers, true / false / nil,
 *     objects  [key: value, key: value]      (trailing commas are fine)
 *     tables   {col, col} [v, v; v, v;]      (a list of rows)
 *   - every value needs a key: a value on its own is a syntax error
 *   - // starts a comment
 *
 * Values are read into the same Json type as $("json"), so PHL and JSON
 * convert into each other:
 *
 *     $("phl");
 *     var[Json] doc = parsePhl(readFile("config.phl"));
 *     print(doc.get("product").at(3).get("ID").asInt());   // 104
 *     print(toPhl(doc));                                   // back to PHL text
 */

{ Json, JsonKind, jsonNull, jsonBool, jsonNum, jsonStr, jsonArray, jsonObject } from $("json");

class PhlError {
    var[Str] message = "";
    init(message[Str]) {
        this.message = message;
    }
    func[Str] toStr() {
        return $"PhlError: {this.message}";
    }
}

class PhlParser {
    var[Str] text = "";
    var[Chr<>] chars = [];   // the text as characters, for fast indexing
    var[Int] pos = 0;

    init(text[Str]) {
        this.text = text;
        this.chars = text.chars();
    }

    // Characters start..end (not included) as text
    func[Str] slice(start[Int], end[Int]) {
        return this.chars.slice(start, end).join("");
    }

    func[Emp] fail(message[Str]) {
        var[Int] line = 1;
        var[Int] column = 1;
        for (var[Int] i = 0; i < this.pos && i < this.chars.length(); i++) {
            if (this.chars<i> == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }
        throw new PhlError($"{message} at line {line}, column {column}");
    }

    func[Bul] atEnd() {
        return this.pos >= this.chars.length();
    }

    func[Chr] peek() {
        if (this.atEnd()) return '\0';
        return this.chars<this.pos>;
    }

    // Skips spaces, newlines and // comments.
    func[Emp] skip() {
        while (!this.atEnd()) {
            var[Chr] c = this.peek();
            if (c.isSpace()) {
                this.pos++;
            } else if (c == '/' && this.pos + 1 < this.chars.length() && this.chars<this.pos + 1> == '/') {
                while (!this.atEnd() && this.peek() != '\n') this.pos++;
            } else {
                return;
            }
        }
    }

    func[Emp] expectChar(c[Chr]) {
        this.skip();
        if (this.peek() != c) {
            this.fail(this.atEnd() ? $"expected '{c}' but the text ended" : $"expected '{c}', found '{this.peek()}'");
        }
        this.pos++;
    }

    func[Bul] isKeyChar(c[Chr]) {
        return c.isAlpha() || c.isDigit() || c == '_' || c == '-';
    }

    // A key: Name, server_name, or "any text"
    func[Str] key() {
        this.skip();
        if (this.peek() == '"') return this.string();
        var[Int] start = this.pos;
        while (!this.atEnd() && this.isKeyChar(this.peek())) this.pos++;
        if (this.pos == start) {
            this.fail(this.atEnd() ? "expected a key but the text ended"
                                   : $"expected a key, found '{this.peek()}' (every value needs a key: name: value)");
        }
        return this.slice(start, this.pos);
    }

    // key: value, key: value ...  until `close` ('\0' = end of the text)
    func[Json] entries(close[Chr]) {
        var[Json] result = jsonObject();
        loop {
            this.skip();
            if (close == '\0' && this.atEnd()) return result;
            if (close != '\0' && this.peek() == close) {
                this.pos++;
                return result;
            }
            if (this.atEnd()) this.fail($"expected '{close}' but the text ended");
            var[Str] name = this.key();
            if (result.has(name)) this.fail($"key '{name}' is used twice");
            this.skip();
            if (this.peek() != ':') {
                this.fail($"'{name}' needs a key and a colon: every value is written as key: value");
            }
            this.pos++;
            result.set(name, this.value());
            this.skip();
            if (this.peek() == ',') this.pos++;   // optional between entries, allowed at the end
        }
    }

    func[Json] value() {
        this.skip();
        if (this.atEnd()) this.fail("expected a value but the text ended");
        var[Chr] c = this.peek();
        if (c == '"') return jsonStr(this.string());
        if (c == '[') {
            this.pos++;
            return this.entries(']');
        }
        if (c == '{') return this.table();
        if (c == '-' || c.isDigit()) return this.number();
        var[Int] start = this.pos;
        while (!this.atEnd() && this.isKeyChar(this.peek())) this.pos++;
        var[Str] word = this.slice(start, this.pos);
        if (word == "true") return jsonBool(true);
        if (word == "false") return jsonBool(false);
        if (word == "nil") return jsonNull();
        this.pos = start;
        this.fail(word.isEmpty() ? $"unexpected '{c}'" : $"unexpected '{word}' (text values need quotes: \"{word}\")");
        return jsonNull();
    }

    // {Col, Col} [ v, v; v, v; ]  ->  a list of objects
    func[Json] table() {
        this.expectChar('{');
        var[Str<>] columns = [];
        loop {
            this.skip();
            if (this.peek() == '}') {
                this.pos++;
                break;
            }
            columns.push(this.key());
            this.skip();
            if (this.peek() == ',') this.pos++;
        }
        if (columns.isEmpty()) this.fail("a table needs at least one column");

        this.expectChar('[');
        var[Json] rows = jsonArray();
        var[Json] row = jsonObject();
        var[Int] column = 0;
        loop {
            this.skip();
            if (this.peek() == ']') {
                this.pos++;
                if (column != 0) this.fail($"the last row has {column} value(s) but the table has {columns.length()} column(s); end rows with ';'");
                return rows;
            }
            if (this.atEnd()) this.fail("expected ']' to close the table");
            if (column >= columns.length()) this.fail($"this row has more values than the {columns.length()} column(s)");
            row.set(columns<column>, this.value());
            column++;
            this.skip();
            if (this.peek() == ',') {
                this.pos++;
            } else if (this.peek() == ';') {
                this.pos++;
                if (column != columns.length()) {
                    this.fail($"this row has {column} value(s) but the table has {columns.length()} column(s)");
                }
                rows.push(row);
                row = jsonObject();
                column = 0;
            } else if (this.peek() != ']') {
                this.fail($"expected ',' or ';' in the table, found '{this.peek()}'");
            }
        }
        return rows;
    }

    func[Json] number() {
        var[Int] start = this.pos;
        if (this.peek() == '-') this.pos++;
        while (!this.atEnd() && (this.peek().isDigit() || this.peek() == '.')) this.pos++;
        var[Str] digits = this.slice(start, this.pos);
        try {
            return jsonNum(Dbl(digits));
        } catch (e[Str]) {
            this.pos = start;
            this.fail($"'{digits}' is not a valid number");
        }
        return jsonNull();
    }

    func[Str] string() {
        this.expectChar('"');
        var[Str] out = "";
        loop {
            if (this.atEnd() || this.peek() == '\n') this.fail("unfinished string");
            var[Chr] c = this.peek();
            this.pos++;
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            var[Chr] e = this.peek();
            this.pos++;
            switch (e) {
                case 'n' := { out += "\n"; }
                case 't' := { out += "\t"; }
                case '"' := { out += "\""; }
                case '\\' := { out += "\\"; }
                default := { this.fail($"unknown escape '\\{e}'"); }
            }
        }
    }
}

// Reads a PHL document into a Json object. Throws PhlError on mistakes.
func[Json] parsePhl(text[Str]) {
    return new PhlParser(text).entries('\0');
}

// ============================================================
// Writing
// ============================================================

func[Str] phlKey(name[Str]) {
    var[Bul] plain = !name.isEmpty();
    for (var[Chr] c in name) {
        if (!(c.isAlpha() || c.isDigit() || c == '_' || c == '-')) plain = false;
    }
    return plain ? name : phlString(name);
}

func[Str] phlString(text[Str]) {
    return "\"" + text.replaceAll("\\", "\\\\").replaceAll("\"", "\\\"").replaceAll("\n", "\\n") + "\"";
}

func[Str] phlValue(value[Json], depth[Int]) {
    switch (value.kind) {
        case JsonKind.NULL := { return "nil"; }
        case JsonKind.BOOL := { return value.boolValue ? "true" : "false"; }
        case JsonKind.NUMBER := { return Str(value.numberValue); }
        case JsonKind.STRING := { return phlString(value.stringValue); }
        case JsonKind.OBJECT := {
            var[Str<>] parts = [];
            for (var[Str] name in value.fields) {
                parts.push($"{phlKey(name)}: {phlValue(value.fields<name>, depth + 1)}");
            }
            return "[" + parts.join(", ") + "]";
        }
        default := {}
    }
    // A list is written as a table: every item must be an object with the same keys.
    if (value.items.isEmpty()) {
        throw new PhlError("PHL has no empty lists; a table needs at least one row to know its columns");
    }
    var[Str<>] columns = [];
    for (var[Json] item in value.items) {
        if (!item.isObject()) {
            throw new PhlError("PHL lists must be tables (a list of objects with the same keys)");
        }
        if (columns.isEmpty()) columns = item.keys();
        if (item.keys() != columns) {
            throw new PhlError("every row of a PHL table must have the same keys");
        }
    }
    var[Str] indent = "    ".repeat(depth + 1);
    var[Str] out = "{" + columns.map((c[Str]) := phlKey(c)).join(", ") + "} [\n";
    for (var[Json] item in value.items) {
        var[Str<>] cells = [];
        for (var[Str] column in columns) cells.push(phlValue(item.get(column), depth + 1));
        out += indent + cells.join(", ") + ";\n";
    }
    return out + "    ".repeat(depth) + "]";
}

// Writes a Json object as a PHL document (one `key: value` per line).
func[Str] toPhl(document[Json]) {
    if (!document.isObject()) {
        throw new PhlError("a PHL document must be an object of key: value entries");
    }
    var[Str] out = "";
    for (var[Str] name in document.fields) {
        out += $"{phlKey(name)}: {phlValue(document.fields<name>, 0)}\n";
    }
    return out;
}
