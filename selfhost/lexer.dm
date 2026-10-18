/***
 * The lexer of the Dessa compiler, written in Dessa: source text in, tokens
 * out. A line-for-line twin of src/lexer/lexer.cpp; `dessa run
 * selfhost/main.ds tokens file.ds` prints exactly what `dessa tokens file.ds`
 * prints (selfhost/check.py compares them on every Dessa file in the repo).
 */

// The kinds of token, in the same order as src/lexer/token.hpp (the numbers
// are printed, so the order matters)
enum TokenType {
    IDENTIFIER, STRING, TEMPLATE_STRING, INT_LITERAL, FLOAT_LITERAL, DOUBLE_LITERAL, CHAR_LITERAL,
    BINARY_LITERAL, OCTAL_LITERAL, HEX_LITERAL,
    VAR, CONST, FUNC, CLASS, INTERFACE, ENUM,
    TYPE_STR, TYPE_INT, TYPE_BUL, TYPE_FLT, TYPE_DBL, TYPE_CHR, TYPE_EMP, TYPE_NIL,
    IF, ELSE, SWITCH, CASE, DEFAULT, FOR, WHILE, LOOP, BREAK, CONTINUE, RETURN, IN, TRY, CATCH, THROW,
    FROM, AS,
    NEW, THIS, TRUE, FALSE, NIL, INIT, SUPER,
    L_PAREN, R_PAREN, L_BRACE, R_BRACE, L_BRACKET, R_BRACKET, L_ANGLE, R_ANGLE,
    COMMA, COLON, SEMICOLON, DOT, DOLLAR,
    PLUS, MINUS, MULT, DIV, EXPONENT, MOD,
    EQUAL, PLUS_EQUAL, MINUS_EQUAL, MULT_EQUAL, DIV_EQUAL, EXPONENT_EQUAL, MOD_EQUAL,
    PLUS_PLUS, MINUS_MINUS,
    EQUAL_EQUAL, BANG_EQUAL, GREATER_EQUAL, LESS_EQUAL,
    AND_AND, OR_OR, NOT,
    BITWISE_AND, BITWISE_OR, BITWISE_XOR, LEFT_SHIFT, RIGHT_SHIFT, BITWISE_NOT,
    QUESTION_QUESTION, QUESTION_DOT, QUESTION, COLON_EQUAL, ELLIPSIS,
    QUESTION_EQUAL, QUESTION_BANG, AT, FUNC_PIPE, WITH, IS,
    END_OF_FILE
}

class Token {
    var[TokenType] type;
    var[Str] lexeme;
    var[Int] line;
    var[Int] column;

    init(type[TokenType], lexeme[Str], line[Int], column[Int]) {
        this.type = type;
        this.lexeme = lexeme;
        this.line = line;
        this.column = column;
    }

    func[Str] toStr() {
        return $"{this.line}:{this.column}\t{this.type.value()}\t{this.lexeme}";
    }
}

const[Map[Str, TokenType]] KEYWORDS = {
    "var": TokenType.VAR, "const": TokenType.CONST, "func": TokenType.FUNC, "class": TokenType.CLASS,
    "interface": TokenType.INTERFACE, "enum": TokenType.ENUM,
    "Str": TokenType.TYPE_STR, "Int": TokenType.TYPE_INT, "Bul": TokenType.TYPE_BUL, "Flt": TokenType.TYPE_FLT,
    "Dbl": TokenType.TYPE_DBL, "Chr": TokenType.TYPE_CHR, "Emp": TokenType.TYPE_EMP, "Nil": TokenType.TYPE_NIL,
    "if": TokenType.IF, "else": TokenType.ELSE, "switch": TokenType.SWITCH, "case": TokenType.CASE,
    "default": TokenType.DEFAULT, "for": TokenType.FOR, "while": TokenType.WHILE, "loop": TokenType.LOOP,
    "break": TokenType.BREAK, "continue": TokenType.CONTINUE, "return": TokenType.RETURN, "in": TokenType.IN,
    "try": TokenType.TRY, "catch": TokenType.CATCH, "throw": TokenType.THROW, "from": TokenType.FROM,
    "as": TokenType.AS, "new": TokenType.NEW, "this": TokenType.THIS, "super": TokenType.SUPER,
    "init": TokenType.INIT, "true": TokenType.TRUE, "false": TokenType.FALSE, "nil": TokenType.NIL,
    "with": TokenType.WITH, "is": TokenType.IS,
};

