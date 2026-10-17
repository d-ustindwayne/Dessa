/***
 * $(<json>) -- read and write JSON. Written in Dessa.
 *
 *     $(<json>);
 *     var[Json] data = parseJson("{\"name\": \"Ann\", \"scores\": [90, 85]}");
 *     print(data.get("name").asStr());           // Ann
 *     print(data.get("scores").at(1).asInt());   // 85
 *
 *     var[Json] user = jsonObject()
 *         .set("name", jsonStr("Bo"))
 *         .set("age", jsonNum(30))
 *         .set("tags", jsonArray(jsonStr("admin"), jsonStr("dev")));
 *     print(user);                                // {"name":"Bo","age":30,"tags":["admin","dev"]}
 *     print(user.pretty());                       // indented over several lines
 *
 * A Json value is one of: null, a boolean, a number, a string, an array
 * (a list of Json) or an object (Str keys -> Json, in insertion order).
 */

enum JsonKind {
    NULL,
    BOOL,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT
}

// Thrown by parseJson for malformed text, and by the as...() / at() / get()
// methods when a value isn't what you asked for.
class JsonError {
    var[Str] message = "";
    init(message[Str]) {
        self.message = message;
    }
    func[Str] toStr() {
        return $"JsonError: {self.message}";
    }
}

class Json {
    var[JsonKind] kind = JsonKind.NULL;
    var[Bul] boolValue = false;
    var[Dbl] numberValue = 0.0;
    var[Str] stringValue = "";
    var[Json<>] items = [];
    var[Map[Str, Json]] fields = {:};

    // ---- What is it? ----
    func[Bul] isNull() { return self.kind == JsonKind.NULL; }
    func[Bul] isBool() { return self.kind == JsonKind.BOOL; }
    func[Bul] isNumber() { return self.kind == JsonKind.NUMBER; }
    func[Bul] isString() { return self.kind == JsonKind.STRING; }
    func[Bul] isArray() { return self.kind == JsonKind.ARRAY; }
    func[Bul] isObject() { return self.kind == JsonKind.OBJECT; }

    func[Str] kindName() {
        return self.kind.name().lower();
    }

    func[Emp] expect(kind[JsonKind], wanted[Str]) {
        if (self.kind != kind) {
            throw new JsonError($"expected {wanted}, found {self.kindName()}");
        }
    }

    // ---- Reading values ----
    func[Str] asStr() {
        self.expect(JsonKind.STRING, "a string");
        return self.stringValue;
    }

    func[Dbl] asDbl() {
        self.expect(JsonKind.NUMBER, "a number");
        return self.numberValue;
    }

    func[Int] asInt() {
        self.expect(JsonKind.NUMBER, "a number");
        return Int(self.numberValue);
    }

    func[Bul] asBul() {
        self.expect(JsonKind.BOOL, "true or false");
        return self.boolValue;
    }

    // An object's field; a JSON null if the key isn't there, so lookups can
    // be chained: data.get("user").get("name").asStr()
    func[Json] get(key[Str]) {
        if (self.isNull()) return self;
        self.expect(JsonKind.OBJECT, $"an object (looking up \"{key}\")");
        return self.fields.get(key) ?? jsonNull();
    }

    func[Bul] has(key[Str]) {
        return self.isObject() && self.fields.has(key);
    }

    // An array's item.
    func[Json] at(index[Int]) {
        self.expect(JsonKind.ARRAY, "an array");
        if (index < 0 || index >= self.items.length()) {
            throw new JsonError($"index {index} is out of range for an array of length {self.items.length()}");
        }
        return self.items<index>;
    }

    // Items of an array, characters of a string, or fields of an object.
    func[Int] length() {
        if (self.isArray()) return self.items.length();
        if (self.isObject()) return self.fields.length();
        if (self.isString()) return self.stringValue.length();
        throw new JsonError($"{self.kindName()} has no length");
    }

    func[Str<>] keys() {
        self.expect(JsonKind.OBJECT, "an object");
        return self.fields.keys();
    }

    // ---- Changing values (each returns the value itself, for chaining) ----
    func[Json] set(key[Str], value[Json]) {
        self.expect(JsonKind.OBJECT, "an object");
        self.fields<key> = value;
        return self;
    }

    func[Json] remove(key[Str]) {
        self.expect(JsonKind.OBJECT, "an object");
        self.fields.remove(key);
        return self;
    }

