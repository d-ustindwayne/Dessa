/***
 * The parser of the Dessa compiler, written in Dessa: tokens in, a tree out.
 * A function-by-function twin of src/parser/parser.cpp; `selfhost/main.ds ast
 * file.ds` prints what `dessa ast file.ds` prints (selfhost/check.py --ast).
 *
 * C++ undoes its scopes (nesting depth, "inside a list", ...) as a ParseError
 * passes through them. Here every place that catches a ParseError puts back
 * the state it saved (_save / _restore), which comes to the same.
 */

$("lexer") { Lexer, Token, TokenType, utf8Size }
$("ast")

class ParseError(Error) {}

const[Int] MAX_NESTING = 200;       // how deeply code may nest
const[Int] MAX_CHAIN = 1000;        // a + b + c ..., a.b().c()..., else if ...
const[Int] MAX_TYPE_SUFFIXES = 32;  // Int<><><>...
const[Int] SHOWN = 60;              // how much of a token an error message quotes (bytes)

// Shared by every parser, also those of template strings and markup (like the C++ thread_local)
var[Int] nestingLevel = 0;

// How many bytes a text takes in UTF-8 (columns count bytes)
func[Int] byteLength(text[Str]) {
    var[Int] n = 0;
    for (var[Chr] c in text) n += utf8Size(c);
    return n;
}

// Its start, at most `limit` bytes (whole characters only)
func[Str] bytePrefix(text[Str], limit[Int]) {
    var[Str<>] out = <>;
    var[Int] n = 0;
    for (var[Chr] c in text) {
        n += utf8Size(c);
        if (n > limit) break;
        out.push(Str(c));
    }
    return out.join("");
}

