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
 *
 * Numbers keep their exact text: asInt and asBig give every digit back, and
 * writing gives the number as it came, however long.
 *
 *     data.path("user.tags.0").asStr()       // through objects and arrays
 *     data.intOr("age", 0)                   // a default when the key is missing
 *     base.merge(changes)                    // objects, deeply
 *     parseJsonLines(text)                   // one value per line
 *     new JsonSchema(schemaText).check(data) // what is wrong with it, as a list
 *
 * Text from elsewhere can't crash a program: nesting deeper than 512 levels is
 * a JsonError (parseJson(text, maxDepth) changes that).
 */

$("math") { Big }

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
class JsonError(Error) {}   // an Error: e.message, prints as JsonError: ...

class Json {
    var[JsonKind] kind = JsonKind.NULL;
    var[Bul] boolValue = false;
    var[Dbl] numberValue = 0.0;
    var[Str] numberText = "";      // a number as it was written: all its digits
    var[Str] stringValue = "";
    var[Json<>] items = <>;
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

    // Exact for whole numbers. Past what an Int holds: a JsonError (see asBig).
    func[Int] asInt() {
        this.expect(JsonKind.NUMBER, "a number");
        if (!_isWhole(this.numberText)) return Int(this.numberValue);
        try {
            return Int(this.numberText);
        } catch {
            throw new JsonError($"{this.numberText} is too big for an Int (use asBig)");
        }
    }