    func[Json] push(value[Json]) {
        self.expect(JsonKind.ARRAY, "an array");
        self.items.push(value);
        return self;
    }

    // ---- Writing JSON ----
    // Compact JSON text. print(json) uses this.
    func[Str] toStr() {
        return writeJson(self, 0, 0);
    }

    // JSON text spread over lines, indented by `indent` spaces per level.
    func[Str] pretty(indent[Int]=2) {
        return writeJson(self, indent, 0);
    }
}

// ============================================================
// Building values
// ============================================================

func[Json] jsonNull() {
    return new Json();
}

func[Json] jsonBool(value[Bul]) {
    var[Json] j = new Json();
    j.kind = JsonKind.BOOL;
    j.boolValue = value;
    return j;
}

func[Json] jsonNum(value[Dbl]) {
    var[Json] j = new Json();
    j.kind = JsonKind.NUMBER;
    j.numberValue = value;
    return j;
}

func[Json] jsonStr(value[Str]) {
    var[Json] j = new Json();
    j.kind = JsonKind.STRING;
    j.stringValue = value;
    return j;
}

func[Json] jsonArray(items[Json*]) {
    var[Json] j = new Json();
    j.kind = JsonKind.ARRAY;
    j.items = items;
    return j;
}

func[Json] jsonObject() {
    var[Json] j = new Json();
    j.kind = JsonKind.OBJECT;
    return j;
}

// ============================================================
// Writing
// ============================================================

func[Str] hexDigit(n[Int]) {
    return "0123456789abcdef".substr(n, 1);
}

func[Str] quoteJson(text[Str]) {
    var[Str] out = "\"";
    for (var[Chr] c in text) {
        var[Int] code = c.code();
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (code < 32) {
            var[Int] high = code / 16;
            out += $"\\u00{hexDigit(high)}{hexDigit(code % 16)}";
        } else {
            out += c;
        }
    }
    return out + "\"";
}

func[Str] writeJson(value[Json], indent[Int], depth[Int]) {
    switch (value.kind) {
        case JsonKind.NULL := { return "null"; }
        case JsonKind.BOOL := { return value.boolValue ? "true" : "false"; }
        case JsonKind.NUMBER := { return Str(value.numberValue); }
        case JsonKind.STRING := { return quoteJson(value.stringValue); }
        default := {}
    }

    var[Bul] isArray = value.kind == JsonKind.ARRAY;
    var[Int] count = isArray ? value.items.length() : value.fields.length();
    var[Str] open = isArray ? "[" : "{";
    var[Str] close = isArray ? "]" : "}";
    if (count == 0) return open + close;

    var[Str] newline = indent > 0 ? "\n" : "";
    var[Str] inner = " ".repeat(indent * (depth + 1));
    var[Str] outer = " ".repeat(indent * depth);
    var[Str] colon = indent > 0 ? ": " : ":";

    var[Str<>] parts = [];
    if (isArray) {
        for (var[Json] item in value.items) {
            parts.push(inner + writeJson(item, indent, depth + 1));
        }
    } else {
        for (var[Str] key in value.fields) {
            parts.push(inner + quoteJson(key) + colon + writeJson(value.fields<key>, indent, depth + 1));
        }
    }
    return open + newline + parts.join("," + newline) + newline + outer + close;
}

// Same as value.toStr() (indent 0) / value.pretty(indent).
func[Str] stringify(value[Json], indent[Int]=0) {
    return writeJson(value, indent, 0);
}

// ============================================================
// Reading
// ============================================================

