/***
 * $(<phl>) -- read and write PHL, Dessa's data format. Written in Dessa.
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
 * Values are read into the same Json type as $(<json>), so PHL and JSON
 * convert into each other:
 *
 *     $(<phl>);
 *     var[Json] doc = parsePhl(readFile("config.phl"));
 *     print(doc.get("product").at(3).get("ID").asInt());   // 104
 *     print(toPhl(doc));                                   // back to PHL text
 */

{ Json, JsonKind, jsonNull, jsonBool, jsonNum, jsonStr, jsonArray, jsonObject } from $(<json>);

class PhlError {
    var[Str] message = "";
    init(message[Str]) {
        self.message = message;
    }
    func[Str] toStr() {
        return $"PhlError: {self.message}";
    }
}

class PhlParser {
    var[Str] text = "";
    var[Chr<>] chars = [];   // the text as characters, for fast indexing
    var[Int] pos = 0;

    init(text[Str]) {
        self.text = text;
        self.chars = text.chars();
    }

    // Characters start..end (not included) as text
    func[Str] slice(start[Int], end[Int]) {
        return self.chars.slice(start, end).join("");
    }

    func[Emp] fail(message[Str]) {
        var[Int] line = 1;
        var[Int] column = 1;
        for (var[Int] i = 0; i < self.pos && i < self.chars.length(); i++) {
            if (self.chars<i> == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }
        throw new PhlError($"{message} at line {line}, column {column}");
    }

    func[Bul] atEnd() {
        return self.pos >= self.chars.length();
    }

    func[Chr] peek() {
        if (self.atEnd()) return '\0';
        return self.chars<self.pos>;
    }

    // Skips spaces, newlines and // comments.
    func[Emp] skip() {
        while (!self.atEnd()) {
            var[Chr] c = self.peek();
            if (c.isSpace()) {
                self.pos++;
            } else if (c == '/' && self.pos + 1 < self.chars.length() && self.chars<self.pos + 1> == '/') {
                while (!self.atEnd() && self.peek() != '\n') self.pos++;
            } else {
                return;
            }
        }
    }

    func[Emp] expectChar(c[Chr]) {
        self.skip();
        if (self.peek() != c) {
            self.fail(self.atEnd() ? $"expected '{c}' but the text ended" : $"expected '{c}', found '{self.peek()}'");
        }
        self.pos++;
    }

    func[Bul] isKeyChar(c[Chr]) {
        return c.isAlpha() || c.isDigit() || c == '_' || c == '-';
    }

    // A key: Name, server_name, or "any text"
    func[Str] key() {
        self.skip();
        if (self.peek() == '"') return self.string();
        var[Int] start = self.pos;
        while (!self.atEnd() && self.isKeyChar(self.peek())) self.pos++;
        if (self.pos == start) {
            self.fail(self.atEnd() ? "expected a key but the text ended"
                                   : $"expected a key, found '{self.peek()}' (every value needs a key: name: value)");
        }
        return self.slice(start, self.pos);
    }

    // key: value, key: value ...  until `close` ('\0' = end of the text)
    func[Json] entries(close[Chr]) {
        var[Json] result = jsonObject();
        loop {
            self.skip();
            if (close == '\0' && self.atEnd()) return result;
            if (close != '\0' && self.peek() == close) {
                self.pos++;
                return result;
            }
            if (self.atEnd()) self.fail($"expected '{close}' but the text ended");
            var[Str] name = self.key();
            if (result.has(name)) self.fail($"key '{name}' is used twice");
            self.skip();
            if (self.peek() != ':') {
                self.fail($"'{name}' needs a key and a colon: every value is written as key: value");
            }
            self.pos++;
            result.set(name, self.value());
            self.skip();
            if (self.peek() == ',') self.pos++;   // optional between entries, allowed at the end
        }
    }

    func[Json] value() {
        self.skip();
        if (self.atEnd()) self.fail("expected a value but the text ended");
        var[Chr] c = self.peek();
        if (c == '"') return jsonStr(self.string());
        if (c == '[') {
            self.pos++;
            return self.entries(']');
        }
        if (c == '{') return self.table();
        if (c == '-' || c.isDigit()) return self.number();
        var[Int] start = self.pos;
        while (!self.atEnd() && self.isKeyChar(self.peek())) self.pos++;
        var[Str] word = self.slice(start, self.pos);
        if (word == "true") return jsonBool(true);
        if (word == "false") return jsonBool(false);
        if (word == "nil") return jsonNull();
        self.pos = start;
        self.fail(word.isEmpty() ? $"unexpected '{c}'" : $"unexpected '{word}' (text values need quotes: \"{word}\")");
        return jsonNull();
    }

    // {Col, Col} [ v, v; v, v; ]  ->  a list of objects
    func[Json] table() {
        self.expectChar('{');
        var[Str<>] columns = [];
        loop {
            self.skip();
            if (self.peek() == '}') {
                self.pos++;
                break;
            }
            columns.push(self.key());
            self.skip();
            if (self.peek() == ',') self.pos++;
        }
        if (columns.isEmpty()) self.fail("a table needs at least one column");

        self.expectChar('[');
        var[Json] rows = jsonArray();
        var[Json] row = jsonObject();
        var[Int] column = 0;
        loop {
            self.skip();
            if (self.peek() == ']') {
                self.pos++;
                if (column != 0) self.fail($"the last row has {column} value(s) but the table has {columns.length()} column(s); end rows with ';'");
                return rows;
            }
            if (self.atEnd()) self.fail("expected ']' to close the table");
            if (column >= columns.length()) self.fail($"this row has more values than the {columns.length()} column(s)");
            row.set(columns<column>, self.value());
            column++;
            self.skip();
            if (self.peek() == ',') {
                self.pos++;
            } else if (self.peek() == ';') {
                self.pos++;
                if (column != columns.length()) {
                    self.fail($"this row has {column} value(s) but the table has {columns.length()} column(s)");
                }
                rows.push(row);
                row = jsonObject();
                column = 0;
            } else if (self.peek() != ']') {
                self.fail($"expected ',' or ';' in the table, found '{self.peek()}'");
            }
        }
        return rows;
    }

    func[Json] number() {
        var[Int] start = self.pos;
        if (self.peek() == '-') self.pos++;
        while (!self.atEnd() && (self.peek().isDigit() || self.peek() == '.')) self.pos++;
        var[Str] digits = self.slice(start, self.pos);
        try {
            return jsonNum(Dbl(digits));
        } catch (e[Str]) {
            self.pos = start;
            self.fail($"'{digits}' is not a valid number");
        }
        return jsonNull();
    }

    func[Str] string() {
        self.expectChar('"');
        var[Str] out = "";
        loop {
            if (self.atEnd() || self.peek() == '\n') self.fail("unfinished string");
            var[Chr] c = self.peek();
            self.pos++;
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            var[Chr] e = self.peek();
            self.pos++;
            switch (e) {
                case 'n' := { out += "\n"; }
                case 't' := { out += "\t"; }
                case '"' := { out += "\""; }
                case '\\' := { out += "\\"; }
                default := { self.fail($"unknown escape '\\{e}'"); }
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