    // A whole number of any size, every digit. A JsonError if it has a fraction.
    func[Big] asBig() {
        this.expect(JsonKind.NUMBER, "a number");
        if (_isWhole(this.numberText)) return new Big(this.numberText);
        if (this.numberValue != Dbl(Int(this.numberValue))) throw new JsonError($"{this.numberText} is not a whole number");
        return new Big(Int(this.numberValue));
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

    // Through objects and arrays at once: data.path("users.0.name"). A JSON null
    // if any part isn't there.
    func[Json] path(where[Str]) {
        var[Json] current = this;
        for (var[Str] part in where.split(".")) {
            if (current.isObject()) {
                current = current.fields.get(part) ?? jsonNull();
            } else if (current.isArray() && _isWhole(part) && !part.startsWith("-") && part.length() < 18 && Int(part) < current.items.length()) {
                current = current.items<Int(part)>;
            } else {
                return jsonNull();
            }
        }
        return current;
    }

    // A field, or `fallback` if it is missing or null. A value of the wrong
    // kind is still a JsonError: that's a mistake, not a default.
    func[Str] strOr(key[Str], fallback[Str]) {
        var[Json] v = this.get(key);
        return v.isNull() ? fallback : v.asStr();
    }
    func[Int] intOr(key[Str], fallback[Int]) {
        var[Json] v = this.get(key);
        return v.isNull() ? fallback : v.asInt();
    }
    func[Dbl] dblOr(key[Str], fallback[Dbl]) {
        var[Json] v = this.get(key);
        return v.isNull() ? fallback : v.asDbl();
    }
    func[Bul] bulOr(key[Str], fallback[Bul]) {
        var[Json] v = this.get(key);
        return v.isNull() ? fallback : v.asBul();
    }

    // An array's item.
    func[Json] at(index[Int]) {
        this.expect(JsonKind.ARRAY, "an array");
        if (index < 0 || index >= this.items.length()) {
            throw new JsonError($"index {index} is out of range for an array of length {this.items.length()}");
        }
        return this.items<index>;
    }

    // An array's items.
    func[Json<>] arrayItems() {
        this.expect(JsonKind.ARRAY, "an array");
        return this.items;
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

    // Adds another object's fields, replacing what is there; objects inside
    // both are merged in turn. Changes this one.
    func[Json] merge(other[Json]) {
        this.expect(JsonKind.OBJECT, "an object");
        other.expect(JsonKind.OBJECT, "an object");
        for (var[Str] key in other.fields) {
            var[Json] incoming = other.fields<key>;
            var[Json] current = this.fields.get(key) ?? jsonNull();
            if (current.isObject() && incoming.isObject()) current.merge(incoming);
            else this.fields<key> = incoming.copy();
        }
        return this;
    }

    // A separate copy, all the way down
    func[Json] copy() {
        var[Json] out = new Json();
        out.kind = this.kind;
        out.boolValue = this.boolValue;
        out.numberValue = this.numberValue;
        out.numberText = this.numberText;
        out.stringValue = this.stringValue;
        for (var[Json] item in this.items) out.items.push(item.copy());
        for (var[Str] key in this.fields) out.fields<key> = this.fields<key>.copy();
        return out;
    }

    // The same value, all the way down. The order of an object's keys doesn't
    // matter; 1 and 1.0 are the same number.
    func[Bul] _eq(other[Json]) {
        if (this.kind != other.kind) return false;
        switch (this.kind) {
            case JsonKind.NULL := { return true; }
            case JsonKind.BOOL := { return this.boolValue == other.boolValue; }
            case JsonKind.STRING := { return this.stringValue == other.stringValue; }
            case JsonKind.NUMBER := {
                if (_isWhole(this.numberText) && _isWhole(other.numberText)) return new Big(this.numberText) == new Big(other.numberText);
                return this.numberValue == other.numberValue;
            }
            case JsonKind.ARRAY := {
                if (this.items.length() != other.items.length()) return false;
                for (var[Int] i = 0; i < this.items.length(); i++) {
                    if (this.items<i> != other.items<i>) return false;
                }
                return true;
            }
            default := {
                if (this.fields.length() != other.fields.length()) return false;
                for (var[Str] key in this.fields) {
                    if (!other.fields.has(key) || this.fields<key> != other.fields<key>) return false;
                }
                return true;
            }
        }
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

    // ---- Into your classes ----
    // An object as a new T: each field of T gets the key with the same name
    // (case and _ don't matter, so first_name fills firstName). Keys without
    // a field are left out, and so are fields without a key. T needs an
    // init() without arguments. Fields that are Str, Int, Dbl, Flt, Bul, Json
    // or lists of Str, Int, Dbl, Bul are filled.
    //
    //     class User { var[Str] name = ""; var[Int] age = 0; }
    //     var[User] u = parseJson(text).to:User();
    func[T] to:T() {
        this.expect(JsonKind.OBJECT, "an object");
        var[T] item = new T();
        _fillFromJson(item, this);
        return item;
    }

    // An array of objects as a list of T (see to).
    func[T<>] toList:T() {
        this.expect(JsonKind.ARRAY, "an array");
        var[T<>] out = <>;
        for (var[Json] value in this.items) out.push(value.to:T());
        return out;
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

// A whole number, exactly
func[Json] jsonInt(value[Int]) {
    var[Json] j = jsonNum(Dbl(value));
    j.numberText = Str(value);
    return j;
}

// A whole number of any size, exactly
func[Json] jsonBig(value[Big]) {
    var[Json] j = jsonNum(value.toDbl());
    j.numberText = value.toStr();
    return j;
}

// "-12", "12": digits and perhaps a minus (not 1.5, not 1e3)
func[Bul] _isWhole(text[Str]) {
    if (text == "" || text == "-") return false;
    var[Chr<>] all = text.chars();
    for (var[Int] i = 0; i < all.length(); i++) {
        if (!all<i>.isDigit() && !(i == 0 && all<i> == '-')) return false;
    }
    return true;
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
        case JsonKind.NUMBER := {
            if (value.numberText != "") return value.numberText;
            // JSON has no way to write these; null would hide the mistake
            var[Dbl] n = value.numberValue;
            if (n - n != 0) throw new JsonError($"JSON can't hold the number {n}");
            return Str(n);
        }
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

    var[Str<>] parts = <>;
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
    var[Chr<>] chars = <>;   // the text as characters, for fast indexing
    var[Int] pos = 0;
    var[Int] depth = 0;
    var[Int] maxDepth = 512;

    init(text[Str], maxDepth[Int]=512) {
        this.text = text;
        this.chars = text.chars();
        this.maxDepth = maxDepth;
    }

    // Text from elsewhere can't use up the stack: see parseJson
    func[Emp] enter() {
        this.depth++;
        if (this.depth > this.maxDepth) this.fail($"nested more than {this.maxDepth} levels deep");
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
        if (c == '{' || c == '[') {
            this.enter();
            var[Json] inner = c == '{' ? this.object() : this.array();
            this.depth--;
            return inner;
        }
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
        if (!_validNumber(digits)) {
            this.pos = start;
            this.fail($"'{digits}' is not a valid number");
        }
        var[Json] result = jsonNum(0);
        try {
            result.numberValue = Dbl(digits);
        } catch {
            // Too big for a Dbl: the digits are still kept
            if (!digits.lower().includes("e") && digits.includes(".")) {
                this.pos = start;
                this.fail($"'{digits}' is not a valid number");
            }
        }
        result.numberText = digits;
        return result;
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

// As JSON writes numbers: -? digits (no leading zeros), then .digits, then e+digits
func[Bul] _validNumber(text[Str]) {
    var[Chr<>] c = text.chars();
    var[Int] i = 0;
    var[Int] n = c.length();
    if (i < n && c<i> == '-') i++;
    if (i >= n || !c<i>.isDigit()) return false;
    if (c<i> == '0') i++;
    else while (i < n && c<i>.isDigit()) i++;
    if (i < n && c<i> == '.') {
        i++;
        if (i >= n || !c<i>.isDigit()) return false;
        while (i < n && c<i>.isDigit()) i++;
    }
    if (i < n && (c<i> == 'e' || c<i> == 'E')) {
        i++;
        if (i < n && (c<i> == '+' || c<i> == '-')) i++;
        if (i >= n || !c<i>.isDigit()) return false;
        while (i < n && c<i>.isDigit()) i++;
    }
    return i == n;
}

// One JSON value per line (blank lines are skipped). A bad line is a JsonError
// that says which.
func[Json<>] parseJsonLines(text[Str], maxDepth[Int]=512) {
    var[Json<>] out = <>;
    var[Str<>] lines = text.split("\n");
    for (var[Int] i = 0; i < lines.length(); i++) {
        var[Str] line = lines<i>.trim();
        if (line.isEmpty()) continue;
        try {
            out.push(parseJson(line, maxDepth));
        } catch (e[JsonError]) {
            throw new JsonError($"line {i + 1}: {e.message}");
        }
    }
    return out;
}

// The other way round: one value per line, each compact
func[Str] toJsonLines(values[Json<>]) {
    var[Str] out = "";
    for (var[Json] value in values) out += value.toStr() + "\n";
    return out;
}

// Reads JSON text. Throws JsonError (with the line and column) if it isn't valid.
func[Json] parseJson(text[Str], maxDepth[Int]=512) {
    var[JsonParser] parser = new JsonParser(text, maxDepth);
    var[Json] result = parser.value();
    parser.skipSpace();
    if (!parser.atEnd()) parser.fail("unexpected text after the JSON value");
    return result;
}

// JSON text of an object straight into your class (see Json.to):
//
//     var[User] u = parseJsonAs:User("{\"name\": \"Ann\", \"age\": 31}");
func[T] parseJsonAs:T(text[Str]) {
    return parseJson(text).to:T();
}

// JSON text of an array of objects into a list of your class.
func[T<>] parseJsonListAs:T(text[Str]) {
    return parseJson(text).toList:T();
}

// "first_name", "FirstName" and "firstname" are the same name
func[Str] _jsonKey(name[Str]) {
    return name.replaceAll("_", "").lower();
}

// The fields of `item` from the keys of `data` with the same names
func[Emp] _fillFromJson(item[Obj], data[Json]) {
    var[Map[Str, Str]] byKey = {:};
    for (var[Str] key in data.keys()) byKey<_jsonKey(key)> = key;
    for (var[Str] field in fieldsOf(item)) {
        var[Str] key = byKey.getOr(_jsonKey(field), "");
        if (key.isEmpty()) continue;
        var[Json] value = data.get(key);
        if (value.isNull()) continue;
        var[Obj] filled = _jsonValue(value, getField(item, field), key);
        if (filled != nil) setField(item, field, filled);
    }
}

// A JSON value as what a field holding `current` takes (nil: a field it can't fill)
func[Obj] _jsonValue(value[Json], current[Obj], key[Str]) {
    try {
        if (current is Str) return value.asStr();
        if (current is Int) return value.asInt();
        if (current is Dbl) return value.asDbl();
        if (current is Flt) return Flt(value.asDbl());
        if (current is Bul) return value.asBul();
        if (current is Json) return value;
        if (current is Str<>) {
            var[Str<>] out = <>;
            for (var[Json] v in value.arrayItems()) out.push(v.asStr());
            return out;
        }
        if (current is Int<>) {
            var[Int<>] out = <>;
            for (var[Json] v in value.arrayItems()) out.push(v.asInt());
            return out;
        }
        if (current is Dbl<>) {
            var[Dbl<>] out = <>;
            for (var[Json] v in value.arrayItems()) out.push(v.asDbl());
            return out;
        }
        if (current is Bul<>) {
            var[Bul<>] out = <>;
            for (var[Json] v in value.arrayItems()) out.push(v.asBul());
            return out;
        }
    } catch (e[JsonError]) {
        throw new JsonError($"\"{key}\": {e.message}");
    }
    return nil;
}

// Your object (or text, a number, true/false, nil, a list of those) as JSON:
// each field becomes a key
//
//     print(toJson(new User()));      // {"name":"","age":0}
func[Json] toJson(value[Obj]) {
    if (value == nil) return jsonNull();
    if (value is Json) return value;
    if (value is Str) return jsonStr(value);
    if (value is Int) return jsonInt(value);
    if (value is Big) return jsonBig(value);
    if (value is Dbl) return jsonNum(value);
    if (value is Flt) return jsonNum(Dbl(value));
    if (value is Bul) return jsonBool(value);
    var[Json] list = jsonArray();
    if (value is Str<>) {
        for (var[Str] s in value) list.push(jsonStr(s));
        return list;
    }
    if (value is Int<>) {
        for (var[Int] n in value) list.push(jsonInt(n));
        return list;
    }
    if (value is Dbl<>) {
        for (var[Dbl] n in value) list.push(jsonNum(n));
        return list;
    }
    if (value is Bul<>) {
        for (var[Bul] b in value) list.push(jsonBool(b));
        return list;
    }
    var[Json] object = jsonObject();
    for (var[Str] field in fieldsOf(value)) object.set(field, toJson(getField(value, field)));
    return object;
}

// ============================================================
// Checking the shape of a value
// ============================================================

// Checks a value against a schema written as JSON. It understands this much of
// JSON Schema: type (one, or a list), required, properties, additionalProperties
// (false), items, enum, minimum, maximum, minLength, maxLength, minItems, maxItems.
//
//     var[JsonSchema] user = new JsonSchema("""{
//         "type": "object",
//         "required": ["name"],
//         "properties": {"name": {"type": "string"}, "age": {"type": "integer", "minimum": 0}}
//     }""");
//     user.check(data)         // <"age: -1 is below the minimum 0">, or <> when it fits
//     user.isValid(data)
//     user.require(data)       // a JsonError with all of them, for input from elsewhere
class JsonSchema {
    var[Json] schema = jsonObject();
    var[Str<>] _problems = <>;

    init(schema[Str | Json]) {
        if (schema is Str) this.schema = parseJson(schema);
        else if (schema is Json) this.schema = schema;
        this.schema.expect(JsonKind.OBJECT, "a schema object");
    }

    func[Str<>] check(value[Json]) {
        this._problems = <>;
        this._check(this.schema, value, "");
        return this._problems;
    }

    func[Bul] isValid(value[Json]) { return this.check(value).isEmpty(); }

    func[Emp] require(value[Json]) {
        var[Str<>] problems = this.check(value);
        if (!problems.isEmpty()) throw new JsonError(problems.join("; "));
    }

    func[Bul] _isType(value[Json], type[Str]) {
        if (type == "integer") {
            if (!value.isNumber()) return false;
            return _isWhole(value.numberText) || value.numberValue == Dbl(Int(value.numberValue));
        }
        if (type == "boolean") return value.isBool();
        return value.kindName() == type;
    }

    func[Emp] _check(rules[Json], value[Json], where[Str]) {
        var[Str] at = where == "" ? "the value" : where;
        if (rules.has("type")) {
            var[Json] type = rules.get("type");
            var[Str<>] allowed = <>;
            if (type.isString()) allowed.push(type.asStr());
            else for (var[Json] t in type.arrayItems()) allowed.push(t.asStr());
            var[Bul] fits = false;
            for (var[Str] t in allowed) {
                if (this._isType(value, t)) fits = true;
            }
            if (!fits) {
                this._problems.push($"{at}: expected {allowed.join(" or ")}, found {value.kindName()}");
                return;
            }
        }
        if (rules.has("enum")) {
            var[Bul] found = false;
            for (var[Json] option in rules.get("enum").arrayItems()) {
                if (option == value) found = true;
            }
            if (!found) this._problems.push($"{at}: {value} is not one of {rules.get("enum")}");
        }
        if (value.isNumber()) {
            if (rules.has("minimum") && value.asDbl() < rules.get("minimum").asDbl()) this._problems.push($"{at}: {value} is below the minimum {rules.get("minimum")}");
            if (rules.has("maximum") && value.asDbl() > rules.get("maximum").asDbl()) this._problems.push($"{at}: {value} is above the maximum {rules.get("maximum")}");
        }
        if (value.isString()) {
            if (rules.has("minLength") && value.length() < rules.get("minLength").asInt()) this._problems.push($"{at}: shorter than {rules.get("minLength")} characters");
            if (rules.has("maxLength") && value.length() > rules.get("maxLength").asInt()) this._problems.push($"{at}: longer than {rules.get("maxLength")} characters");
        }
        if (value.isArray()) {
            if (rules.has("minItems") && value.length() < rules.get("minItems").asInt()) this._problems.push($"{at}: fewer than {rules.get("minItems")} items");
            if (rules.has("maxItems") && value.length() > rules.get("maxItems").asInt()) this._problems.push($"{at}: more than {rules.get("maxItems")} items");
            if (rules.has("items")) {
                for (var[Int] i = 0; i < value.length(); i++) this._check(rules.get("items"), value.items<i>, where == "" ? Str(i) : $"{where}.{i}");
            }
        }
        if (value.isObject()) {
            if (rules.has("required")) {
                for (var[Json] name in rules.get("required").arrayItems()) {
                    var[Str] key = name.asStr();
                    if (!value.has(key)) this._problems.push($"{where == "" ? key : where + "." + key}: missing");
                }
            }
            var[Json] properties = rules.has("properties") ? rules.get("properties") : jsonObject();
            for (var[Str] key in value.fields) {
                var[Str] inner = where == "" ? key : $"{where}.{key}";
                if (properties.has(key)) {
                    this._check(properties.get(key), value.fields<key>, inner);
                } else if (rules.has("additionalProperties") && rules.get("additionalProperties").isBool() && !rules.get("additionalProperties").asBul()) {
                    this._problems.push($"{inner}: not allowed here");
                }
            }
        }
    }
}