class JsonParser {
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
        // line and column of `pos`, counting from 1
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
        throw new JsonError($"{message} at line {line}, column {column}");
    }

    func[Bul] atEnd() {
        return self.pos >= self.chars.length();
    }

    func[Chr] peek() {
        if (self.atEnd()) return '\0';
        return self.chars<self.pos>;
    }

    func[Emp] skipSpace() {
        while (!self.atEnd() && self.peek().isSpace()) self.pos++;
    }

    func[Emp] expectChar(c[Chr]) {
        self.skipSpace();
        if (self.peek() != c) {
            self.fail(self.atEnd() ? $"expected '{c}' but the text ended" : $"expected '{c}', found '{self.peek()}'");
        }
        self.pos++;
    }

    func[Bul] tryWord(word[Str]) {
        if (self.pos + word.length() <= self.chars.length() && self.slice(self.pos, self.pos + word.length()) == word) {
            self.pos += word.length();
            return true;
        }
        return false;
    }

    func[Json] value() {
        self.skipSpace();
        if (self.atEnd()) self.fail("expected a value but the text ended");
        var[Chr] c = self.peek();
        if (c == '{') return self.object();
        if (c == '[') return self.array();
        if (c == '"') return jsonStr(self.string());
        if (c == '-' || c.isDigit()) return self.number();
        if (self.tryWord("true")) return jsonBool(true);
        if (self.tryWord("false")) return jsonBool(false);
        if (self.tryWord("null")) return jsonNull();
        self.fail($"unexpected character '{c}'");
        return jsonNull();
    }

    func[Json] object() {
        var[Json] result = jsonObject();
        self.expectChar('{');
        self.skipSpace();
        if (self.peek() == '}') {
            self.pos++;
            return result;
        }
        loop {
            self.skipSpace();
            if (self.peek() != '"') self.fail("expected a \"key\" in the object");
            var[Str] key = self.string();
            self.expectChar(':');
            result.fields<key> = self.value();
            self.skipSpace();
            if (self.peek() == ',') {
                self.pos++;
                continue;
            }
            self.expectChar('}');
            return result;
        }
    }

    func[Json] array() {
        var[Json] result = jsonArray();
        self.expectChar('[');
        self.skipSpace();
        if (self.peek() == ']') {
            self.pos++;
            return result;
        }
        loop {
            result.items.push(self.value());
            self.skipSpace();
            if (self.peek() == ',') {
                self.pos++;
                continue;
            }
            self.expectChar(']');
            return result;
        }
    }

    func[Json] number() {
        var[Int] start = self.pos;
        if (self.peek() == '-') self.pos++;
        while (!self.atEnd()) {
            var[Chr] c = self.peek();
            if (c.isDigit() || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                self.pos++;
            } else {
                break;
            }
        }
        var[Str] digits = self.slice(start, self.pos);
        try {
            return jsonNum(Dbl(digits));
        } catch (e[Str]) {
            self.pos = start;
            self.fail($"'{digits}' is not a valid number");
        }
        return jsonNull();
    }

    func[Int] hexValue(c[Chr]) {
        if (c.isDigit()) return c.code() - 48;
        var[Chr] lower = c.lower();
        if (lower >= 'a' && lower <= 'f') return lower.code() - 87;
        self.fail($"'{c}' is not a hex digit");
        return 0;
    }

    func[Int] hex4() {
        var[Int] value = 0;
        for (var[Int] i = 0; i < 4; i++) {
            if (self.atEnd()) self.fail("unfinished \\u escape");
            value = value * 16 + self.hexValue(self.peek());
            self.pos++;
        }
        return value;
    }


    func[Str] string() {
        self.expectChar('"');
        var[Str] out = "";
        loop {
            if (self.atEnd()) self.fail("unfinished string");
            var[Chr] c = self.peek();
            self.pos++;
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (self.atEnd()) self.fail("unfinished escape");
            var[Chr] e = self.peek();
            self.pos++;
            switch (e) {
                case '"' := { out += "\""; }
                case '\\' := { out += "\\"; }
                case '/' := { out += "/"; }
                case 'b' := { out += Chr(8); }
                case 'f' := { out += Chr(12); }
                case 'n' := { out += "\n"; }
                case 'r' := { out += "\r"; }
                case 't' := { out += "\t"; }
                case 'u' := {
                    var[Int] code = self.hex4();
                    // a surrogate pair encodes one character above U+FFFF
                    if (code >= 55296 && code <= 56319 && self.tryWord("\\u")) {
                        var[Int] low = self.hex4();
                        code = 65536 + (code - 55296) * 1024 + (low - 56320);
                    }
                    out += Chr(code);
                }
                default := { self.fail($"unknown escape '\\{e}'"); }
            }
        }
    }
}

// Reads JSON text. Throws JsonError (with the line and column) if it isn't valid.
func[Json] parseJson(text[Str]) {
    var[JsonParser] parser = new JsonParser(text);
    var[Json] result = parser.value();
    parser.skipSpace();
    if (!parser.atEnd()) parser.fail("unexpected text after the JSON value");
    return result;
}