func[Bul] isDigit(c[Chr]) { return c >= '0' && c <= '9'; }
func[Bul] isAsciiAlpha(c[Chr]) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
func[Bul] isAlnum(c[Chr]) { return isDigit(c) || isAsciiAlpha(c); }
func[Bul] isHexDigit(c[Chr]) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

// How many bytes a character takes in UTF-8 (columns count bytes, like the C++ lexer)
func[Int] utf8Size(c[Chr]) {
    var[Int] code = c.code();
    return code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
}

class Lexer {
    var[Chr<>] _source = [];
    var[Token<>] tokens = [];
    var[Str<>] errors = [];
    var[Int] _start = 0;
    var[Int] _current = 0;
    var[Int] _line = 1;
    var[Int] _column = 1;
    var[Int] _startLine = 1;
    var[Int] _startColumn = 1;

    init(source[Str]) {
        this._source = source.chars();
    }

    func[Token<>] scanTokens() {
        while (!this._isAtEnd()) {
            this._start = this._current;
            this._startLine = this._line;
            this._startColumn = this._column;
            this._scanToken();
        }
        this.tokens.push(new Token(TokenType.END_OF_FILE, "", this._line, this._column));
        return this.tokens;
    }

    func[Bul] _isAtEnd() { return this._current >= this._source.length(); }

    func[Chr] _advance() {
        var[Chr] c = this._source<this._current>;
        this._current++;
        if (c == '\n') {
            this._line++;
            this._column = 1;
        } else {
            this._column += utf8Size(c);
        }
        return c;
    }

    func[Chr] _peek() { return this._isAtEnd() ? '\0' : this._source<this._current>; }

    func[Chr] _peekNext() {
        return this._current + 1 >= this._source.length() ? '\0' : this._source<this._current + 1>;
    }

    func[Chr] _peekAt(offset[Int]) {
        return this._current + offset >= this._source.length() ? '\0' : this._source<this._current + offset>;
    }

    func[Bul] _match(expected[Chr]) {
        if (this._isAtEnd() || this._source<this._current> != expected) return false;
        this._advance();
        return true;
    }

    func[Str] _text() {
        return this._source.slice(this._start, this._current).join("");
    }

    func[Emp] _add(type[TokenType]) {
        this._addWith(type, this._text());
    }

    func[Emp] _addWith(type[TokenType], lexeme[Str]) {
        this.tokens.push(new Token(type, lexeme, this._startLine, this._startColumn));
    }

    func[Emp] _error(message[Str]) {
        this.errors.push($"[line {this._startLine}, col {this._startColumn}] Lex error: {message}");
    }