func[Bul] isAsciiLetter(c[Chr]) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
func[Bul] isAsciiDigit(c[Chr]) { return c >= '0' && c <= '9'; }
func[Bul] isAsciiHex(c[Chr]) { return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
func[Bul] isBlank(c[Chr]) { return c == ' ' || (c.code() >= 9 && c.code() <= 13); }
func[Bul] isMarkupNameChar(c[Chr]) { return isAsciiLetter(c) || isAsciiDigit(c) || c == '_' || c == '-' || c == ':' || c == '.'; }

// Only spaces and tabs (or nothing)?
func[Bul] onlySpacesTabs(text[Str]) {
    for (var[Chr] c in text) {
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

// Where `pattern` is in `cs` from `from` on, or -1
func[Int] findChars(cs[Chr<>], pattern[Str], from[Int]) {
    var[Chr<>] p = pattern.chars();
    var[Int] i = from < 0 ? 0 : from;
    while (i + p.length() <= cs.length()) {
        var[Int] k = 0;
        while (k < p.length() && cs<i + k> == p<k>) k++;
        if (k == p.length()) return i;
        i++;
    }
    return -1;
}

func[Str] slice(cs[Chr<>], from[Int], to[Int]) {
    return cs.slice(from, to).join("");
}

// Template-string text parts: \n, \t, \r, \0, \xNN; any other \c is c (\{ is {)
func[Str] unescape(raw[Str]) {
    var[Chr<>] cs = raw.chars();
    var[Str<>] out = <>;
    var[Int] i = 0;
    while (i < cs.length()) {
        if (cs<i> != '\\' || i + 1 >= cs.length()) {
            out.push(Str(cs<i>));
            i++;
            continue;
        }
        i++;
        var[Chr] e = cs<i>;
        if (e == 'n') out.push("\n");
        else if (e == 't') out.push("\t");
        else if (e == 'r') out.push("\r");
        else if (e == '0') out.push("\0");
        else if (e == 'x') {
            if (i + 2 < cs.length() && isAsciiHex(cs<i + 1>) && isAsciiHex(cs<i + 2>)) {
                out.push(Str(Chr(Int("0x" + Str(cs<i + 1>) + Str(cs<i + 2>)))));
                i += 2;
            } else {
                out.push("x");
            }
        } else {
            out.push(Str(e));
        }
        i++;
    }
    return out.join("");
}

// &lt; and friends in markup text and "attribute" strings
const[Str<>] ENTITY_NAMES = <"&lt;", "&gt;", "&amp;", "&quot;", "&apos;", "&nbsp;", "&#123;", "&#125;">;
const[Str<>] ENTITY_VALUES = <"<", ">", "&", "\"", "'", " ", "{", "}">;

func[Str] decodeEntities(text[Str]) {
    var[Chr<>] cs = text.chars();
    var[Str<>] out = <>;
    var[Int] i = 0;
    while (i < cs.length()) {
        var[Bul] found = false;
        if (cs<i> == '&') {
            var[Int] k = 0;
            while (k < ENTITY_NAMES.length()) {
                var[Str] name = ENTITY_NAMES<k>;
                if (findChars(cs, name, i) == i) {
                    out.push(ENTITY_VALUES<k>);
                    i += name.length();
                    found = true;
                    break;
                }
                k++;
            }
        }
        if (!found) {
            out.push(Str(cs<i>));
            i++;
        }
    }
    return out.join("");
}

func[Str] trimStartSpaces(line[Str]) {
    var[Chr<>] cs = line.chars();
    var[Int] i = 0;
    while (i < cs.length() && (cs<i> == ' ' || cs<i> == '\t')) i++;
    return slice(cs, i, cs.length());
}

func[Str] trimEndSpaces(line[Str]) {
    var[Chr<>] cs = line.chars();
    var[Int] n = cs.length();
    while (n > 0 && (cs<n - 1> == ' ' || cs<n - 1> == '\t')) n--;
    return slice(cs, 0, n);
}

// Text between tags, as JSX has it: lines joined by one space, the blank
// space around each line break gone
func[Str] markupText(raw[Str]) {
    var[Str<>] lines = raw.split("\n");
    var[Str<>] kept = <>;
    var[Int] i = 0;
    while (i < lines.length()) {
        var[Str] line = lines<i>;
        if (line.endsWith("\r")) line = line.substr(0, line.length() - 1);
        if (i > 0) line = trimStartSpaces(line);
        if (i + 1 < lines.length()) line = trimEndSpaces(line);
        if (line != "") kept.push(line);
        i++;
    }
    return decodeEntities(kept.join(" "));
}

// What the parser's scopes were at some point: put back after a ParseError
class ParserState {
    var[Int] nesting = 0;
    var[Bul] inAngleList = false;
    var[Bul] colonEnds = false;
    var[Bul] inAsync = false;
    var[Bul] inComp = false;
    var[Int] blockDepth = 0;
    var[Int] indexDepth = 0;
}

class Parser {
    var[Token<>] tokens;
    var[Int] current = 0;
    var[Str<>] errors = <>;
    var[Stmt<>] templates = <>;
    var[Stmt<>] moreDeclarations = <>;
    var[Bul] componentFile = false;
    var[Int] blockDepth = 0;
    var[Bul] inComp = false;
    var[Bul] inAsync = false;
    var[Bul] asyncNext = false;
    var[Bul] colonEnds = false;
    var[Bul] inAngleList = false;
    var[Int] indexDepth = 0;
    var[Int] elseIfChain = 0;
    // Where `<` turned out not to start an index (in what surroundings)
    var[Map[Int, Bul]] notAnIndex = {:};

    init(tokens[Token<>], componentFile[Bul] = false) {
        this.tokens = tokens;
        this.componentFile = componentFile;
    }

    func[ParserState] _save() {
        var[ParserState] s = new ParserState();
        s.nesting = nestingLevel;
        s.inAngleList = this.inAngleList;
        s.colonEnds = this.colonEnds;
        s.inAsync = this.inAsync;
        s.inComp = this.inComp;
        s.blockDepth = this.blockDepth;
        s.indexDepth = this.indexDepth;
        return s;
    }

    func[Emp] _restore(s[ParserState]) {
        nestingLevel = s.nesting;
        this.inAngleList = s.inAngleList;
        this.colonEnds = s.colonEnds;
        this.inAsync = s.inAsync;
        this.inComp = s.inComp;
        this.blockDepth = s.blockDepth;
        this.indexDepth = s.indexDepth;
    }

    // ---- Token plumbing ----

    func[Bul] _isAtEnd() { return this._peek().type == TokenType.END_OF_FILE; }
    func[Token] _peek() { return this.tokens<this.current>; }

    func[Token] _peekAt(offset[Int]) {
        var[Int] index = this.current + offset;
        return index < this.tokens.length() ? this.tokens<index> : this.tokens.last();
    }

    func[Token] _previous() { return this.tokens<this.current - 1>; }

    func[Token] _advance() {
        if (!this._isAtEnd()) this.current++;
        return this._previous();
    }

    func[Bul] _check(type[TokenType]) {
        if (this._isAtEnd()) return type == TokenType.END_OF_FILE;
        return this._peek().type == type;
    }

    func[Bul] _checkNext(type[TokenType]) {
        if (this._isAtEnd()) return false;
        return this._peekAt(1).type == type;
    }

    func[Bul] _match(type[TokenType]) {
        if (!this._check(type)) return false;
        this._advance();
        return true;
    }

    func[Bul] _matchAny(types[TokenType<>]) {
        for (var[TokenType] type in types) {
            if (this._check(type)) {
                this._advance();
                return true;
            }
        }
        return false;
    }

    func[Token] _consume(type[TokenType], message[Str]) {
        if (this._check(type)) return this._advance();
        if (type == TokenType.SEMICOLON && this.current > 0 && this._previous().line < this._peek().line) {
            // `var[Int] g = 3` <newline> `var ...`: point at the line missing the ';'
            var[Token] before = this._previous();
            throw this._error(new Token(before.type, before.lexeme, before.line, before.column + byteLength(before.lexeme)), message);
        }
        if (this._peek().type == TokenType.WITH) throw this._error(this._peek(), $"'{this._peek().lexeme}' is reserved for future use.");
        throw this._error(this._peek(), message);
    }

    func[ParseError] _error(token[Token], message[Str]) {
        var[Str] text = $"[line {token.line}, col {token.column}] Parse error";
        if (token.type == TokenType.END_OF_FILE) {
            text += " at end: " + message;
        } else {
            var[Str] shown = byteLength(token.lexeme) > SHOWN ? bytePrefix(token.lexeme, SHOWN) + "..." : token.lexeme;
            text += " at '" + shown + "': " + message;
        }
        this.errors.push(text);
        return new ParseError(message);
    }

    func[Emp] _synchronize() {
        if (!this._isAtEnd()) this._advance();
        while (!this._isAtEnd()) {
            if (this._previous().type == TokenType.SEMICOLON) return;
            var[TokenType] t = this._peek().type;
            if (t == TokenType.R_BRACE) return;
            if (t == TokenType.CLASS || t == TokenType.FUNC || t == TokenType.VAR || t == TokenType.CONST || t == TokenType.ENUM ||
                t == TokenType.INTERFACE || t == TokenType.IF || t == TokenType.SWITCH || t == TokenType.FOR || t == TokenType.WHILE ||
                t == TokenType.LOOP || t == TokenType.RETURN || t == TokenType.TRY || t == TokenType.THROW) return;
            this._advance();
        }
    }

    // Backtracking: the position and how many errors there were
    func[Emp] _reset(position[Int], errorCount[Int]) {
        this.current = position;
        while (this.errors.length() > errorCount) this.errors.pop();
    }

    func[Expr] _placeE(e[Expr], at[Token]) {
        if (e.line == 0) {
            e.line = at.line;
            e.column = at.column;
        }
        return e;
    }

    func[Stmt | Nil] _placeS(s[Stmt | Nil], at[Token]) {
        if (s != nil && s.line == 0) {
            s.line = at.line;
            s.column = at.column;
        }
        return s;
    }

    func[Emp] _enterNesting() {
        if (nestingLevel >= MAX_NESTING) {
            throw this._error(this._peek(), $"This code is nested too deeply (more than {MAX_NESTING} levels). Split it into smaller pieces.");
        }
        nestingLevel++;
    }

    func[Emp] _checkChain(length[Int], what[Str]) {
        if (length > MAX_CHAIN) throw this._error(this._peek(), $"This {what} is too long (more than {MAX_CHAIN}). Split it into smaller pieces.");
    }

    // ---- The program ----

    func[Stmt<>] parseProgram() {
        var[Stmt<>] statements = <>;
        while (!this._isAtEnd()) {
            var[Stmt | Nil] stmt = this.declaration();
            if (stmt != nil) {
                var[Bul] generic = false;
                if (stmt is FuncDeclStmt) generic = !stmt.typeParams.isEmpty();
                if (stmt is ClassDeclStmt) generic = !stmt.typeParams.isEmpty();
                if (generic) this.templates.push(stmt);
                else statements.push(stmt);
            }
            for (var[Stmt] more in this._takeMoreDeclarations()) statements.push(more);
        }
        return statements;
    }

    // var[Int] x = 1, y = 2: the declarations after the first. (Returned, not
    // pushed into a list given: a list passed to a function is a copy.)
    func[Stmt<>] _takeMoreDeclarations() {
        var[Stmt<>] more = this.moreDeclarations;
        this.moreDeclarations = <>;
        return more;
    }

    // ---- Types ----

    // :T or :[K, V] after a generic function's or class's name
    func[Str<>] _parseTypeParams() {
        var[Str<>] params = <>;
        if (this._match(TokenType.L_BRACKET)) {
            loop {
                params.push(this._typeParam(params));
                if (!this._match(TokenType.COMMA)) break;
            }
            this._consume(TokenType.R_BRACKET, "Expect ']' after the type parameters.");
        } else {
            params.push(this._typeParam(params));
        }
        return params;
    }

    // One name; `params` are those before it
    func[Str] _typeParam(params[Str<>]) {
        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect a type parameter name, e.g. :T or :[K, V].").lexeme;
        if (params.indexOf(name) >= 0) throw this._error(this._previous(), $"Type parameter '{name}' is listed twice.");
        return name;
    }

    // The types after ':' in Box:Int or Pair:[Str, Int]
    func[TypeNode<>] _parseTypeArgs() {
        var[TypeNode<>] args = <>;
        if (this._match(TokenType.L_BRACKET)) {
            loop {
                args.push(this._parseType());
                if (!this._match(TokenType.COMMA)) break;
            }
            this._consume(TokenType.R_BRACKET, "Expect ']' after the types.");
        } else {
            args.push(this._parseTypeBase());
        }
        return args;
    }

    // After ':' -- does a type come next? ([, a built-in type, or a capitalised name)
    func[Bul] _typeArgAheadAt(offset[Int]) {
        var[Token] t = this._peekAt(offset);
        var[TokenType] k = t.type;
        if (k == TokenType.L_BRACKET || k == TokenType.TYPE_STR || k == TokenType.TYPE_INT || k == TokenType.TYPE_BUL ||
            k == TokenType.TYPE_FLT || k == TokenType.TYPE_DBL || k == TokenType.TYPE_CHR || k == TokenType.TYPE_EMP) return true;
        if (k == TokenType.IDENTIFIER) {
            if (t.lexeme == "") return false;
            var[Chr] first = t.lexeme.chars()<0>;
            return first >= 'A' && first <= 'Z';
        }
        return false;
    }

    func[Bul] _typeArgAhead() { return this._typeArgAheadAt(1); }

    func[TypeNode] _parseType() {
        this._enterNesting();
        var[TypeNode] node = this._parseTypeBase();
        // Collection suffixes nest: Int<> (list), Int{} (set), Int<><> (list of lists)
        var[Int] suffixes = 0;
        loop {
            if (suffixes > MAX_TYPE_SUFFIXES) {
                throw this._error(this._peek(), $"This type is nested too deeply (more than {MAX_TYPE_SUFFIXES} levels).");
            }
            var[Bul] list = false;
            if (this._match(TokenType.L_ANGLE)) {
                this._consume(TokenType.R_ANGLE, "Expect '>' after '<' in list type.");
                list = true;
            } else if (this._match(TokenType.L_BRACE)) {
                this._consume(TokenType.R_BRACE, "Expect '}' after '{' in set type.");
            } else {
                break;
            }
            if (node.isList || node.isSet) {
                var[TypeNode] inner = node;
                node = new TypeNode();
                node.group.push(inner);
            }
            if (list) node.isList = true;
            else node.isSet = true;
            suffixes++;
        }
        while (this._match(TokenType.BITWISE_OR)) node.unionMembers.push(this._parseType());
        nestingLevel--;
        return node;
    }

    // A type without <>, {} or | after it: Int, Int[16], Map[K, V], Func[...], (A | B), Box:Int, Lst[Int]
    func[TypeNode] _parseTypeBase() {
        var[TypeNode] node = new TypeNode();
        if (this._match(TokenType.L_PAREN)) {
            node.group.push(this._parseType());
            this._consume(TokenType.R_PAREN, "Expect ')' after the type in parentheses.");
        } else if (this._check(TokenType.IDENTIFIER) || this._check(TokenType.TYPE_STR) || this._check(TokenType.TYPE_INT) ||
                   this._check(TokenType.TYPE_BUL) || this._check(TokenType.TYPE_FLT) || this._check(TokenType.TYPE_DBL) ||
                   this._check(TokenType.TYPE_CHR) || this._check(TokenType.TYPE_EMP) || this._check(TokenType.TYPE_NIL)) {
            node.name = this._advance().lexeme;
            // m.Database: a type from a module imported as m
            while (this._previous().type == TokenType.IDENTIFIER && this._check(TokenType.DOT) && this._checkNext(TokenType.IDENTIFIER)) {
                this._advance();
                node.name += "." + this._advance().lexeme;
            }
        } else {
            throw this._error(this._peek(), "Expect type name.");
        }

        // Lst[Int | Str]: another way to write (Int | Str)<>
        if (node.name == "Lst" && this._check(TokenType.L_BRACKET)) {
            this._advance();
            var[TypeNode] element = this._parseType();
            this._consume(TokenType.R_BRACKET, "Expect ']' after the type of the list's items, e.g. Lst[Int].");
            var[TypeNode] list = new TypeNode();
            list.group.push(element);
            list.isList = true;
            return list;
        }
        // Box:Int, Pair:[Str, Int]
        if (node.group.isEmpty() && this._check(TokenType.COLON) && this._typeArgAhead()) {
            this._advance();
            node.typeArgs = this._parseTypeArgs();
            return node;
        }

        if (node.name == "Map" && this._match(TokenType.L_BRACKET)) {
            node.isMap = true;
            node.mapTypes.push(this._parseType());
            this._consume(TokenType.COMMA, "Expect ',' between the key and value types, e.g. Map[Str, Int].");
            node.mapTypes.push(this._parseType());
            this._consume(TokenType.R_BRACKET, "Expect ']' after Map types, e.g. Map[Str, Int].");
        } else if (node.name == "Func" && this._match(TokenType.L_BRACKET)) {
            node.isFunction = true;
            node.funcReturn.push(this._parseType());
            if (this._match(TokenType.COMMA)) {
                if (this._match(TokenType.L_BRACKET)) {
                    if (!this._check(TokenType.R_BRACKET)) {
                        loop {
                            node.funcParams.push(this._parseType());
                            if (!this._match(TokenType.COMMA)) break;
                        }
                    }
                    this._consume(TokenType.R_BRACKET, "Expect ']' after Func parameter types.");
                } else {
                    node.funcParams.push(this._parseType());
                }
            }
            this._consume(TokenType.R_BRACKET, "Expect ']' after Func type, e.g. Func[Int, [Int, Int]].");
        }

        // Int[16]: an integer base
        if (!node.isFunction && !node.isMap && node.name != "" && this._match(TokenType.L_BRACKET)) {
            var[Token] radixToken = this._consume(TokenType.INT_LITERAL, "Expect integer base (2, 8, 10 or 16).");
            var[Int] radix = -1;
            try {
                radix = Int(radixToken.lexeme);
            } catch (e[Str]) {
                radix = -1;
            }
            if (radix < 0 || radix > 2147483647) throw this._error(radixToken, "Invalid integer base.");
            node.radix = radix;
            this._consume(TokenType.R_BRACKET, "Expect ']' after integer base.");
        }
        return node;
    }

    // name[Type], rest[Type*], greeting[Str]="Hi", ...   (possibly empty; stops before ')')
    func[Param<>] _parseParamList() {
        var[Param<>] params = <>;
        if (!this._check(TokenType.R_PAREN)) {
            loop {
                var[Param] param = new Param();
                var[Token] nameToken = this._consume(TokenType.IDENTIFIER, "Expect parameter name.");
                param.name = nameToken.lexeme;
                param.line = nameToken.line;
                param.column = nameToken.column;
                this._consume(TokenType.L_BRACKET, "Expect '[' before parameter type.");
                param.type = this._parseType();
                param.isVariadic = this._match(TokenType.MULT);
                this._consume(TokenType.R_BRACKET, "Expect ']' after parameter type.");
                if (this._match(TokenType.EQUAL)) param.defaultValue = this.expression();
                params.push(param);
                if (!this._match(TokenType.COMMA)) break;
            }
        }
        return params;
    }

    // ---- Declarations ----

    func[Stmt | Nil] declaration() {
        var[Token] start = this._peek();
        var[ParserState] saved = this._save();
        try {
            return this._placeS(this._declarationInner(), start);
        } catch (e[ParseError]) {
            this._restore(saved);
            this.moreDeclarations = <>;
            this._synchronize();
            return nil;
        }
    }

    func[Stmt | Nil] _declarationInner() {
        if (this._match(TokenType.VAR)) return this._varDeclaration();
        if (this._match(TokenType.CONST)) return this._constDeclaration();
        // async func[T] f()
        if (this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "async" && this._checkNext(TokenType.FUNC)) {
            this._advance();
            this.asyncNext = true;
        }
        if (this._match(TokenType.FUNC)) {
            var[Token] at = this._previous();
            var[FuncDeclStmt] fn = this._funcDeclaration();
            // Inside a function: a lambda with a name
            return this.blockDepth > 0 ? this._innerFunction(fn, at) : fn;
        }
        if (this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "comp" && this._peekAt(1).type == TokenType.IDENTIFIER &&
            this._peekAt(2).type == TokenType.L_PAREN) {
            if (!this.componentFile) this._error(this._peek(), "comp declares a component, and only .xds, .xdz and .xdx files have them.");
            else if (this.blockDepth > 0) this._error(this._peek(), "A comp can't be inside a comp or a function. Declare it at the top of the file; a func can be inside.");
            var[Bul] allowed = this.componentFile && this.blockDepth == 0;
            this._advance();
            var[Stmt] comp = this._compDeclaration();
            return allowed ? comp : nil;
        }
        if (this._check(TokenType.AT)) {
            var[Token<>] annotations = this._parseAnnotations();
            if (!this._match(TokenType.FUNC)) throw this._error(this._peek(), "Decorators like @override go before methods.");
            var[FuncDeclStmt] fn = this._funcDeclaration();
            fn.annotations = annotations;
            return fn;
        }
        if (this._match(TokenType.CLASS)) return this._classDeclaration();
        if (this._match(TokenType.ENUM)) return this._enumDeclaration();
        if (this._match(TokenType.INTERFACE)) return this._interfaceDeclaration();
        if (this._check(TokenType.DOLLAR) || this._isImportAhead()) return this._importStatement();
        return this._statementInner();
    }

    func[Stmt] _varDeclaration() {
        this._consume(TokenType.L_BRACKET, "Expect '[' before variable type.");
        var[TypeNode] type = this._parseType();
        this._consume(TokenType.R_BRACKET, "Expect ']' after variable type.");
        return this._declarators(false, type);
    }

    func[Stmt] _constDeclaration() {
        this._consume(TokenType.L_BRACKET, "Expect '[' before constant type.");
        var[TypeNode] type = this._parseType();
        this._consume(TokenType.R_BRACKET, "Expect ']' after constant type.");
        return this._declarators(true, type);
    }

    // After var[Type] / const[Type]:  x = 10, y = 20;  or  a = b = c = 2;
    func[Stmt] _declarators(isConst[Bul], type[TypeNode]) {
        var[Stmt<>] made = <>;
        loop {
            if (!this._check(TokenType.IDENTIFIER)) {
                throw this._error(this._peek(), isConst ? "Expect constant name." : "Expect variable name after type.");
            }
            var[Token<>] names = <this._advance()>;
            var[Expr | Nil] initializer = nil;
            if (this._match(TokenType.EQUAL)) {
                while (this._check(TokenType.IDENTIFIER) && this._checkNext(TokenType.EQUAL)) {
                    names.push(this._advance());
                    this._advance();
                }
                initializer = this.expression();
            } else if (isConst) {
                throw this._error(this._peek(), "Constant declaration must have an initializer.");
            }
            // the last name takes the value; each name before it copies the next
            var[Int] i = names.length() - 1;
            while (i >= 0) {
                var[Expr | Nil] value = initializer;
                if (i + 1 != names.length()) value = this._placeE(new IdentifierExpr(names<i + 1>), names<i + 1>);
                var[VarDeclStmt] decl = new VarDeclStmt(isConst, type, names<i>.lexeme, value);
                this._placeS(decl, names<i>);
                made.push(decl);
                i--;
            }
            if (!this._match(TokenType.COMMA)) break;
        }
        this._consume(TokenType.SEMICOLON, isConst ? "Expect ';' after constant declaration." : "Expect ';' after variable declaration.");
        var[Int] k = 1;
        while (k < made.length()) {
            this.moreDeclarations.push(made<k>);
            k++;
        }
        return made<0>;
    }

    // func[Int !! NumberError, Str]: what the function may throw
    func[TypeNode<>] _parseThrows() {
        var[TypeNode<>] types = <>;
        if (this._check(TokenType.NOT) && this._checkNext(TokenType.NOT)) {
            this._advance();
            this._advance();
            loop {
                types.push(this._parseType());
                if (!this._match(TokenType.COMMA)) break;
            }
        }
        return types;
    }

    func[FuncDeclStmt] _funcDeclaration() {
        // the `func`, and an `async` in front of it: a generic one is read again from here
        var[Bul] isAsync = this.asyncNext;
        this.asyncNext = false;
        var[Int] start = this.current - (isAsync ? 2 : 1);
        this._consume(TokenType.L_BRACKET, "Expect '[' before return type.");
        var[TypeNode] returnType = this._parseType();
        var[TypeNode<>] throwsTypes = this._parseThrows();
        this._consume(TokenType.R_BRACKET, "Expect ']' after return type.");

        // func[T] async f(): the same, the other way of writing it
        if (this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "async" && this._checkNext(TokenType.IDENTIFIER)) {
            this._advance();
            isAsync = true;
        }

        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect function name.").lexeme;
        var[Str<>] typeParams = <>;
        if (this._match(TokenType.COLON)) typeParams = this._parseTypeParams();

        this._consume(TokenType.L_PAREN, "Expect '(' after function name.");
        var[Param<>] params = this._parseParamList();
        this._consume(TokenType.R_PAREN, "Expect ')' after parameters.");

        this._consume(TokenType.L_BRACE, "Expect '{' before function body.");
        var[Bul] savedAsync = this.inAsync;
        var[Bul] savedComp = this.inComp;
        this.inAsync = isAsync;
        this.inComp = false;
        var[Stmt] body = this._blockStatement();
        this.inAsync = savedAsync;
        this.inComp = savedComp;

        var[FuncDeclStmt] fn = new FuncDeclStmt(returnType, name, params, body);
        fn.isAsync = isAsync;
        fn.throwsTypes = throwsTypes;
        if (!typeParams.isEmpty()) {
            fn.typeParams = typeParams;
            fn.templateTokens = this.tokens.slice(start, this.current);
            fn.templateTokens.push(new Token(TokenType.END_OF_FILE, "", this._previous().line, this._previous().column));
        }
        return fn;
    }

    // comp Name(params) { ... @(<markup>) }
    func[Stmt] _compDeclaration() {
        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect a component name.").lexeme;
        this._consume(TokenType.L_PAREN, "Expect '(' after the component's name.");
        var[Param<>] params = this._parseParamList();
        this._consume(TokenType.R_PAREN, "Expect ')' after parameters.");
        this._consume(TokenType.L_BRACE, "Expect '{' before the component's body.");
        var[Bul] saved = this.inComp;
        this.inComp = true;
        var[Stmt] body = this._blockStatement();
        this.inComp = saved;
        var[TypeNode] unset = new TypeNode();
        unset.name = "Emp";   // the analyser fills it in
        var[FuncDeclStmt] fn = new FuncDeclStmt(unset, name, params, body);
        fn.isComp = true;
        return fn;
    }

    // func inside a function body: const[R] name = (params) := { ... };
    func[Stmt | Nil] _innerFunction(fn[FuncDeclStmt], at[Token]) {
        if (fn.isAsync) {
            this._error(at, "An async function can't be inside a function. Declare it at the top of the file.");
            return nil;
        }
        if (!fn.typeParams.isEmpty()) {
            this._error(at, "A function inside a function can't be generic. Declare it at the top of the file.");
            return nil;
        }
        for (var[Param] p in fn.params) {
            if (p.isVariadic || p.defaultValue != nil) {
                this._error(at, $"A function inside a function is a lambda, so '{p.name}' can't have a default or be variadic. Declare it at the top of the file.");
                return nil;
            }
        }
        if (!fn.throwsTypes.isEmpty()) {
            this._error(at, "A function inside a function is a lambda, so it can't list what it throws. Declare it at the top of the file.");
            return nil;
        }
        var[Stmt | Nil] body = fn.body;
        var[Stmt] lambdaBody = new Stmt();
        if (body != nil) lambdaBody = body;
        var[Expr] lambda = this._placeE(new LambdaExpr(fn.params, lambdaBody), at);
        return new VarDeclStmt(true, fn.returnType, fn.name, lambda);
    }

    // init(params) { body }
    func[Stmt] _constructorDeclaration() {
        this._consume(TokenType.L_PAREN, "Expect '(' after 'init'.");
        var[Param<>] params = this._parseParamList();
        this._consume(TokenType.R_PAREN, "Expect ')' after constructor parameters.");
        this._consume(TokenType.L_BRACE, "Expect '{' before constructor body.");
        var[Stmt] body = this._blockStatement();
        var[TypeNode] emp = new TypeNode();
        emp.name = "Emp";
        var[FuncDeclStmt] ctor = new FuncDeclStmt(emp, "init", params, body);
        ctor.isConstructor = true;
        return ctor;
    }

    func[Stmt] _classDeclaration() {
        var[Int] start = this.current - 1;   // the `class`
        var[Str | Nil] interfaceName = nil;
        if (this._match(TokenType.L_BRACKET)) {
            interfaceName = this._consume(TokenType.IDENTIFIER, "Expect interface name.").lexeme;
            this._consume(TokenType.R_BRACKET, "Expect ']' after interface name.");
        }
        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect class name.").lexeme;
        var[Str<>] typeParams = <>;
        if (this._match(TokenType.COLON)) typeParams = this._parseTypeParams();

        var[Str | Nil] parentClassName = nil;
        var[TypeNode | Nil] parentType = nil;
        if (this._match(TokenType.L_PAREN)) {
            var[Str] parent = this._consume(TokenType.IDENTIFIER, "Expect parent class name.").lexeme;
            while (this._match(TokenType.DOT)) {   // class Circle(shapes.Shape)
                parent += "." + this._consume(TokenType.IDENTIFIER, "Expect class name after '.'.").lexeme;
            }
            parentClassName = parent;
            if (this._match(TokenType.COLON)) {    // class IntBox(Box:Int)
                var[TypeNode] filled = new TypeNode();
                filled.name = parent;
                filled.typeArgs = this._parseTypeArgs();
                parentType = filled;
            }
            this._consume(TokenType.R_PAREN, "Expect ')' after parent class name.");
        }

        this._consume(TokenType.L_BRACE, "Expect '{' before class body.");
        var[Stmt<>] members = <>;
        var[Stmt<>] methodTemplates = <>;
        while (!this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
            var[Token] memberStart = this._peek();
            var[ParserState] saved = this._save();
            try {
                var[Token<>] annotations = this._parseAnnotations();
                var[Stmt] member = new Stmt();
                if (this._match(TokenType.VAR)) member = this._varDeclaration();
                else if (this._match(TokenType.CONST)) member = this._constDeclaration();
                else if (this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "async" && this._checkNext(TokenType.FUNC)) {
                    this._advance();
                    this._advance();
                    this.asyncNext = true;
                    member = this._funcDeclaration();
                }
                else if (this._match(TokenType.FUNC)) member = this._funcDeclaration();
                else if (this._match(TokenType.INIT)) member = this._constructorDeclaration();
                else throw this._error(this._peek(), "Expect var, const, func, or init declaration inside class.");
                if (!annotations.isEmpty()) {
                    if (member is FuncDeclStmt) member.annotations = annotations;
                    else this._error(annotations<0>, "Decorators like @override go before methods");
                }
                this._placeS(member, memberStart);
                members.push(member);
                for (var[Stmt] more in this._takeMoreDeclarations()) members.push(more);
            } catch (e[ParseError]) {
                this._restore(saved);
                this.moreDeclarations = <>;
                this._synchronize();
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after class body.");

        // func[T<>] all:T() inside the class: generic methods, filled in when used
        var[Stmt<>] plain = <>;
        for (var[Stmt] member in members) {
            var[Bul] generic = false;
            if (member is FuncDeclStmt) generic = !member.typeParams.isEmpty();
            if (generic) methodTemplates.push(member);
            else plain.push(member);
        }
        var[ClassDeclStmt] klass = new ClassDeclStmt(name, interfaceName, parentClassName, plain);
        klass.methodTemplates = methodTemplates;
        klass.parentType = parentType;
        if (!typeParams.isEmpty()) {
            klass.typeParams = typeParams;
            klass.templateTokens = this.tokens.slice(start, this.current);
            klass.templateTokens.push(new Token(TokenType.END_OF_FILE, "", this._previous().line, this._previous().column));
        }
        return klass;
    }

    // @override, or several: @a, @b, @c
    func[Token<>] _parseAnnotations() {
        var[Token<>] annotations = <>;
        while (this._match(TokenType.AT)) {
            if (this._check(TokenType.L_PAREN)) {
                throw this._error(this._peek(), "@(...) holds markup, in a component of an .xds file. Decorators are written @name, and several @a, @b.");
            }
            annotations.push(this._consume(TokenType.IDENTIFIER, "Expect a decorator name after '@', e.g. @override."));
            if (this._match(TokenType.COMMA) && !this._check(TokenType.AT)) this._error(this._peek(), "Expect another decorator after ',', e.g. @a, @b.");
        }
        return annotations;
    }

    func[Stmt] _enumDeclaration() {
        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect enum name.").lexeme;
        this._consume(TokenType.L_BRACE, "Expect '{' before enum values.");
        var[EnumEntry<>] values = <>;
        if (!this._check(TokenType.R_BRACE)) {
            loop {
                if (this._check(TokenType.R_BRACE)) break;   // trailing comma
                var[Str] valueName = this._consume(TokenType.IDENTIFIER, "Expect enum value.").lexeme;
                var[Expr | Nil] value = nil;
                if (this._match(TokenType.L_PAREN)) {
                    value = this.expression();
                    this._consume(TokenType.R_PAREN, "Expect ')' after enum value.");
                }
                values.push(new EnumEntry(valueName, value));
                if (!this._match(TokenType.COMMA)) break;
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after enum values.");
        return new EnumDeclStmt(name, values);
    }

    func[Stmt] _interfaceDeclaration() {
        var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect interface name.").lexeme;
        this._consume(TokenType.L_BRACE, "Expect '{' before interface members.");
        var[Stmt<>] members = <>;
        while (!this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
            var[Token] start = this._peek();
            if (this._check(TokenType.IDENTIFIER)) {
                var[Str] fieldName = this._advance().lexeme;
                this._consume(TokenType.L_BRACKET, "Expect '[' before field type.");
                var[TypeNode] type = this._parseType();
                this._consume(TokenType.R_BRACKET, "Expect ']' after field type.");
                this._consume(TokenType.SEMICOLON, "Expect ';' after interface field declaration.");
                var[Stmt] field = new VarDeclStmt(false, type, fieldName, nil);
                this._placeS(field, start);
                members.push(field);
            } else if (this._match(TokenType.FUNC)) {
                this._consume(TokenType.L_BRACKET, "Expect '[' before return type.");
                var[TypeNode] returnType = this._parseType();
                var[TypeNode<>] throwsTypes = this._parseThrows();
                this._consume(TokenType.R_BRACKET, "Expect ']' after return type.");
                var[Str] methodName = this._consume(TokenType.IDENTIFIER, "Expect interface method name.").lexeme;
                this._consume(TokenType.L_PAREN, "Expect '(' after method name.");
                var[Param<>] params = this._parseParamList();
                this._consume(TokenType.R_PAREN, "Expect ')' after interface method parameters.");
                this._consume(TokenType.SEMICOLON, "Expect ';' after interface method declaration.");
                var[FuncDeclStmt] method = new FuncDeclStmt(returnType, methodName, params, nil);
                method.throwsTypes = throwsTypes;
                this._placeS(method, start);
                members.push(method);
            } else {
                throw this._error(this._peek(), "Expect a field (name[Type];) or method (func[Type] name();) inside interface.");
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after interface members.");
        return new InterfaceDeclStmt(name, members);
    }

    // The old form `{ Name (as Alias)? (, ...)* } from` (reported)
    func[Bul] _isImportAhead() {
        if (!this._check(TokenType.L_BRACE)) return false;
        var[Int] i = 1;
        loop {
            if (this._peekAt(i).type != TokenType.IDENTIFIER) return false;
            i++;
            if (this._peekAt(i).type == TokenType.AS) {
                if (this._peekAt(i + 1).type != TokenType.IDENTIFIER) return false;
                i += 2;
            }
            if (this._peekAt(i).type == TokenType.COMMA) {
                i++;
                continue;
            }
            break;
        }
        return this._peekAt(i).type == TokenType.R_BRACE && this._peekAt(i + 1).type == TokenType.IDENTIFIER && this._peekAt(i + 1).lexeme == "from";
    }

    // $("a.b", "c")  /  $("math")
    func[ModuleSpec<>] _parseModuleSpecs() {
        this._consume(TokenType.DOLLAR, "Expect '$' before module list.");
        this._consume(TokenType.L_PAREN, "Expect '(' after '$'.");
        var[ModuleSpec<>] specs = <>;
        loop {
            if (!specs.isEmpty() && this._check(TokenType.R_PAREN)) break;   // trailing comma
            var[ModuleSpec] spec = new ModuleSpec();
            spec.line = this._peek().line;
            spec.column = this._peek().column;
            if (this._check(TokenType.L_ANGLE)) {
                // The old $(<math>): reported, but still understood
                var[Token] angle = this._advance();
                spec.path = this._consume(TokenType.IDENTIFIER, "Expect library name after '<'.").lexeme;
                while (this._match(TokenType.DOT)) {
                    spec.path += "." + this._consume(TokenType.IDENTIFIER, "Expect name after '.' in library path.").lexeme;
                }
                this._consume(TokenType.R_ANGLE, "Expect '>' after library name.");
                this._error(angle, "Libraries are imported by name without < >: $(\"" + spec.path + "\")");
            } else {
                var[Token] path = this._consume(TokenType.STRING, "Expect a module or library name in quotes, e.g. $(\"math\").");
                if (path.lexeme == "") throw this._error(path, "Module path cannot be empty.");
                spec.path = path.lexeme;
            }
            if (this._match(TokenType.AS)) {
                spec.alias = this._consume(TokenType.IDENTIFIER, "Expect a name after 'as', e.g. $(\"math\" as m).").lexeme;
            }
            specs.push(spec);
            if (!this._match(TokenType.COMMA)) break;
        }
        this._consume(TokenType.R_PAREN, "Expect ')' after module list.");
        return specs;
    }

    // { a, b as c }
    func[ImportName<>] _parseImportNames() {
        var[ImportName<>] names = <>;
        this._consume(TokenType.L_BRACE, "Expect '{' before imported names.");
        if (!this._check(TokenType.R_BRACE)) {
            loop {
                if (this._check(TokenType.R_BRACE)) break;   // trailing comma
                var[ImportName] item = new ImportName();
                var[Token] nameToken = this._consume(TokenType.IDENTIFIER, "Expect imported name.");
                item.name = nameToken.lexeme;
                item.line = nameToken.line;
                item.column = nameToken.column;
                if (this._match(TokenType.AS)) item.alias = this._consume(TokenType.IDENTIFIER, "Expect alias after 'as'.").lexeme;
                names.push(item);
                if (!this._match(TokenType.COMMA)) break;
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after imported names.");
        return names;
    }

    func[Stmt] _importStatement() {
        // The old form: { a } from $("x"); -- understood, but reported
        if (this._check(TokenType.L_BRACE)) {
            var[Token] start = this._peek();
            var[ImportName<>] oldNames = this._parseImportNames();
            if (!(this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "from")) throw this._error(this._peek(), "Expect 'from' after imported names.");
            this._advance();
            var[ModuleSpec<>] oldModules = this._parseModuleSpecs();
            this._match(TokenType.SEMICOLON);
            var[Str<>] list = <>;
            for (var[ImportName] n in oldNames) {
                var[Str | Nil] alias = n.alias;
                list.push(n.name + (alias != nil ? " as " + alias : ""));
            }
            var[Str<>] specs = <>;
            for (var[ModuleSpec] m in oldModules) specs.push("\"" + m.path + "\"");
            this._error(start, "Imports are written $(" + specs.join(", ") + ") { " + list.join(", ") + " } now");
            return new ImportStmt(oldNames, oldModules, false);
        }
        var[ImportName<>] names = <>;
        var[Bul] importAll = true;
        var[ModuleSpec<>] modules = this._parseModuleSpecs();
        if (this._check(TokenType.L_BRACE)) {
            names = this._parseImportNames();
            importAll = false;
        }
        this._match(TokenType.SEMICOLON);   // optional
        return new ImportStmt(names, modules, importAll);
    }

    // ---- Statements ----

    func[Stmt | Nil] _statement() {
        this._enterNesting();
        var[Token] start = this._peek();
        var[Stmt | Nil] s = this._statementInner();
        nestingLevel--;
        return this._placeS(s, start);
    }

    func[Stmt | Nil] _statementInner() {
        if (this._match(TokenType.IF)) return this._ifStatement();
        if (this._match(TokenType.SWITCH)) return this._switchStatement();
        if (this._match(TokenType.FOR)) return this._forStatement();
        if (this._match(TokenType.WHILE)) return this._whileStatement();
        if (this._match(TokenType.LOOP)) return this._loopStatement();
        if (this._match(TokenType.RETURN)) return this._returnStatement();
        if (this._match(TokenType.BREAK)) {
            this._consume(TokenType.SEMICOLON, "Expect ';' after break.");
            return new BreakStmt();
        }
        if (this._match(TokenType.CONTINUE)) {
            this._consume(TokenType.SEMICOLON, "Expect ';' after continue.");
            return new ContinueStmt();
        }
        if (this._match(TokenType.TRY)) return this._tryStatement();
        if (this._match(TokenType.THROW)) {
            var[Expr] value = this.expression();
            this._consume(TokenType.SEMICOLON, "Expect ';' after throw expression.");
            return new ThrowStmt(value);
        }
        if (this._match(TokenType.L_BRACE)) return this._blockStatement();
        if (this.componentFile && this._strayMarkupAhead()) {
            this._error(this._peek(), "Markup needs @(...) around it: @(<" + this._peekAt(1).lexeme + " ... />)");
            // To the end of the block this is in
            var[Int] depth = 0;
            while (!this._isAtEnd() && !(depth == 0 && this._check(TokenType.R_BRACE))) {
                if (this._check(TokenType.L_BRACE)) depth++;
                if (this._check(TokenType.R_BRACE)) depth--;
                this._advance();
            }
            return nil;
        }
        return this._expressionStatement();
    }

    // Called after '{' has been consumed
    func[Stmt] _blockStatement() {
        this._enterNesting();
        var[Token] open = this._previous();
        var[Stmt<>] statements = <>;
        this.blockDepth++;
        while (!this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
            var[Stmt | Nil] stmt = this.declaration();
            var[Bul] markup = false;
            if (stmt is ReturnStmt) markup = stmt.isMarkup;
            if (stmt != nil) statements.push(stmt);
            for (var[Stmt] more in this._takeMoreDeclarations()) statements.push(more);
            if (markup && !this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
                this._error(this._peek(), "Nothing can follow @(...) in the same block: it returns.");
                this._synchronize();
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after block.");
        this.blockDepth--;
        nestingLevel--;
        var[Stmt] block = new BlockStmt(statements);
        this._placeS(block, open);
        return block;
    }

    // The body of if/else/for/while: a { block } or a single statement
    func[Stmt | Nil] _bodyStatement() {
        if (this._match(TokenType.L_BRACE)) return this._blockStatement();
        return this._statement();
    }

    // <div> or <Title text="x" /> at the start of a statement, written without @()
    func[Bul] _strayMarkupAhead() {
        if (!this._check(TokenType.L_ANGLE) || this._peekAt(1).type != TokenType.IDENTIFIER) return false;
        var[TokenType] after = this._peekAt(2).type;
        if (after == TokenType.IDENTIFIER || after == TokenType.DIV) return true;
        if (after != TokenType.R_ANGLE) return false;
        var[TokenType] then = this._peekAt(3).type;   // <a>.forEach(...) and <a>; are lists
        return then != TokenType.DOT && then != TokenType.SEMICOLON && then != TokenType.QUESTION_DOT;
    }

    func[Stmt] _expressionStatement() {
        var[Token] first = this._peek();
        var[Expr] expr = this.expression();
        // @(<markup>) as a statement returns it; its ';' is optional
        if (expr is MarkupExpr) {
            if (this.blockDepth == 0) {
                throw this._error(first, "@(...) on its own returns from a comp or function. At the top of a file, store it: var[Xml] data = @(...);");
            }
            this._match(TokenType.SEMICOLON);
            var[ReturnStmt] markup = new ReturnStmt(expr);
            markup.isMarkup = true;
            return markup;
        }
        // `...` is a placeholder body; its trailing ';' is optional
        var[Bul] placeholder = false;
        if (expr is LiteralExpr) placeholder = expr.token.type == TokenType.ELLIPSIS;
        if (placeholder) this._match(TokenType.SEMICOLON);
        else this._consume(TokenType.SEMICOLON, "Expect ';' after expression.");
        return new ExpressionStmt(expr);
    }

    func[Stmt] _ifStatement() {
        this._consume(TokenType.L_PAREN, "Expect '(' after 'if'.");
        var[Expr] condition = this.expression();
        this._consume(TokenType.R_PAREN, "Expect ')' after if condition.");
        var[Stmt | Nil] thenBranch = this._bodyStatement();
        var[Stmt | Nil] elseBranch = nil;
        if (this._match(TokenType.ELSE)) {
            var[Token] start = this._peek();
            if (this._match(TokenType.IF)) {
                this.elseIfChain++;
                this._checkChain(this.elseIfChain, "chain of else if");
                elseBranch = this._placeS(this._ifStatement(), start);
                this.elseIfChain--;
            } else {
                elseBranch = this._bodyStatement();
            }
        }
        return new IfStmt(condition, thenBranch, elseBranch);
    }

    func[Stmt] _switchStatement() {
        this._consume(TokenType.L_PAREN, "Expect '(' after 'switch'.");
        var[Expr] discriminant = this.expression();
        this._consume(TokenType.R_PAREN, "Expect ')' after switch condition.");
        this._consume(TokenType.L_BRACE, "Expect '{' before switch cases.");
        var[SwitchCase<>] cases = <>;
        while (!this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
            if (this._match(TokenType.CASE)) {
                var[Expr] value = this.expression();
                this._consume(TokenType.COLON_EQUAL, "Expect ':=' after case value.");
                this._consume(TokenType.L_BRACE, "Expect '{' before case body.");
                cases.push(new SwitchCase(value, this._blockStatement()));
            } else if (this._match(TokenType.DEFAULT)) {
                this._consume(TokenType.COLON_EQUAL, "Expect ':=' after default.");
                this._consume(TokenType.L_BRACE, "Expect '{' before default body.");
                cases.push(new SwitchCase(nil, this._blockStatement()));
            } else {
                throw this._error(this._peek(), "Expect 'case' or 'default' inside switch.");
            }
        }
        this._consume(TokenType.R_BRACE, "Expect '}' after switch block.");
        return new SwitchStmt(discriminant, cases);
    }

    // for (var[T] x in iterable) body  /  for (init; condition; increment) body
    func[Stmt] _forStatement() {
        this._consume(TokenType.L_PAREN, "Expect '(' after 'for'.");
        var[Stmt | Nil] start = nil;
        if (this._check(TokenType.VAR)) {
            var[Token] varToken = this._advance();
            this._consume(TokenType.L_BRACKET, "Expect '[' before loop variable type.");
            var[TypeNode] type = this._parseType();
            this._consume(TokenType.R_BRACKET, "Expect ']' after loop variable type.");
            var[Str] name = this._consume(TokenType.IDENTIFIER, "Expect loop variable name.").lexeme;
            if (this._match(TokenType.IN)) {
                var[Stmt] decl = new VarDeclStmt(false, type, name, nil);
                this._placeS(decl, varToken);
                var[Expr] iterable = this.expression();
                this._consume(TokenType.R_PAREN, "Expect ')' after iterable.");
                var[Stmt | Nil] body = this._bodyStatement();
                return new ForInStmt(decl, iterable, body);
            }
            var[Expr | Nil] initializer = nil;
            if (this._match(TokenType.EQUAL)) initializer = this.expression();
            this._consume(TokenType.SEMICOLON, "Expect ';' after loop initializer.");
            start = this._placeS(new VarDeclStmt(false, type, name, initializer), varToken);
        } else if (!this._match(TokenType.SEMICOLON)) {
            start = this._statement();   // an expression statement, with its ';'
        }
        var[Expr | Nil] condition = nil;
        if (!this._check(TokenType.SEMICOLON)) condition = this.expression();
        this._consume(TokenType.SEMICOLON, "Expect ';' after loop condition.");
        var[Expr | Nil] increment = nil;
        if (!this._check(TokenType.R_PAREN)) increment = this.expression();
        this._consume(TokenType.R_PAREN, "Expect ')' after for clauses.");
        var[Stmt | Nil] body = this._bodyStatement();
        return new ForStmt(start, condition, increment, body);
    }

    func[Stmt] _whileStatement() {
        this._consume(TokenType.L_PAREN, "Expect '(' after 'while'.");
        var[Expr] condition = this.expression();
        this._consume(TokenType.R_PAREN, "Expect ')' after while condition.");
        var[Stmt | Nil] body = this._bodyStatement();
        return new WhileStmt(condition, body);
    }

    func[Stmt] _loopStatement() {
        this._consume(TokenType.L_BRACE, "Expect '{' before loop body.");
        return new LoopStmt(this._blockStatement());
    }

    func[Stmt] _returnStatement() {
        var[Expr | Nil] value = nil;
        var[Token] at = this._previous();
        if (!this._check(TokenType.SEMICOLON)) {
            value = this.expression();
        } else if (this.inComp) {
            value = this._placeE(new LiteralExpr(new Token(TokenType.NIL, "nil", at.line, at.column)), at);   // a comp with nothing to show
        }
        var[Bul] markup = value is MarkupExpr;
        if (markup) this._match(TokenType.SEMICOLON);
        else this._consume(TokenType.SEMICOLON, "Expect ';' after return value.");
        var[ReturnStmt] result = new ReturnStmt(value);
        result.isMarkup = markup;
        return result;
    }

    func[Stmt] _tryStatement() {
        this._consume(TokenType.L_BRACE, "Expect '{' before try block.");
        var[Stmt] tryBlock = this._blockStatement();
        var[CatchClause<>] catches = <>;
        while (this._match(TokenType.CATCH)) {
            var[CatchClause] clause = new CatchClause();
            clause.line = this._previous().line;
            clause.column = this._previous().column;
            if (this._match(TokenType.L_PAREN)) {
                clause.param = this._consume(TokenType.IDENTIFIER, "Expect catch parameter name.").lexeme;
                this._consume(TokenType.L_BRACKET, "Expect '[' before catch parameter type.");
                clause.type = this._parseType();
                this._consume(TokenType.R_BRACKET, "Expect ']' after catch parameter type.");
                this._consume(TokenType.R_PAREN, "Expect ')' after catch parameter declaration.");
            }
            this._consume(TokenType.L_BRACE, "Expect '{' before catch block.");
            clause.body = this._blockStatement();
            catches.push(clause);
        }
        if (catches.isEmpty()) throw this._error(this._peek(), "Expect 'catch' after try block.");
        return new TryStmt(tryBlock, catches);
    }

    // ---- Expressions: precedence climbing ----

    func[Expr] _binary(left[Expr], op[Token], right[Expr]) {
        var[Int] chain = (left.chain > right.chain ? left.chain : right.chain) + 1;
        this._checkChain(chain, "expression");
        var[Expr] node = new BinaryExpr(left, op, right);
        node.line = left.line;
        node.column = left.column;
        node.chain = chain;
        return node;
    }

    func[Expr] expression() { return this._assignment(); }

    func[Expr] _assignment() {
        this._enterNesting();
        var[Expr] expr = this._ternary();
        if (this._matchAny(<TokenType.EQUAL, TokenType.PLUS_EQUAL, TokenType.MINUS_EQUAL, TokenType.MULT_EQUAL, TokenType.DIV_EQUAL,
                             TokenType.EXPONENT_EQUAL, TokenType.MOD_EQUAL, TokenType.QUESTION_EQUAL>)) {
            var[Token] op = this._previous();
            var[Expr] value = this._assignment();
            var[Expr] node = new AssignExpr(expr, op, value);
            node.line = expr.line;
            node.column = expr.column;
            nestingLevel--;
            return node;
        }
        nestingLevel--;
        return expr;
    }

    // x |> f(a)  is  f(x, a);   x |> f  is  f(x)
    func[Expr] _pipe() {
        var[Expr] expr = this._nullishCoalescing();
        var[Int] links = 0;
        while (this._match(TokenType.FUNC_PIPE)) {
            this._checkChain(links, "chain of |>");
            var[Token] op = this._previous();
            var[Int] line = expr.line;
            var[Int] column = expr.column;
            var[Expr] target = this._call();
            if (!(target is CallExpr) && !(target is IdentifierExpr) && !(target is MemberExpr) && !(target is LambdaExpr) && !(target is GroupingExpr)) {
                throw this._error(op, "After |> comes a function or a call, e.g. x |> f or x |> f(2)");
            }
            var[Argument] first = new Argument();
            first.value = expr;
            if (target is CallExpr) {
                target.args.insert(0, first);
                expr = target;
            } else {
                expr = new CallExpr(target, <first>, op);
            }
            expr.line = line;
            expr.column = column;
            links++;
        }
        return expr;
    }

    func[Expr] _ternary() {
        var[Expr] condition = this._pipe();
        if (this._match(TokenType.QUESTION)) {
            var[Bul] savedColon = this.colonEnds;
            this.colonEnds = true;
            var[Expr] thenBranch = this.expression();
            this.colonEnds = savedColon;
            this._consume(TokenType.COLON, "Expect ':' after ternary 'then' branch.");
            var[Expr] elseBranch = this._ternary();
            var[Expr] node = new TernaryExpr(condition, thenBranch, elseBranch);
            node.line = condition.line;
            node.column = condition.column;
            return node;
        }
        return condition;
    }

    func[Expr] _nullishCoalescing() {
        var[Expr] expr = this._logicalOr();
        while (this._matchAny(<TokenType.QUESTION_QUESTION, TokenType.QUESTION_BANG>)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._logicalOr());
        }
        return expr;
    }

    func[Expr] _logicalOr() {
        var[Expr] expr = this._logicalAnd();
        while (this._match(TokenType.OR_OR)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._logicalAnd());
        }
        return expr;
    }

    func[Expr] _logicalAnd() {
        var[Expr] expr = this._bitwiseOr();
        while (this._match(TokenType.AND_AND)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._bitwiseOr());
        }
        return expr;
    }

    func[Expr] _bitwiseOr() {
        var[Expr] expr = this._bitwiseXor();
        while (this._match(TokenType.BITWISE_OR)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._bitwiseXor());
        }
        return expr;
    }

    func[Expr] _bitwiseXor() {
        var[Expr] expr = this._bitwiseAnd();
        while (this._match(TokenType.BITWISE_XOR)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._bitwiseAnd());
        }
        return expr;
    }

    func[Expr] _bitwiseAnd() {
        var[Expr] expr = this._equality();
        while (this._match(TokenType.BITWISE_AND)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._equality());
        }
        return expr;
    }

    func[Expr] _equality() {
        var[Expr] expr = this._comparison();
        if (this._matchAny(<TokenType.EQUAL_EQUAL, TokenType.BANG_EQUAL>)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._comparison());
            if (this._check(TokenType.EQUAL_EQUAL) || this._check(TokenType.BANG_EQUAL)) {
                throw this._error(this._peek(), "Equality operators (==, !=) cannot be chained. Use logical operators (&&, ||).");
            }
        }
        return expr;
    }

    func[Expr] _comparison() {
        var[Expr] expr = this._shift();
        if (this._match(TokenType.IS)) {
            var[TypeNode] type = this._parseType();
            var[Expr] node = new IsExpr(expr, type);
            node.line = expr.line;
            node.column = expr.column;
            return node;
        }
        if (this._matchAny(<TokenType.L_ANGLE, TokenType.LESS_EQUAL, TokenType.GREATER_EQUAL>) || (!this.inAngleList && this._match(TokenType.R_ANGLE))) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._shift());
            if (this._check(TokenType.L_ANGLE) || this._check(TokenType.LESS_EQUAL) || (this._check(TokenType.R_ANGLE) && !this.inAngleList) ||
                this._check(TokenType.GREATER_EQUAL)) {
                throw this._error(this._peek(), "Comparison operators (<, >, <=, >=) cannot be chained. Use logical operators (&&, ||).");
            }
        }
        return expr;
    }

    func[Expr] _shift() {
        var[Expr] expr = this._additive();
        while (this._match(TokenType.LEFT_SHIFT) || (!this.inAngleList && this._match(TokenType.RIGHT_SHIFT))) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._additive());
        }
        return expr;
    }

    func[Expr] _additive() {
        var[Expr] expr = this._multiplicative();
        while (this._matchAny(<TokenType.PLUS, TokenType.MINUS>)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._multiplicative());
        }
        return expr;
    }

    func[Expr] _multiplicative() {
        var[Expr] expr = this._unary();
        while (this._matchAny(<TokenType.MULT, TokenType.DIV, TokenType.MOD>)) {
            var[Token] op = this._previous();
            expr = this._binary(expr, op, this._unary());
        }
        return expr;
    }

    func[Expr] _unary() {
        if (this._check(TokenType.IDENTIFIER) && this._peek().lexeme == "await") {
            var[TokenType] next = this._peekAt(1).type;
            var[Bul] startsValue = next == TokenType.IDENTIFIER || next == TokenType.NEW || next == TokenType.THIS || next == TokenType.L_PAREN;
            if (this.inAsync && startsValue) {
                this._enterNesting();
                var[Token] at = this._advance();
                var[Expr] operand = this._unary();
                nestingLevel--;
                return this._placeE(new AwaitExpr(operand), at);
            }
            if (next == TokenType.IDENTIFIER || next == TokenType.NEW) {
                throw this._error(this._peek(), "await only works directly in an async function: async func[Emp] f() { ... }");
            }
        }
        if (this._matchAny(<TokenType.NOT, TokenType.MINUS, TokenType.BITWISE_NOT>)) {
            this._enterNesting();
            var[Token] op = this._previous();
            var[Expr] operand = this._unary();
            nestingLevel--;
            return this._placeE(new UnaryExpr(op, operand), op);
        }
        return this._exponent();
    }

    // ^ is right-associative and binds tighter than unary minus on its left
    func[Expr] _exponent() {
        var[Expr] left = this._postfix();
        if (this._match(TokenType.EXPONENT)) {
            var[Token] op = this._previous();
            return this._binary(left, op, this._unary());
        }
        return left;
    }

    func[Expr] _postfix() {
        var[Expr] expr = this._call();
        if (this._matchAny(<TokenType.PLUS_PLUS, TokenType.MINUS_MINUS>)) {
            var[Token] op = this._previous();
            var[Expr] node = new PostfixExpr(op, expr);
            node.line = expr.line;
            node.column = expr.column;
            return node;
        }
        return expr;
    }

    func[Expr] _call() {
        var[Expr] expr = this._primary();
        var[Int] links = 0;
        loop {
            this._checkChain(links, "chain of calls");
            var[Int] line = expr.line;
            var[Int] column = expr.column;
            if (this._match(TokenType.L_PAREN)) {
                // super(...) is short for super.init(...)
                var[Bul] isSuper = false;
                if (expr is IdentifierExpr) isSuper = expr.name.type == TokenType.SUPER;
                if (isSuper) {
                    var[Token] superToken = this._previous();
                    if (expr is IdentifierExpr) superToken = expr.name;
                    expr = new MemberExpr(expr, new Token(TokenType.INIT, "init", superToken.line, superToken.column), false);
                    expr.line = line;
                    expr.column = column;
                }
                expr = this._finishCall(expr);
            } else if (this._matchAny(<TokenType.DOT, TokenType.QUESTION_DOT>)) {
                var[Bul] optional = this._previous().type == TokenType.QUESTION_DOT;
                var[Token] name = this._check(TokenType.INIT) ? this._advance() : this._consume(TokenType.IDENTIFIER, "Expect member name after '.'.");
                var[TypeNode<>] typeArgs = <>;
                if (!this.colonEnds && this._check(TokenType.COLON) && this._typeArgAhead()) {   // db.all:Person(...)
                    var[Int] position = this.current;
                    var[Int] errorCount = this.errors.length();
                    var[ParserState] saved = this._save();
                    try {
                        this._advance();
                        typeArgs = this._parseTypeArgs();
                        if (!this._check(TokenType.L_PAREN)) {
                            typeArgs = <>;
                            this._reset(position, errorCount);
                        }
                    } catch (e[ParseError]) {
                        this._restore(saved);
                        typeArgs = <>;
                        this._reset(position, errorCount);
                    }
                }
                var[MemberExpr] member = new MemberExpr(expr, name, optional);
                member.typeArgs = typeArgs;
                expr = member;
            } else if (this._check(TokenType.L_ANGLE)) {
                var[Expr | Nil] attempt = this._tryParseIndex(expr);
                if (attempt == nil) break;
                expr = attempt;
            } else {
                break;
            }
            expr.line = line;
            expr.column = column;
            links++;
        }
        return expr;
    }

    // students<0> vs. a < b: an index is `<` additive-expression `>`. If that
    // shape doesn't match, rewind and let comparison() treat `<` as less-than.
    func[Expr | Nil] _tryParseIndex(object[Expr]) {
        var[Int] attempt = this.current * 8 + (this.inAngleList ? 4 : 0) + (this.indexDepth > 0 ? 2 : 0) + (this.colonEnds ? 1 : 0);
        if (this.notAnIndex.has(attempt)) return nil;
        var[Int] position = this.current;
        var[Int] errorCount = this.errors.length();
        var[ParserState] saved = this._save();
        this._advance();   // '<'
        try {
            this.indexDepth++;
            var[Expr] index = this._additive();
            this.indexDepth--;
            if (this._check(TokenType.RIGHT_SHIFT) && (this.inAngleList || this.indexDepth > 0)) this._splitDoubleAngle(TokenType.R_ANGLE);
            if (this._check(TokenType.R_ANGLE)) {
                this._advance();
                return new IndexExpr(object, index);
            }
        } catch (e[ParseError]) {
            this._restore(saved);   // not an index
        }
        this._reset(position, errorCount);
        this.notAnIndex<attempt> = true;
        return nil;
    }

    func[Argument<>] _parseArguments() {
        var[Bul] savedAngle = this.inAngleList;
        var[Bul] savedColon = this.colonEnds;
        this.inAngleList = false;
        this.colonEnds = false;
        var[Argument<>] args = <>;
        if (!this._check(TokenType.R_PAREN)) {
            loop {
                var[Argument] arg = new Argument();
                // name = value, or the old name: value -- but first:Int(...) (no spaces) is a generic call
                var[Bul] genericCall = this._checkNext(TokenType.COLON) && this._typeArgAheadAt(2) &&
                                       this._peekAt(1).column == this._peek().column + byteLength(this._peek().lexeme) &&
                                       this._peekAt(2).column == this._peekAt(1).column + 1 && this._peekAt(2).line == this._peek().line;
                if (this._check(TokenType.IDENTIFIER) && this._checkNext(TokenType.EQUAL)) {
                    arg.name = this._advance().lexeme;
                    this._advance();   // '='
                } else if (this._check(TokenType.IDENTIFIER) && this._checkNext(TokenType.COLON) && !genericCall) {
                    arg.name = this._advance().lexeme;
                    arg.oldNamed = true;
                    this._advance();   // ':'
                }
                arg.isSpread = this._match(TokenType.ELLIPSIS);
                var[Token] start = this._peek();
                arg.value = this.expression();
                var[Expr] inner = arg.value;
                while (inner is GroupingExpr) inner = inner.inner;
                if (inner is AssignExpr) this._error(start, "An assignment can't be an argument; assign on the line before the call");
                args.push(arg);
                if (!this._match(TokenType.COMMA)) break;
            }
        }
        this.inAngleList = savedAngle;
        this.colonEnds = savedColon;
        return args;
    }

    func[Expr] _finishCall(callee[Expr]) {
        var[Argument<>] args = this._parseArguments();
        var[Token] paren = this._consume(TokenType.R_PAREN, "Expect ')' after arguments.");
        return new CallExpr(callee, args, paren);
    }

    // Called after '(' has been consumed
    func[Expr] _parseLambdaOrGrouping(openParen[Token]) {
        var[Bul] savedAngle = this.inAngleList;
        var[Bul] savedColon = this.colonEnds;
        this.inAngleList = false;
        this.colonEnds = false;
        var[Bul] isLambda = (this._check(TokenType.R_PAREN) && this._checkNext(TokenType.COLON_EQUAL)) ||
                            (this._check(TokenType.IDENTIFIER) && this._checkNext(TokenType.L_BRACKET));
        if (isLambda) {
            var[Param<>] params = this._parseParamList();
            this._consume(TokenType.R_PAREN, "Expect ')' after lambda parameters.");
            this._consume(TokenType.COLON_EQUAL, "Expect ':=' after lambda parameters.");
            var[Stmt] body = new Stmt();
            var[Bul] savedComp = this.inComp;
            var[Bul] savedAsync = this.inAsync;
            if (this._match(TokenType.L_BRACE)) {
                this.inComp = false;
                this.inAsync = false;
                body = this._blockStatement();
                this.inComp = savedComp;
                this.inAsync = savedAsync;
            } else {
                // An expression body runs on into the surrounding list, if any: <(x[Int]) := (x > 1)>
                this.inAngleList = savedAngle;
                var[Token] start = this._peek();
                this.inAsync = false;
                body = new ExpressionStmt(this.expression());
                this._placeS(body, start);
                this.inAsync = savedAsync;
                this.inAngleList = false;
            }
            this.inAngleList = savedAngle;
            this.colonEnds = savedColon;
            return this._placeE(new LambdaExpr(params, body), openParen);
        }
        var[Expr] expr = this.expression();
        this._consume(TokenType.R_PAREN, "Expect ')' after expression.");
        this.inAngleList = savedAngle;
        this.colonEnds = savedColon;
        return this._placeE(new GroupingExpr(expr), openParen);
    }

    // $"Hello {name}, you are {age + 1}" -> literal parts + parsed interpolations
    func[Expr] _parseTemplateString(token[Token]) {
        var[Chr<>] text = token.lexeme.chars();
        // where each character starts, in bytes (columns count bytes)
        var[Int<>] bytesBefore = <>;
        var[Int] total = 0;
        for (var[Chr] c in text) {
            bytesBefore.push(total);
            total += utf8Size(c);
        }
        bytesBefore.push(total);

        var[Str<>] literals = <>;
        var[Expr<>] interpolations = <>;
        var[Str<>] pending = <>;
        var[Int] i = 0;
        while (i < text.length()) {
            var[Chr] c = text<i>;
            if (c == '\\' && i + 1 < text.length()) {
                pending.push(Str(c));
                pending.push(Str(text<i + 1>));
                i += 2;
                continue;
            }
            if (c == '}') throw this._error(token, "Unmatched '}' in template string (use \\} for a literal brace).");
            if (c != '{') {
                pending.push(Str(c));
                i++;
                continue;
            }
            // Find the matching '}'
            var[Int] depth = 1;
            var[Int] j = i + 1;
            var[Chr] quote = '\0';
            var[Bul] inQuote = false;
            while (j < text.length() && depth > 0) {
                var[Chr] d = text<j>;
                if (inQuote) {
                    if (d == '\\') j++;
                    else if (d == quote) inQuote = false;
                } else if (d == '"' || d == '\'') {
                    quote = d;
                    inQuote = true;
                } else if (d == '{') {
                    depth++;
                } else if (d == '}') {
                    depth--;
                }
                j++;
            }
            if (depth != 0) throw this._error(token, "Unterminated '{' in template string.");

            var[Str] exprText = slice(text, i + 1, j - 1);
            literals.push(unescape(pending.join("")));
            pending = <>;

            var[Lexer] innerLexer = new Lexer(exprText);
            var[Token<>] innerTokens = innerLexer.scanTokens();
            for (var[Token] t in innerTokens) {
                // Re-anchor positions to the template string in the source file
                t.column += token.column + 1 + bytesBefore<i>;
                t.line = token.line;
            }
            if (!innerLexer.errors.isEmpty() || onlySpacesTabs(exprText)) {
                throw this._error(token, "Invalid interpolation '{" + exprText + "}' in template string.");
            }
            var[Parser] innerParser = new Parser(innerTokens);
            innerParser.inAsync = this.inAsync;
            var[Expr | Nil] inner = nil;
            var[ParserState] saved = this._save();
            try {
                inner = innerParser.expression();
                if (!innerParser._isAtEnd()) innerParser._error(innerParser._peek(), "Unexpected token in interpolation.");
            } catch (e[ParseError]) {
                this._restore(saved);
            }
            if (!innerParser.errors.isEmpty() || inner == nil) {
                for (var[Str] e in innerParser.errors) this.errors.push(e);
                throw new ParseError("interpolation");
            }
            interpolations.push(inner);
            i = j;
        }
        literals.push(unescape(pending.join("")));

        var[TemplateStringExpr] expr = new TemplateStringExpr(token);
        expr.literalParts = literals;
        expr.interpolations = interpolations;
        return this._placeE(expr, token);
    }

    // << or >> where lists open or close: two separate < or > tokens
    func[Emp] _splitDoubleAngle(single[TokenType]) {
        var[Token] old = this.tokens<this.current>;
        var[Str] lexeme = single == TokenType.L_ANGLE ? "<" : ">";
        this.tokens<this.current> = new Token(single, lexeme, old.line, old.column);
        this.tokens.insert(this.current + 1, new Token(single, lexeme, old.line, old.column + 1));
        this.notAnIndex = {:};   // the positions after this one have moved
    }

    func[Expr] _primary() {
        if (this._matchAny(<TokenType.INT_LITERAL, TokenType.FLOAT_LITERAL, TokenType.DOUBLE_LITERAL, TokenType.CHAR_LITERAL, TokenType.STRING,
                            TokenType.TRUE, TokenType.FALSE, TokenType.BINARY_LITERAL, TokenType.OCTAL_LITERAL, TokenType.HEX_LITERAL,
                            TokenType.NIL, TokenType.ELLIPSIS>)) {
            return this._placeE(new LiteralExpr(this._previous()), this._previous());
        }

        // Int("42"), Str(42), ... -- conversions
        if ((this._check(TokenType.TYPE_INT) || this._check(TokenType.TYPE_STR) || this._check(TokenType.TYPE_BUL) ||
             this._check(TokenType.TYPE_FLT) || this._check(TokenType.TYPE_DBL) || this._check(TokenType.TYPE_CHR)) &&
            this._checkNext(TokenType.L_PAREN)) {
            var[Token] type = this._advance();
            this._advance();   // '('
            var[Bul] savedAngle = this.inAngleList;
            this.inAngleList = false;
            if (this._check(TokenType.R_PAREN)) {
                throw this._error(this._peek(), type.lexeme + "(...) needs a value to convert, e.g. " + type.lexeme + "(x)");
            }
            var[Expr] value = this.expression();
            this._consume(TokenType.R_PAREN, "Expect ')' after the value to convert; a conversion takes one value.");
            this.inAngleList = savedAngle;
            return this._placeE(new ConvertExpr(type, value), type);
        }

        if (this._match(TokenType.TEMPLATE_STRING)) return this._parseTemplateString(this._previous());

        if (this._match(TokenType.MARKUP)) {
            var[Token] markup = this._previous();
            var[ParserState] saved = this._save();
            try {
                return this._parseMarkup(markup);
            } catch (e[ParseError]) {
                // Said already, and the block is behind us: the code around it can go on
                this._restore(saved);
                return this._placeE(new MarkupExpr(new MarkupNode()), markup);
            }
        }

        if (this._matchAny(<TokenType.IDENTIFIER, TokenType.THIS, TokenType.SUPER>)) {
            var[IdentifierExpr] identifier = new IdentifierExpr(this._previous());
            // pick:Int(items): a generic function with its types given
            if (!this.colonEnds && this._previous().type == TokenType.IDENTIFIER && this._check(TokenType.COLON) && this._typeArgAhead()) {
                var[Int] position = this.current;
                var[Int] errorCount = this.errors.length();
                var[ParserState] saved = this._save();
                try {
                    this._advance();
                    var[TypeNode<>] typeArgs = this._parseTypeArgs();
                    if (this._check(TokenType.L_PAREN)) identifier.typeArgs = typeArgs;
                    else this._reset(position, errorCount);
                } catch (e[ParseError]) {
                    this._restore(saved);
                    this._reset(position, errorCount);
                }
            }
            return this._placeE(identifier, identifier.name);
        }

        if (this._match(TokenType.NEW)) {
            var[Token] newToken = this._previous();
            // new { message = "Hi", code = 200 }: an anonymous object (an Obj)
            if (this._check(TokenType.L_BRACE)) {
                var[Bul] savedAngle = this.inAngleList;
                this.inAngleList = false;
                var[Token] brace = this._advance();
                var[Argument<>] fields = <>;
                var[(TypeNode | Nil)<>] types = <>;
                while (!this._check(TokenType.R_BRACE) && !this._isAtEnd()) {
                    // name = value, or var[Type] name = value
                    var[TypeNode | Nil] type = nil;
                    if (this._match(TokenType.VAR)) {
                        this._consume(TokenType.L_BRACKET, "Expect '[' after var.");
                        type = this._parseType();
                        this._consume(TokenType.R_BRACKET, "Expect ']' after the type.");
                    }
                    var[Argument] field = new Argument();
                    field.name = this._consume(TokenType.IDENTIFIER, "Expect a field name, e.g. new { message = \"Hi\" }.").lexeme;
                    if (!this._matchAny(<TokenType.EQUAL, TokenType.COLON>)) throw this._error(this._peek(), "Expect '=' after the field name.");
                    field.value = this.expression();
                    fields.push(field);
                    types.push(type);
                    if (!this._matchAny(<TokenType.COMMA, TokenType.SEMICOLON>)) break;
                }
                this._consume(TokenType.R_BRACE, "Expect '}' after the fields of the object.");
                this.inAngleList = savedAngle;
                var[NewExpr] anonymous = new NewExpr(brace, fields);
                anonymous.isAnonymous = true;
                anonymous.fieldTypes = types;
                return this._placeE(anonymous, newToken);
            }
            var[Token] classToken = this._consume(TokenType.IDENTIFIER, "Expect class name after 'new'.");
            var[Str] className = classToken.lexeme;
            while (this._match(TokenType.DOT)) {   // new m.Database()
                className += "." + this._consume(TokenType.IDENTIFIER, "Expect class name after '.'.").lexeme;
            }
            var[Token] nameToken = new Token(classToken.type, className, classToken.line, classToken.column);
            var[TypeNode<>] typeArgs = <>;
            if (this._check(TokenType.COLON)) {   // new Box:Int(5)
                this._advance();
                typeArgs = this._parseTypeArgs();
            }
            this._consume(TokenType.L_PAREN, "Expect '(' after class name.");
            var[Argument<>] args = this._parseArguments();
            this._consume(TokenType.R_PAREN, "Expect ')' after constructor arguments.");
            if (className == "Obj") {
                // new Obj(): an empty object, like new {}
                if (!args.isEmpty()) throw this._error(nameToken, "new Obj() takes no arguments; give it fields with new { name = value }");
                var[NewExpr] empty = new NewExpr(nameToken, <>);
                empty.isAnonymous = true;
                return this._placeE(empty, newToken);
            }
            var[NewExpr] made = new NewExpr(nameToken, args);
            made.typeArgs = typeArgs;
            return this._placeE(made, newToken);
        }

        // <1, 2, 3>, <> -- a list (<<1>, <2>> opens with <<)
        if (this._check(TokenType.L_ANGLE) || this._check(TokenType.LEFT_SHIFT)) {
            if (this._check(TokenType.LEFT_SHIFT)) this._splitDoubleAngle(TokenType.L_ANGLE);
            var[Token] open = this._advance();
            var[CollectionElement<>] elements = <>;
            var[Bul] savedAngle = this.inAngleList;
            this.inAngleList = true;
            if (this._check(TokenType.RIGHT_SHIFT)) this._splitDoubleAngle(TokenType.R_ANGLE);
            if (!this._check(TokenType.R_ANGLE)) {
                loop {
                    if (this._check(TokenType.RIGHT_SHIFT)) this._splitDoubleAngle(TokenType.R_ANGLE);
                    if (this._check(TokenType.R_ANGLE)) break;   // trailing comma
                    var[Bul] isSpread = this._match(TokenType.ELLIPSIS);
                    elements.push(new CollectionElement(isSpread, this.expression()));
                    if (!this._match(TokenType.COMMA)) break;
                }
            }
            if (this._check(TokenType.RIGHT_SHIFT)) this._splitDoubleAngle(TokenType.R_ANGLE);
            this.inAngleList = savedAngle;
            this._consume(TokenType.R_ANGLE, "Expect '>' after list elements (a comparison inside a list needs brackets: <(a > b)>).");
            // <1 > 2>: the > was meant as a comparison
            if (this._check(TokenType.INT_LITERAL) || this._check(TokenType.FLOAT_LITERAL) || this._check(TokenType.DOUBLE_LITERAL) ||
                this._check(TokenType.IDENTIFIER) || this._check(TokenType.STRING) || this._check(TokenType.CHAR_LITERAL) ||
                this._check(TokenType.TRUE) || this._check(TokenType.FALSE) || this._check(TokenType.NIL)) {
                throw this._error(this._peek(), "A comparison inside a list needs brackets: <(a > b)>");
            }
            return this._placeE(new ListExpr(elements), open);
        }

        // [1, 2]: the old way to write a list (warned about by the analyser)
        if (this._match(TokenType.L_BRACKET)) {
            var[Token] open = this._previous();
            var[CollectionElement<>] elements = <>;
            var[Bul] savedAngle = this.inAngleList;
            this.inAngleList = false;
            if (!this._check(TokenType.R_BRACKET)) {
                loop {
                    if (this._check(TokenType.R_BRACKET)) break;   // trailing comma
                    var[Bul] isSpread = this._match(TokenType.ELLIPSIS);
                    elements.push(new CollectionElement(isSpread, this.expression()));
                    if (!this._match(TokenType.COMMA)) break;
                }
            }
            this.inAngleList = savedAngle;
            this._consume(TokenType.R_BRACKET, "Expect ']' after list elements.");
            var[ListExpr] list = new ListExpr(elements);
            list.oldBrackets = true;
            return this._placeE(list, open);
        }

        if (this._match(TokenType.L_BRACE)) {
            var[Bul] savedAngle = this.inAngleList;
            this.inAngleList = false;
            var[Token] open = this._previous();
            // {:} -- an empty map
            if (this._check(TokenType.COLON) && this._checkNext(TokenType.R_BRACE)) {
                this._advance();
                this._advance();
                this.inAngleList = savedAngle;
                return this._placeE(new MapExpr(<>), open);
            }
            var[CollectionElement<>] elements = <>;
            var[MapEntry<>] entries = <>;
            var[Bul] isMap = false;
            if (!this._check(TokenType.R_BRACE)) {
                loop {
                    if (this._check(TokenType.R_BRACE)) break;   // trailing comma
                    var[Bul] isSpread = this._match(TokenType.ELLIPSIS);
                    var[Bul] savedColon = this.colonEnds;
                    this.colonEnds = true;
                    var[Expr] first = this.expression();
                    this.colonEnds = savedColon;
                    // The first `key: value` decides that this is a map (spreads before it were maps being copied in)
                    var[Bul] allSpread = true;
                    for (var[CollectionElement] e in elements) {
                        if (!e.isSpread) allSpread = false;
                    }
                    if (!isMap && !isSpread && this._check(TokenType.COLON) && allSpread) {
                        isMap = true;
                        for (var[CollectionElement] e in elements) entries.push(new MapEntry(nil, e.value));
                        elements = <>;
                    }
                    if (isMap) {
                        if (isSpread) {
                            entries.push(new MapEntry(nil, first));
                        } else {
                            this._consume(TokenType.COLON, "Expect ':' between key and value in a map literal.");
                            entries.push(new MapEntry(first, this.expression()));
                        }
                    } else {
                        elements.push(new CollectionElement(isSpread, first));
                    }
                    if (!this._match(TokenType.COMMA)) break;
                }
            }
            if (isMap) {
                this._consume(TokenType.R_BRACE, "Expect '}' after map entries.");
                this.inAngleList = savedAngle;
                return this._placeE(new MapExpr(entries), open);
            }
            this._consume(TokenType.R_BRACE, "Expect '}' after set elements.");
            this.inAngleList = savedAngle;
            return this._placeE(new SetExpr(elements), open);
        }

        if (this._match(TokenType.L_PAREN)) return this._parseLambdaOrGrouping(this._previous());

        if (this._check(TokenType.DOLLAR)) throw this._error(this._peek(), "Module references '$(...)' are only allowed in import statements.");
        if (this._peek().type == TokenType.WITH) throw this._error(this._peek(), $"'{this._peek().lexeme}' is reserved for future use.");
        throw this._error(this._peek(), "Expect expression.");
    }

    // ---- Markup: the raw block of @(<div>...</div>) ----

    // The code of a {...}, with positions in the file
    func[Expr] _parseMarkupExpression(token[Token], text[Str], line[Int], column[Int]) {
        var[Bul] blank = true;
        for (var[Chr] c in text) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') blank = false;
        }
        if (blank) throw this._error(token, "{ } in markup needs a value inside.");
        var[Lexer] lexer = new Lexer(text, true);
        var[Token<>] inner = lexer.scanTokens();
        for (var[Token] t in inner) {
            if (t.line == 1) t.column += column - 1;
            t.line += line - 1;
        }
        for (var[Str] e in lexer.errors) this.errors.push($"[line {line}, col {column}] Parse error: {e}");
        if (!lexer.errors.isEmpty()) throw new ParseError("markup");
        var[Parser] parser = new Parser(inner, true);
        parser.blockDepth = 1;
        var[Expr | Nil] value = nil;
        var[ParserState] saved = this._save();
        try {
            value = parser.expression();
            if (!parser._isAtEnd()) parser._error(parser._peek(), "Expect '}' here: { } in markup holds one value.");
        } catch (e[ParseError]) {
            this._restore(saved);
        }
        if (!parser.errors.isEmpty() || value == nil) {
            for (var[Str] e in parser.errors) this.errors.push(e);
            throw new ParseError("markup");
        }
        return value;
    }

    func[Expr] _parseMarkup(token[Token]) {
        var[MarkupReader] reader = new MarkupReader(this, token);
        var[MarkupNode] root = reader.element(0);
        return this._placeE(new MarkupExpr(root), token);
    }
}

