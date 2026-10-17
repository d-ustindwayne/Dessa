/***
 * $("json") -- read and write JSON. Written in Dessa.
 *
 *     $("json");
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
        this.message = message;
    }
    func[Str] toStr() {
        return $"JsonError: {this.message}";
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
    func[Bul] isNull() { return this.kind == JsonKind.NULL; }
    func[Bul] isBool() { return this.kind == JsonKind.BOOL; }
    func[Bul] isNumber() { return this.kind == JsonKind.NUMBER; }
    func[Bul] isString() { return this.kind == JsonKind.STRING; }
    func[Bul] isArray() { return this.kind == JsonKind.ARRAY; }
    func[Bul] isObject() { return this.kind == JsonKind.OBJECT; }

    func[Str] kindName() {
        return this.kind.name().lower();
    }

    func[Emp] expect(kind[JsonKind], wanted[Str]) {
        if (this.kind != kind) {
            throw new JsonError($"expected {wanted}, found {this.kindName()}");
        }
    }

    // ---- Reading values ----
    func[Str] asStr() {
        this.expect(JsonKind.STRING, "a string");
        return this.stringValue;
    }

    func[Dbl] asDbl() {
        this.expect(JsonKind.NUMBER, "a number");
        return this.numberValue;
    }

    func[Int] asInt() {
        this.expect(JsonKind.NUMBER, "a number");
        return Int(this.numberValue);
    }

    func[Bul] asBul() {
        this.expect(JsonKind.BOOL, "true or false");
        return this.boolValue;
    }

    // An object's field; a JSON null if the key isn't there, so lookups can
    // be chained: data.get("user").get("name").asStr()
    func[Json] get(key[Str]) {
        if (this.isNull()) return this;
        this.expect(JsonKind.OBJECT, $"an object (looking up \"{key}\")");
        return this.fields.get(key) ?? jsonNull();
    }

    func[Bul] has(key[Str]) {
        return this.isObject() && this.fields.has(key);
    }

    // An array's item.
    func[Json] at(index[Int]) {
        this.expect(JsonKind.ARRAY, "an array");
        if (index < 0 || index >= this.items.length()) {
            throw new JsonError($"index {index} is out of range for an array of length {this.items.length()}");
        }
        return this.items<index>;
    }

    // Items of an array, characters of a string, or fields of an object.
    func[Int] length() {
        if (this.isArray()) return this.items.length();
        if (this.isObject()) return this.fields.length();
        if (this.isString()) return this.stringValue.length();
        throw new JsonError($"{this.kindName()} has no length");
    }

    func[Str<>] keys() {
        this.expect(JsonKind.OBJECT, "an object");
        return this.fields.keys();
    }

    // ---- Changing values (each returns the value itself, for chaining) ----
    func[Json] set(key[Str], value[Json]) {
        this.expect(JsonKind.OBJECT, "an object");
        this.fields<key> = value;
        return this;
    }

    func[Json] remove(key[Str]) {
        this.expect(JsonKind.OBJECT, "an object");
        this.fields.remove(key);
        return this;
    }

    func[Json] push(value[Json]) {
        this.expect(JsonKind.ARRAY, "an array");
        this.items.push(value);
        return this;
    }

    // ---- Writing JSON ----
    // Compact JSON text. print(json) uses this.
    func[Str] toStr() {
        return writeJson(this, 0, 0);
    }

    // JSON text spread over lines, indented by `indent` spaces per level.
    func[Str] pretty(indent[Int]=2) {
        return writeJson(this, indent, 0);
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
        this.text = text;
        this.chars = text.chars();
    }

    // Characters start..end (not included) as text
    func[Str] slice(start[Int], end[Int]) {
        return this.chars.slice(start, end).join("");
    }

    func[Emp] fail(message[Str]) {
        // line and column of `pos`, counting from 1
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
        throw new JsonError($"{message} at line {line}, column {column}");
    }

    func[Bul] atEnd() {
        return this.pos >= this.chars.length();
    }

    func[Chr] peek() {
        if (this.atEnd()) return '\0';
        return this.chars<this.pos>;
    }

    func[Emp] skipSpace() {
        while (!this.atEnd() && this.peek().isSpace()) this.pos++;
    }

    func[Emp] expectChar(c[Chr]) {
        this.skipSpace();
        if (this.peek() != c) {
            this.fail(this.atEnd() ? $"expected '{c}' but the text ended" : $"expected '{c}', found '{this.peek()}'");
        }
        this.pos++;
    }

    func[Bul] tryWord(word[Str]) {
        if (this.pos + word.length() <= this.chars.length() && this.slice(this.pos, this.pos + word.length()) == word) {
            this.pos += word.length();
            return true;
        }
        return false;
    }

    func[Json] value() {
        this.skipSpace();
        if (this.atEnd()) this.fail("expected a value but the text ended");
        var[Chr] c = this.peek();
        if (c == '{') return this.object();
        if (c == '[') return this.array();
        if (c == '"') return jsonStr(this.string());
        if (c == '-' || c.isDigit()) return this.number();
        if (this.tryWord("true")) return jsonBool(true);
        if (this.tryWord("false")) return jsonBool(false);
        if (this.tryWord("null")) return jsonNull();
        this.fail($"unexpected character '{c}'");
        return jsonNull();
    }

    func[Json] object() {
        var[Json] result = jsonObject();
        this.expectChar('{');
        this.skipSpace();
        if (this.peek() == '}') {
            this.pos++;
            return result;
        }
        loop {
            this.skipSpace();
            if (this.peek() != '"') this.fail("expected a \"key\" in the object");
            var[Str] key = this.string();
            this.expectChar(':');
            result.fields<key> = this.value();
            this.skipSpace();
            if (this.peek() == ',') {
                this.pos++;
                continue;
            }
            this.expectChar('}');
            return result;
        }
    }

    func[Json] array() {
        var[Json] result = jsonArray();
        this.expectChar('[');
        this.skipSpace();
        if (this.peek() == ']') {
            this.pos++;
            return result;
        }
        loop {
            result.items.push(this.value());
            this.skipSpace();
            if (this.peek() == ',') {
                this.pos++;
                continue;
            }
            this.expectChar(']');
            return result;
        }
    }

    func[Json] number() {
        var[Int] start = this.pos;
        if (this.peek() == '-') this.pos++;
        while (!this.atEnd()) {
            var[Chr] c = this.peek();
            if (c.isDigit() || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                this.pos++;
            } else {
                break;
            }
        }
        var[Str] digits = this.slice(start, this.pos);
        try {
            return jsonNum(Dbl(digits));
        } catch (e[Str]) {
            this.pos = start;
            this.fail($"'{digits}' is not a valid number");
        }
        return jsonNull();
    }

    func[Int] hexValue(c[Chr]) {
        if (c.isDigit()) return c.code() - 48;
        var[Chr] lower = c.lower();
        if (lower >= 'a' && lower <= 'f') return lower.code() - 87;
        this.fail($"'{c}' is not a hex digit");
        return 0;
    }

    func[Int] hex4() {
        var[Int] value = 0;
        for (var[Int] i = 0; i < 4; i++) {
            if (this.atEnd()) this.fail("unfinished \\u escape");
            value = value * 16 + this.hexValue(this.peek());
            this.pos++;
        }
        return value;
    }


    func[Str] string() {
        this.expectChar('"');
        var[Str] out = "";
        loop {
            if (this.atEnd()) this.fail("unfinished string");
            var[Chr] c = this.peek();
            this.pos++;
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (this.atEnd()) this.fail("unfinished escape");
            var[Chr] e = this.peek();
            this.pos++;
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
                    var[Int] code = this.hex4();
                    // a surrogate pair encodes one character above U+FFFF
                    if (code >= 55296 && code <= 56319 && this.tryWord("\\u")) {
                        var[Int] low = this.hex4();
                        code = 65536 + (code - 55296) * 1024 + (low - 56320);
                    }
                    out += Chr(code);
                }
                default := { this.fail($"unknown escape '\\{e}'"); }
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