    func[Emp] _scanToken() {
        var[Chr] c = this._advance();
        switch (c) {
            case ' ' := {}
            case '\r' := {}
            case '\t' := {}
            case '\n' := {}
            case '(' := { this._add(TokenType.L_PAREN); }
            case ')' := { this._add(TokenType.R_PAREN); }
            case '{' := { this._add(TokenType.L_BRACE); }
            case '}' := { this._add(TokenType.R_BRACE); }
            case '[' := { this._add(TokenType.L_BRACKET); }
            case ']' := { this._add(TokenType.R_BRACKET); }
            case '<' := {
                if (this._match('=')) this._add(TokenType.LESS_EQUAL);
                else if (this._match('<')) this._add(TokenType.LEFT_SHIFT);
                else this._add(TokenType.L_ANGLE);
            }
            case '>' := {
                if (this._match('=')) this._add(TokenType.GREATER_EQUAL);
                else if (this._match('>')) this._add(TokenType.RIGHT_SHIFT);
                else this._add(TokenType.R_ANGLE);
            }
            case ',' := { this._add(TokenType.COMMA); }
            case ';' := { this._add(TokenType.SEMICOLON); }
            case ':' := { this._add(this._match('=') ? TokenType.COLON_EQUAL : TokenType.COLON); }
            case '.' := {
                if (this._peek() == '.' && this._peekNext() == '.') {
                    this._advance();
                    this._advance();
                    this._add(TokenType.ELLIPSIS);
                } else {
                    this._add(TokenType.DOT);
                }
            }
            case '@' := { this._add(TokenType.AT); }
            case '$' := {
                if (this._peek() == '"' && this._peekNext() == '"' && this._peekAt(2) == '"') {
                    this._advance();
                    this._advance();
                    this._advance();
                    this._scanTripleString(true);
                } else if (this._peek() == '"') {
                    this._advance();
                    this._scanTemplateString();
                } else {
                    this._add(TokenType.DOLLAR);
                }
            }
            case '+' := {
                if (this._match('+')) this._add(TokenType.PLUS_PLUS);
                else if (this._match('=')) this._add(TokenType.PLUS_EQUAL);
                else this._add(TokenType.PLUS);
            }
            case '-' := {
                if (this._match('-')) this._add(TokenType.MINUS_MINUS);
                else if (this._match('=')) this._add(TokenType.MINUS_EQUAL);
                else this._add(TokenType.MINUS);
            }
            case '*' := { this._add(this._match('=') ? TokenType.MULT_EQUAL : TokenType.MULT); }
            case '%' := { this._add(this._match('=') ? TokenType.MOD_EQUAL : TokenType.MOD); }
            case '/' := {
                if (this._match('/')) {
                    while (this._peek() != '\n' && !this._isAtEnd()) this._advance();
                } else if (this._match('*')) {
                    while (!(this._peek() == '*' && this._peekNext() == '/') && !this._isAtEnd()) this._advance();
                    if (this._isAtEnd()) {
                        this._error("Unterminated block comment.");
                    } else {
                        this._advance();
                        this._advance();
                    }
                } else if (this._match('=')) {
                    this._add(TokenType.DIV_EQUAL);
                } else {
                    this._add(TokenType.DIV);
                }
            }
            case '^' := {
                if (this._match('^')) this._add(TokenType.BITWISE_XOR);
                else if (this._match('=')) this._add(TokenType.EXPONENT_EQUAL);
                else this._add(TokenType.EXPONENT);
            }
            case '=' := { this._add(this._match('=') ? TokenType.EQUAL_EQUAL : TokenType.EQUAL); }
            case '!' := { this._add(this._match('=') ? TokenType.BANG_EQUAL : TokenType.NOT); }
            case '&' := { this._add(this._match('&') ? TokenType.AND_AND : TokenType.BITWISE_AND); }
            case '|' := {
                if (this._match('|')) this._add(TokenType.OR_OR);
                else if (this._match('>')) this._add(TokenType.FUNC_PIPE);
                else this._add(TokenType.BITWISE_OR);
            }
            case '~' := { this._add(TokenType.BITWISE_NOT); }
            case '?' := {
                if (this._match('?')) this._add(TokenType.QUESTION_QUESTION);
                else if (this._match('.')) this._add(TokenType.QUESTION_DOT);
                else if (this._match('=')) this._add(TokenType.QUESTION_EQUAL);
                else if (this._match('!')) this._add(TokenType.QUESTION_BANG);
                else this._add(TokenType.QUESTION);
            }
            case '"' := {
                if (this._peek() == '"' && this._peekNext() == '"') {
                    this._advance();
                    this._advance();
                    this._scanTripleString(false);
                } else {
                    this._scanString();
                }
            }
            case '\'' := { this._scanChar(); }
            default := {
                if (isDigit(c)) {
                    this._scanNumber();
                } else if (isAsciiAlpha(c) || c == '_') {
                    this._scanIdentifierOrKeyword();
                } else {
                    this._error($"unexpected character '{c}'");
                }
            }
        }
    }

    func[Str] _translateEscape(escape[Chr]) {
        switch (escape) {
            case 'n' := { return "\n"; }
            case 't' := { return "\t"; }
            case 'r' := { return "\r"; }
            case '0' := { return "\0"; }
            case '\\' := { return "\\"; }
            case '"' := { return "\""; }
            case '\'' := { return "'"; }
            case '{' := { return "{"; }
            case '}' := { return "}"; }
            case 'x' := {
                var[Str] digits = "";
                while (digits.length() < 2 && isHexDigit(this._peek())) digits += this._advance();
                if (digits.length() != 2) {
                    this._error("\\x needs two hex digits, e.g. \\x1b");
                    return "x";
                }
                return Str(Chr(hexValue(digits)));
            }
            default := {
                this._error($"unknown escape sequence '\\{escape}'.");
                return Str(escape);
            }
        }
    }