// Reads the raw text of one MARKUP token (Parser.parseMarkup in C++)
class MarkupReader {
    var[Parser] parser;
    var[Token] token;
    var[Chr<>] raw;
    var[Int] at = 0;
    var[Int] line;
    var[Int] column;

    init(parser[Parser], token[Token]) {
        this.parser = parser;
        this.token = token;
        this.raw = token.lexeme.chars();
        this.line = token.line;
        this.column = token.column;
    }

    func[Emp] move(to[Int]) {
        while (this.at < to && this.at < this.raw.length()) {
            if (this.raw<this.at> == '\n') {
                this.line++;
                this.column = 1;
            } else {
                this.column += utf8Size(this.raw<this.at>);
            }
            this.at++;
        }
    }

    func[ParseError] fail(message[Str]) {
        return this.parser._error(new Token(this.token.type, this.token.lexeme, this.line, this.column), message);
    }

    func[Emp] blanks() {
        var[Int] i = this.at;
        while (i < this.raw.length() && isBlank(this.raw<i>)) i++;
        this.move(i);
    }

    func[Bul] startsWith(text[Str], from[Int]) { return findChars(this.raw, text, from) == from; }

    // Where the } of the { at `from` is, or -1
    func[Int] closingBrace(from[Int]) {
        var[Int] depth = 0;
        var[Int] n = this.raw.length();
        var[Int] i = from;
        while (i < n) {
            var[Chr] c = this.raw<i>;
            if (c == '"' || c == '\'') {
                i++;
                while (i < n && this.raw<i> != c) {
                    if (this.raw<i> == '\\') i++;
                    i++;
                }
            } else if (c == '/' && i + 1 < n && this.raw<i + 1> == '/') {
                while (i < n && this.raw<i> != '\n') i++;
            } else if (c == '/' && i + 1 < n && this.raw<i + 1> == '*') {
                i = findChars(this.raw, "*/", i + 2);
                if (i < 0) return -1;
                i++;
            } else if (c == '@') {
                // nested markup: its own braces and text mustn't count, so find its end by lexing it
                var[Int] open = i + 1;
                while (open < n && isBlank(this.raw<open>)) open++;
                if (open < n && this.raw<open> == '(') {
                    var[Lexer] nested = new Lexer(slice(this.raw, i, n), true);
                    var[Token<>] found = nested.scanTokens();
                    if (!found.isEmpty() && found<0>.type == TokenType.MARKUP) {
                        // skip to the ')' after the block
                        var[Int] end = findChars(this.raw, found<0>.lexeme, i);
                        if (end >= 0) {
                            i = end + found<0>.lexeme.length();
                            while (i < n && this.raw<i> != ')') i++;
                        }
                    }
                }
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) return i;
            }
            i++;
        }
        return -1;
    }

    func[Int] nameEnd(from[Int]) {
        var[Int] end = from;
        while (end < this.raw.length() && isMarkupNameChar(this.raw<end>)) end++;
        return end;
    }

    func[MarkupNode] element(depth[Int]) {
        if (depth > MAX_NESTING) throw this.fail("This markup is nested too deeply.");
        var[MarkupNode] node = new MarkupNode();
        node.line = this.line;
        node.column = this.column;
        this.move(this.at + 1);   // <
        var[Int] end = this.nameEnd(this.at);
        node.tag = slice(this.raw, this.at, end);
        if (node.tag == "") throw this.fail("Expect a tag name after '<'.");
        this.move(end);
        // attributes
        loop {
            this.blanks();
            if (this.at >= this.raw.length()) throw this.fail("<" + node.tag + "> never ends.");
            if (this.raw<this.at> == '>') {
                this.move(this.at + 1);
                break;
            }
            if (this.startsWith("/>", this.at)) {
                this.move(this.at + 2);
                return node;
            }
            var[MarkupAttribute] attribute = new MarkupAttribute();
            attribute.line = this.line;
            attribute.column = this.column;
            end = this.nameEnd(this.at);
            attribute.name = slice(this.raw, this.at, end);
            if (attribute.name == "") throw this.fail("Expect an attribute name in <" + node.tag + ">.");
            this.move(end);
            this.blanks();
            if (this.at >= this.raw.length() || this.raw<this.at> != '=') {
                throw this.fail("Attribute '" + attribute.name + "' needs a value: " + attribute.name + "=\"text\" or " + attribute.name + "={value}");
            }
            this.move(this.at + 1);
            this.blanks();
            if (this.at < this.raw.length() && (this.raw<this.at> == '"' || this.raw<this.at> == '\'')) {
                var[Int] close = findChars(this.raw, Str(this.raw<this.at>), this.at + 1);
                if (close < 0) throw this.fail("Attribute '" + attribute.name + "': this string never ends.");
                var[Token] text = new Token(TokenType.STRING, decodeEntities(slice(this.raw, this.at + 1, close)), this.line, this.column);
                attribute.value = this.parser._placeE(new LiteralExpr(text), text);
                attribute.isText = true;
                this.move(close + 1);
            } else if (this.at < this.raw.length() && this.raw<this.at> == '{') {
                var[Int] close = this.closingBrace(this.at);
                if (close < 0) throw this.fail("This { never ends.");
                attribute.value = this.parser._parseMarkupExpression(this.token, slice(this.raw, this.at + 1, close), this.line, this.column + 1);
                this.move(close + 1);
            } else {
                throw this.fail("Attribute '" + attribute.name + "' takes \"text\" or {value}.");
            }
            node.attributes.push(attribute);
        }
        // what is inside
        loop {
            if (this.at >= this.raw.length()) throw this.fail("<" + node.tag + "> has no closing tag.");
            if (this.raw<this.at> == '{') {
                var[Int] close = this.closingBrace(this.at);
                if (close < 0) throw this.fail("This { never ends.");
                var[Str] code = slice(this.raw, this.at + 1, close);
                if (!onlyCommentOrBlank(code)) {
                    var[MarkupNode] value = new MarkupNode();
                    value.kind = MarkupKind.VALUE;
                    value.line = this.line;
                    value.column = this.column;
                    value.value = this.parser._parseMarkupExpression(this.token, code, this.line, this.column + 1);
                    node.children.push(value);
                }
                this.move(close + 1);
            } else if (this.startsWith("<!--", this.at)) {
                var[Int] close = findChars(this.raw, "-->", this.at + 4);
                if (close < 0) throw this.fail("This comment never ends.");
                this.move(close + 3);
            } else if (this.startsWith("</", this.at)) {
                var[Int] close = findChars(this.raw, ">", this.at);
                if (close < 0) throw this.fail("This closing tag never ends.");
                var[Int] nameStop = close;
                while (nameStop > this.at + 2 && isBlank(this.raw<nameStop - 1>)) nameStop--;
                var[Str] name = slice(this.raw, this.at + 2, nameStop);
                if (name != node.tag) {
                    throw this.fail("</" + name + "> closes <" + node.tag + ">: write </" + node.tag + ">. A tag with nothing inside can end in />.");
                }
                this.move(close + 1);
                return node;
            } else if (this.raw<this.at> == '<' && this.at + 1 < this.raw.length() && isAsciiLetter(this.raw<this.at + 1>)) {
                node.children.push(this.element(depth + 1));
            } else {
                var[Int] stop = this.at + 1;
                var[Int] n = this.raw.length();
                loop {
                    if (stop >= n || this.raw<stop> == '{') break;
                    if (this.raw<stop> == '<' && stop + 1 < n) {
                        var[Chr] next = this.raw<stop + 1>;
                        if (next == '/' || next == '!' || isAsciiLetter(next)) break;
                    }
                    if (this.raw<stop> == '}') {
                        this.move(stop);
                        throw this.fail("A } on its own: write &#125; for the character.");
                    }
                    stop++;
                }
                var[MarkupNode] text = new MarkupNode();
                text.kind = MarkupKind.TEXT;
                text.line = this.line;
                text.column = this.column;
                text.text = markupText(slice(this.raw, this.at, stop));
                if (text.text != "") node.children.push(text);
                this.move(stop);
            }
        }
        return node;
    }
}

// { } or { /* a comment */ } in markup: nothing to show
func[Bul] onlyCommentOrBlank(code[Str]) {
    var[Chr<>] cs = code.chars();
    var[Int] first = 0;
    while (first < cs.length() && (cs<first> == ' ' || cs<first> == '\t' || cs<first> == '\r' || cs<first> == '\n')) first++;
    if (first == cs.length()) return true;
    if (findChars(cs, "/*", first) != first) return false;
    var[Int] close = findChars(cs, "*/", 0);
    if (close < 0) return false;
    var[Int] rest = close + 2;
    while (rest < cs.length() && (cs<rest> == ' ' || cs<rest> == '\t' || cs<rest> == '\r' || cs<rest> == '\n')) rest++;
    return rest == cs.length();
}