    // """...""" and $"""...""": may span lines
    func[Emp] _scanTripleString(isTemplate[Bul]) {
        var[Str] raw = "";
        while (!this._isAtEnd() && !(this._peek() == '"' && this._peekNext() == '"' && this._peekAt(2) == '"')) {
            if (this._peek() == '\\') {
                raw += this._advance();
                if (!this._isAtEnd()) raw += this._advance();
            } else {
                raw += this._advance();
            }
        }
        if (this._isAtEnd()) {
            this._error("Unterminated \"\"\" string (it needs a closing \"\"\").");
            return;
        }
        this._advance();
        this._advance();
        this._advance();
        var[Str] text = dedentTriple(raw.replaceAll("\r", ""));
        if (isTemplate) {
            this._addWith(TokenType.TEMPLATE_STRING, text.replaceAll("\n", "\\n"));
        } else {
            this._addWith(TokenType.STRING, unescapeRaw(text));
        }
    }

    func[Emp] _scanString() {
        var[Str] value = "";
        while (this._peek() != '"' && !this._isAtEnd()) {
            if (this._peek() == '\n') break;
            if (this._peek() == '\\') {
                this._advance();
                if (this._isAtEnd()) break;
                value += this._translateEscape(this._advance());
            } else {
                value += this._advance();
            }
        }
        if (this._isAtEnd() || this._peek() != '"') {
            this._error("Unterminated string literal.");
            return;
        }
        this._advance();
        this._addWith(TokenType.STRING, value);
    }

    // Raw text, escapes included: the parser splits out the {interpolations}
    func[Emp] _scanTemplateString() {
        var[Str] value = "";
        var[Int] depth = 0;
        while ((this._peek() != '"' || depth > 0) && !this._isAtEnd()) {
            if (this._peek() == '\n') break;
            var[Chr] c = this._peek();
            if (c == '\\') {
                value += this._advance();
                if (!this._isAtEnd()) value += this._advance();
            } else if (depth > 0 && (c == '"' || c == '\'')) {
                var[Chr] quote = this._advance();
                value += quote;
                while (!this._isAtEnd() && this._peek() != quote && this._peek() != '\n') {
                    if (this._peek() == '\\') value += this._advance();
                    if (!this._isAtEnd()) value += this._advance();
                }
                if (this._peek() == quote) value += this._advance();
            } else {
                if (c == '{') depth++;
                else if (c == '}' && depth > 0) depth--;
                value += this._advance();
            }
        }
        if (this._isAtEnd() || this._peek() != '"') {
            this._error("Unterminated template string.");
            return;
        }
        this._advance();
        this._addWith(TokenType.TEMPLATE_STRING, value);
    }

    func[Emp] _scanNumber() {
        var[Chr] first = this._source<this._start>;
        var[Chr] prefix = this._peek();
        if (first == '0' && (prefix == 'x' || prefix == 'b' || prefix == 'o')) {
            this._advance();
            var[TokenType] type = prefix == 'x' ? TokenType.HEX_LITERAL : prefix == 'b' ? TokenType.BINARY_LITERAL : TokenType.OCTAL_LITERAL;
            var[Str] baseName = prefix == 'x' ? "hexadecimal" : prefix == 'b' ? "binary" : "octal";
            var[Int] digitsStart = this._current;
            while (isAlnum(this._peek())) {
                var[Chr] d = this._peek();
                var[Bul] valid = prefix == 'x' ? isHexDigit(d) : prefix == 'b' ? (d == '0' || d == '1') : (d >= '0' && d <= '7');
                if (!valid) this._error($"invalid digit '{d}' in {baseName} literal.");
                this._advance();
            }
            if (this._current == digitsStart) this._error($"missing digits after '0{prefix}' prefix.");
            this._add(type);
            return;
        }

        while (isDigit(this._peek())) this._advance();
        var[Bul] isDecimal = false;
        if (this._peek() == '.' && isDigit(this._peekNext())) {
            isDecimal = true;
            this._advance();
            while (isDigit(this._peek())) this._advance();
        }
        var[Chr] after = this._peekNext();
        var[Bul] endsWord = !(isAlnum(after) || after == '_');
        if (this._peek() == 'f' && endsWord) {
            this._advance();
            this._add(TokenType.FLOAT_LITERAL);
        } else if (this._peek() == 'd' && endsWord) {
            this._advance();
            this._add(TokenType.DOUBLE_LITERAL);
        } else if (isDecimal) {
            this._add(TokenType.DOUBLE_LITERAL);
        } else {
            this._add(TokenType.INT_LITERAL);
        }
        if (isAsciiAlpha(this._peek()) || this._peek() == '_') {
            this._error("invalid suffix on numeric literal.");
            while (isAlnum(this._peek()) || this._peek() == '_') this._advance();
        }
    }

    func[Emp] _scanIdentifierOrKeyword() {
        while (isAlnum(this._peek()) || this._peek() == '_') this._advance();
        var[Str] text = this._text();
        if (KEYWORDS.has(text)) {
            this._add(KEYWORDS<text>);
        } else {
            this._addWith(TokenType.IDENTIFIER, text);
        }
    }

    func[Emp] _scanChar() {
        if (this._peek() == '\'' || this._peek() == '\n' || this._isAtEnd()) {
            this._error("Empty char literal.");
            if (this._peek() == '\'') this._advance();
            this._addWith(TokenType.CHAR_LITERAL, "\0");
            return;
        }
        var[Str] value = "";
        if (this._peek() == '\\') {
            this._advance();
            value = this._translateEscape(this._advance());
        } else {
            value = Str(this._advance());   // one character, however many bytes
        }
        if (this._peek() != '\'') {
            this._error("Unterminated or multi-character char literal.");
            while (this._peek() != '\'' && this._peek() != '\n' && !this._isAtEnd()) this._advance();
        }
        if (this._peek() == '\'') this._advance();
        this._addWith(TokenType.CHAR_LITERAL, value);
    }
}

func[Int] hexValue(digits[Str]) {
    var[Int] value = 0;
    for (var[Chr] c in digits.lower()) {
        value = value * 16 + (isDigit(c) ? c.code() - 48 : c.code() - 87);
    }
    return value;
}

// Escapes in a triple-quoted string; unknown ones are kept as written
func[Str] unescapeRaw(raw[Str]) {
    var[Chr<>] cs = raw.chars();
    var[Str] out = "";
    var[Int] i = 0;
    while (i < cs.length()) {
        if (cs<i> != '\\' || i + 1 >= cs.length()) {
            out += cs<i>;
            i++;
            continue;
        }
        var[Chr] e = cs<i + 1>;
        if (e == 'n') { out += "\n"; i += 2; }
        else if (e == 't') { out += "\t"; i += 2; }
        else if (e == 'r') { out += "\r"; i += 2; }
        else if (e == '0') { out += "\0"; i += 2; }
        else if (e == '\\' || e == '"' || e == '\'' || e == '{' || e == '}') { out += e; i += 2; }
        else if (e == 'x' && i + 3 < cs.length() && isHexDigit(cs<i + 2>) && isHexDigit(cs<i + 3>)) {
            out += Chr(hexValue(Str(cs<i + 2>) + Str(cs<i + 3>)));
            i += 4;
        } else {
            out += cs<i>;
            i++;
        }
    }
    return out;
}

// C#-style layout of a triple-quoted string (see dedentTriple in lexer.cpp)
func[Str] dedentTriple(raw[Str]) {
    var[Str] text = raw;
    var[Int] first = 0;
    var[Chr<>] cs = text.chars();
    while (first < cs.length() && (cs<first> == ' ' || cs<first> == '\t')) first++;
    if (first < cs.length() && cs<first> == '\n') text = text.substr(first + 1, text.length() - first - 1);

    var[Str<>] lines = text.split("\n");
    if (lines.length() == 1) return text;
    var[Str] indent = lines.last();
    if (indent.trim() != "" ) return text;   // the closing """ follows text
    lines.pop();
    var[Str<>] out = [];
    for (var[Str] line in lines) {
        if (line.startsWith(indent)) {
            out.push(line.substr(indent.length(), line.length() - indent.length()));
        } else {
            var[Int] skip = 0;
            var[Chr<>] lc = line.chars();
            while (skip < lc.length() && (lc<skip> == ' ' || lc<skip> == '\t')) skip++;
            out.push(line.substr(skip, line.length() - skip));
        }
    }
    return out.join("\n");
}
