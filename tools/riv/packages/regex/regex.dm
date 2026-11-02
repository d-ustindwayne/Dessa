/***
 * $("regex") -- regular expressions: find, test, replace and split text by a
 * pattern.
 *
 *     $("regex") { Regex, Match }
 *
 *     var[Regex] date = new Regex("""(\d{4})-(\d\d)-(\d\d)""");
 *     print(date.test("due 2026-11-02"));                 // true
 *     var[Match | Nil] m = date.find("due 2026-11-02");
 *     if (m != nil) print(m.text, m.group(1), m.start);  // 2026-11-02 2026 4
 *     print(date.replaceAll("2026-11-02", "$3/$2/$1"));   // 02/11/2026
 *     print(new Regex(""",\s*""").split("a, b,c"));       // <"a", "b", "c">
 *     print(new Regex("hello", "i").test("HeLLo there")); // true
 *
 * Write patterns in """triple quotes""": there a backslash is just a
 * backslash ("""\d+"""). In "normal" strings write it twice ("\\d+").
 *
 * The pattern language:
 *     .            any character but a newline (any at all with flag "s")
 *     [abc] [a-z]  one of these characters; [^abc] any other one
 *     \d \w \s     a digit, a word character (letter, digit, _), white space
 *     \D \W \S     anything else
 *     \b \B        a word boundary / not one
 *     ^ $          start / end of the text (of a line with flag "m")
 *     a* a+ a?     0 or more, 1 or more, 0 or 1 (add ? to take as few as possible: a*?)
 *     a{3} a{2,} a{2,5}   exactly 3, at least 2, 2 to 5
 *     (...)        a group: its text is m.group(1), m.group(2), ... and $1 in replace
 *     (?:...)      a group that isn't counted
 *     a|b          a or b
 *     \. \\ \( ... a character that would mean something else; \n \t \r
 *
 * Flags (the second argument): "i" ignores upper / lower case, "m" makes ^ and
 * $ work on each line, "s" lets . match newlines too. A bad pattern throws a
 * RegexError.
 *
 * Written in Dessa itself (no native part), so it works everywhere Dessa runs.
 * Matching never takes more than (pattern size x text length) steps.
 */

class RegexError(Error) {}

// One match: where it is and what its groups caught
class Match {
    var[Str] text = "";             // the matched text
    var[Int] start = 0;             // where it starts (in characters)
    var[Int] end = 0;               // where it ends (the first character after it)
    var[(Str | Nil)<>] groups = <>; // group 1, 2, ...: nil when that group didn't take part

    // group(0) is the whole match, group(1) the first (...), ...
    func[Str | Nil] group(index[Int]) {
        if (index == 0) return this.text;
        if (index < 1 || index > this.groups.length()) throw new RegexError($"there is no group {index}");
        return this.groups<index - 1>;
    }

    func[Str] toStr() {
        return $"Match(\"{this.text}\" at {this.start})";
    }
}

// ---- the program the pattern turns into

const[Int] _CHAR = 0;
const[Int] _ANY = 1;      // . (x = 1: newlines too)
const[Int] _CLASS = 2;    // x = class
const[Int] _SPLIT = 3;    // try x, then y
const[Int] _JMP = 4;
const[Int] _SAVE = 5;     // x = slot
const[Int] _MATCH = 6;
const[Int] _BOL = 7;
const[Int] _EOL = 8;
const[Int] _WORDB = 9;    // x = 1: \b, 0: \B

class _Class {
    var[Int<>] lo = <>;
    var[Int<>] hi = <>;
    var[Bul] negate = false;

    func[Emp] add(lo[Int], hi[Int]) {
        this.lo.push(lo);
        this.hi.push(hi);
    }

    func[Bul] has(code[Int]) {
        for (var[Int] i = 0; i < this.lo.length(); i++) {
            if (code >= this.lo<i> && code <= this.hi<i>) return true;
        }
        return false;
    }
}

class _Node {
    var[Int] kind = 0;          // 0 char, 1 any, 2 class, 3 concat, 4 alt, 5 repeat, 6 group, 7 bol, 8 eol, 9 wordb, 10 empty
    var[Int] value = 0;         // char code, class index, group number (-1: not counted), \b (1) / \B (0)
    var[_Node<>] kids = <>;
    var[Int] min = 0;
    var[Int] max = -1;          // -1: no limit
    var[Bul] greedy = true;
}

class _Program {
    var[Int<>] op = <>;
    var[Int<>] x = <>;
    var[Int<>] y = <>;

    func[Int] emit(op[Int], x[Int], y[Int]) {
        this.op.push(op);
        this.x.push(x);
        this.y.push(y);
        return this.op.length() - 1;
    }
}

class _Parser {
    var[Chr<>] p = <>;
    var[Int] at = 0;
    var[Int] groups = 0;
    var[_Class<>] classes = <>;
    var[Str] source = "";

    init(source[Str]) {
        this.source = source;
        this.p = source.chars();
    }

    func[Emp] fail(message[Str]) {
        throw new RegexError($"bad pattern \"{this.source}\": {message}");
    }

    func[Bul] more() { return this.at < this.p.length(); }
    func[Chr] peek() { return this.p<this.at>; }

    func[_Node] node(kind[Int], value[Int]) {
        var[_Node] n = new _Node();
        n.kind = kind;
        n.value = value;
        return n;
    }

    // a|b|c
    func[_Node] alternation() {
        var[_Node] first = this.sequence();
        if (!this.more() || this.peek() != '|') return first;
        var[_Node] alt = this.node(4, 0);
        alt.kids.push(first);
        while (this.more() && this.peek() == '|') {
            this.at++;
            alt.kids.push(this.sequence());
        }
        return alt;
    }

    func[_Node] sequence() {
        var[_Node] seq = this.node(3, 0);
        while (this.more() && this.peek() != '|' && this.peek() != ')') seq.kids.push(this.repeat());
        return seq.kids.length() == 1 ? seq.kids<0> : seq;
    }

    func[_Node] repeat() {
        var[_Node] atom = this.atom();
        while (this.more()) {
            var[Chr] c = this.peek();
            var[Int] min = -2;
            var[Int] max = -1;
            if (c == '*') { min = 0; max = -1; this.at++; }
            else if (c == '+') { min = 1; max = -1; this.at++; }
            else if (c == '?') { min = 0; max = 1; this.at++; }
            else if (c == '{' && this.countAhead()) {
                this.at++;
                min = this.number();
                max = min;
                if (this.peek() == ',') {
                    this.at++;
                    max = this.peek() == '}' ? -1 : this.number();
                }
                this.at++;   // }
                if (max != -1 && max < min) this.fail("{min,max} with max smaller than min");
            } else {
                break;
            }
            if (atom.kind >= 7 && atom.kind <= 9) this.fail("nothing to repeat");
            var[_Node] r = this.node(5, 0);
            r.kids.push(atom);
            r.min = min;
            r.max = max;
            if (this.more() && this.peek() == '?') {
                r.greedy = false;
                this.at++;
            }
            atom = r;
        }
        return atom;
    }

    // Is this { the start of {n}, {n,} or {n,m}? (else it's a plain {)
    func[Bul] countAhead() {
        var[Int] i = this.at + 1;
        var[Int] digits = 0;
        while (i < this.p.length() && this.p<i>.isDigit()) { i++; digits++; }
        if (digits == 0 || i >= this.p.length()) return false;
        if (this.p<i> == '}') return true;
        if (this.p<i> != ',') return false;
        i++;
        while (i < this.p.length() && this.p<i>.isDigit()) i++;
        return i < this.p.length() && this.p<i> == '}';
    }

    func[Int] number() {
        var[Int] n = 0;
        while (this.more() && this.peek().isDigit()) {
            n = n * 10 + this.peek().toInt();
            this.at++;
            if (n > 1000) this.fail("a count over 1000");
        }
        return n;
    }

    func[_Node] atom() {
        var[Chr] c = this.peek();
        this.at++;
        if (c == '.') return this.node(1, 0);
        if (c == '^') return this.node(7, 0);
        if (c == '$') return this.node(8, 0);
        if (c == '[') return this.node(2, this.charClass());
        if (c == '(') {
            var[Int] number = -1;
            if (this.at + 1 < this.p.length() && this.peek() == '?' && this.p<this.at + 1> == ':') {
                this.at += 2;
            } else {
                this.groups++;
                number = this.groups;
            }
            var[_Node] g = this.node(6, number);
            g.kids.push(this.alternation());
            if (!this.more() || this.peek() != ')') this.fail("a ( without its )");
            this.at++;
            return g;
        }
        if (c == ')') this.fail("a ) without its (");
        if (c == '*' || c == '+' || c == '?') this.fail($"nothing to repeat before {c}");
        if (c == '\\') return this.escape(false);
        return this.node(0, c.code());
    }

    // After a backslash. In a [class], \b is a backspace.
    func[_Node] escape(inClass[Bul]) {
        if (!this.more()) this.fail("a \\ at the end");
        var[Chr] c = this.peek();
        this.at++;
        if (c == 'd' || c == 'w' || c == 's' || c == 'D' || c == 'W' || c == 'S') {
            var[_Class] k = new _Class();
            _addShorthand(k, c.lower());
            k.negate = c.isUpper();
            this.classes.push(k);
            return this.node(2, this.classes.length() - 1);
        }
        if (!inClass && c == 'b') return this.node(9, 1);
        if (!inClass && c == 'B') return this.node(9, 0);
        if (c == 'n') return this.node(0, 10);
        if (c == 't') return this.node(0, 9);
        if (c == 'r') return this.node(0, 13);
        if (c == 'f') return this.node(0, 12);
        if (c == 'v') return this.node(0, 11);
        if (c == '0') return this.node(0, 0);
        if (inClass && c == 'b') return this.node(0, 8);
        if (c.isAlpha() || c.isDigit()) this.fail($"unknown escape \\{c}");
        return this.node(0, c.code());
    }

    // [abc], [^a-z0-9_], [\d.]: the class's index
    func[Int] charClass() {
        var[_Class] k = new _Class();
        if (this.more() && this.peek() == '^') {
            k.negate = true;
            this.at++;
        }
        var[Bul] first = true;
        while (true) {
            if (!this.more()) this.fail("a [ without its ]");
            var[Chr] c = this.peek();
            if (c == ']' && !first) break;
            first = false;
            this.at++;
            var[Int] lo = c.code();
            if (c == '\\') {
                var[_Node] e = this.escape(true);
                if (e.kind == 2) {
                    // \d inside [...]: its ranges (a negated one, \D, as itself)
                    var[_Class] inner = this.classes<e.value>;
                    if (inner.negate) {
                        _addNegated(k, inner);
                    } else {
                        for (var[Int] i = 0; i < inner.lo.length(); i++) k.add(inner.lo<i>, inner.hi<i>);
                    }
                    continue;
                }
                lo = e.value;
            }
            var[Int] hi = lo;
            if (this.at + 1 < this.p.length() && this.peek() == '-' && this.p<this.at + 1> != ']') {
                this.at++;
                var[Chr] h = this.peek();
                this.at++;
                hi = h.code();
                if (h == '\\') hi = this.escape(true).value;
                if (hi < lo) this.fail("a range going backwards in [...]");
            }
            k.add(lo, hi);
        }
        this.at++;   // ]
        this.classes.push(k);
        return this.classes.length() - 1;
    }
}

func[Emp] _addShorthand(k[_Class], letter[Chr]) {
    if (letter == 'd') {
        k.add(48, 57);
    } else if (letter == 'w') {
        k.add(48, 57);
        k.add(65, 90);
        k.add(97, 122);
        k.add(95, 95);
    } else {
        k.add(32, 32);
        k.add(9, 13);
    }
}

// Everything `inner` (a negated class) leaves out, as ranges
func[Emp] _addNegated(k[_Class], inner[_Class]) {
    var[Int] from = 0;
    var[Int<>] los = inner.lo;
    var[Int<>] his = inner.hi;
    // the ranges sorted by start
    var[Int<>] order = <>;
    for (var[Int] i = 0; i < los.length(); i++) order.push(i);
    order.sort((a[Int], b[Int]) := los<a> - los<b>);
    for (var[Int] i in order) {
        if (los<i> > from) k.add(from, los<i> - 1);
        if (his<i> + 1 > from) from = his<i> + 1;
    }
    k.add(from, 1114111);
}

func[Emp] _compile(n[_Node], prog[_Program]) {
    if (n.kind == 0) {
        prog.emit(_CHAR, n.value, 0);
    } else if (n.kind == 1) {
        prog.emit(_ANY, 0, 0);
    } else if (n.kind == 2) {
        prog.emit(_CLASS, n.value, 0);
    } else if (n.kind == 3) {
        for (var[_Node] k in n.kids) _compile(k, prog);
    } else if (n.kind == 4) {
        // SPLIT a, next; a; JMP end; next: SPLIT b, ...
        var[Int<>] jumps = <>;
        for (var[Int] i = 0; i < n.kids.length(); i++) {
            if (i + 1 < n.kids.length()) {
                var[Int] split = prog.emit(_SPLIT, prog.op.length() + 1, 0);
                _compile(n.kids<i>, prog);
                jumps.push(prog.emit(_JMP, 0, 0));
                prog.y<split> = prog.op.length();
            } else {
                _compile(n.kids<i>, prog);
            }
        }
        for (var[Int] j in jumps) prog.x<j> = prog.op.length();
    } else if (n.kind == 5) {
        var[_Node] kid = n.kids<0>;
        for (var[Int] i = 0; i < n.min; i++) _compile(kid, prog);
        if (n.max == -1) {
            // L: SPLIT body, end; body; JMP L
            var[Int] split = prog.emit(_SPLIT, 0, 0);
            var[Int] body = prog.op.length();
            _compile(kid, prog);
            prog.emit(_JMP, split, 0);
            _aim(prog, split, body, prog.op.length(), n.greedy);
        } else {
            var[Int<>] splits = <>;
            for (var[Int] i = n.min; i < n.max; i++) {
                var[Int] split = prog.emit(_SPLIT, 0, 0);
                splits.push(split);
                _compile(kid, prog);
            }
            for (var[Int] s in splits) _aim(prog, s, s + 1, prog.op.length(), n.greedy);
        }
    } else if (n.kind == 6) {
        if (n.value >= 0) prog.emit(_SAVE, n.value * 2, 0);
        _compile(n.kids<0>, prog);
        if (n.value >= 0) prog.emit(_SAVE, n.value * 2 + 1, 0);
    } else if (n.kind == 7) {
        prog.emit(_BOL, 0, 0);
    } else if (n.kind == 8) {
        prog.emit(_EOL, 0, 0);
    } else if (n.kind == 9) {
        prog.emit(_WORDB, n.value, 0);
    }
}

// A SPLIT trying `more` first when greedy, `skip` first when not
func[Emp] _aim(prog[_Program], split[Int], more[Int], skip[Int], greedy[Bul]) {
    prog.x<split> = greedy ? more : skip;
    prog.y<split> = greedy ? skip : more;
}

func[Bul] _isWord(code[Int]) {
    return (code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122) || code == 95;
}

class Regex {
    var[Str] pattern = "";
    var[Str] flags = "";
    var[Int] groupCount = 0;          // how many (...) groups it has
    var[_Program] _prog = new _Program();
    var[_Class<>] _classes = <>;
    var[Bul] _ignoreCase = false;
    var[Bul] _multiline = false;
    var[Bul] _dotAll = false;
    var[Int<>] _seen = <>;
    var[Int] _runs = 0;

    init(pattern[Str], flags[Str] = "") {
        this.pattern = pattern;
        this.flags = flags;
        for (var[Chr] f in flags) {
            if (f == 'i') this._ignoreCase = true;
            else if (f == 'm') this._multiline = true;
            else if (f == 's') this._dotAll = true;
            else throw new RegexError($"unknown flag '{f}' (use i, m or s)");
        }
        var[_Parser] parser = new _Parser(pattern);
        var[_Node] tree = parser.alternation();
        if (parser.more()) parser.fail("a ) without its (");
        this.groupCount = parser.groups;
        this._classes = parser.classes;
        // .*? in front finds the leftmost match in one pass
        var[_Program] prog = new _Program();
        prog.emit(_SPLIT, 3, 1);
        prog.emit(_ANY, 1, 0);
        prog.emit(_JMP, 0, 0);
        prog.emit(_SAVE, 0, 0);
        _compile(tree, prog);
        prog.emit(_SAVE, 1, 0);
        prog.emit(_MATCH, 0, 0);
        this._prog = prog;
    }

    func[Bul] _sameChar(a[Int], b[Int]) {
        if (a == b) return true;
        if (!this._ignoreCase) return false;
        return Chr(a).lower() == Chr(b).lower();
    }

    func[Bul] _inClass(k[_Class], code[Int]) {
        var[Bul] found = k.has(code);
        if (!found && this._ignoreCase) {
            found = k.has(Chr(code).lower().code()) || k.has(Chr(code).upper().code());
        }
        return found != k.negate;
    }

    // The slots (start, end of the match and of each group; -1: none) of the
    // leftmost match at or after `from`, or an empty list
    func[Int<>] _run(text[Int<>], from[Int], anchored[Bul]) {
        const[Int] n = text.length();
        var[_Program] prog = this._prog;
        var[Int<>] op = prog.op;
        var[Int<>] xs = prog.x;
        var[Int<>] ys = prog.y;
        const[Int] size = op.length();
        // (pc, sp) already tried in this run: kept between runs, a run's number marks it
        const[Int] total = size * (n + 1);
        if (this._seen.length() != total) {
            this._seen = <>;
            for (var[Int] i = 0; i < total; i++) this._seen.push(0);
            this._runs = 0;
        }
        this._runs++;
        const[Int] run = this._runs;
        var[Int<>] caps = <>;
        for (var[Int] i = 0; i < (this.groupCount + 1) * 2; i++) caps.push(-1);
        // the stack: kind 0 = try (pc, sp); kind 1 = put caps<a> back to b
        var[Int<>] kinds = <0>;
        var[Int<>] pcs = <anchored ? 3 : 0>;
        var[Int<>] sps = <from>;
        while (!kinds.isEmpty()) {
            var[Int] last = kinds.length() - 1;
            var[Int] kind = kinds<last>;
            var[Int] pc = pcs<last>;
            var[Int] sp = sps<last>;
            kinds.removeAt(last);
            pcs.removeAt(last);
            sps.removeAt(last);
            if (kind == 1) {
                caps<pc> = sp;
                continue;
            }
            while (true) {
                var[Int] key = pc * (n + 1) + sp;
                if (this._seen<key> == run) break;
                this._seen<key> = run;
                var[Int] o = op<pc>;
                if (o == _CHAR) {
                    if (sp < n && this._sameChar(text<sp>, xs<pc>)) { pc++; sp++; continue; }
                    break;
                } else if (o == _ANY) {
                    if (sp < n && (xs<pc> == 1 || this._dotAll || text<sp> != 10)) { pc++; sp++; continue; }
                    break;
                } else if (o == _CLASS) {
                    if (sp < n && this._inClass(this._classes<xs<pc>>, text<sp>)) { pc++; sp++; continue; }
                    break;
                } else if (o == _SPLIT) {
                    kinds.push(0);
                    pcs.push(ys<pc>);
                    sps.push(sp);
                    pc = xs<pc>;
                } else if (o == _JMP) {
                    pc = xs<pc>;
                } else if (o == _SAVE) {
                    kinds.push(1);
                    pcs.push(xs<pc>);
                    sps.push(caps<xs<pc>>);
                    caps<xs<pc>> = sp;
                    pc++;
                } else if (o == _MATCH) {
                    return caps;
                } else if (o == _BOL) {
                    if (sp == 0 || (this._multiline && text<sp - 1> == 10)) { pc++; continue; }
                    break;
                } else if (o == _EOL) {
                    if (sp == n || (this._multiline && text<sp> == 10)) { pc++; continue; }
                    break;
                } else if (o == _WORDB) {
                    var[Bul] before = sp > 0 && _isWord(text<sp - 1>);
                    var[Bul] after = sp < n && _isWord(text<sp>);
                    if ((before != after) == (xs<pc> == 1)) { pc++; continue; }
                    break;
                }
            }
        }
        return <>;
    }

    func[Match] _match(chars[Chr<>], caps[Int<>]) {
        var[Match] m = new Match();
        m.start = caps<0>;
        m.end = caps<1>;
        m.text = _slice(chars, caps<0>, caps<1>);
        for (var[Int] g = 1; g <= this.groupCount; g++) {
            if (caps<g * 2> >= 0 && caps<g * 2 + 1> >= 0) m.groups.push(_slice(chars, caps<g * 2>, caps<g * 2 + 1>));
            else m.groups.push(nil);
        }
        return m;
    }

    // Is there a match anywhere in the text?
    func[Bul] test(text[Str]) {
        return !this._run(_codes(text.chars()), 0, false).isEmpty();
    }

    // Does the whole text match?
    func[Bul] matches(text[Str]) {
        var[Regex] whole = new Regex("(?:" + this.pattern + ")$", this.flags.replaceAll("m", ""));
        return !whole._run(_codes(text.chars()), 0, true).isEmpty();
    }

    // The first match at or after `from` (a character position), or nil
    func[Match | Nil] find(text[Str], from[Int] = 0) {
        var[Chr<>] chars = text.chars();
        if (from < 0 || from > chars.length()) return nil;
        var[Int<>] caps = this._run(_codes(chars), from, false);
        if (caps.isEmpty()) return nil;
        return this._match(chars, caps);
    }

    // Every match, left to right, not overlapping
    func[Match<>] findAll(text[Str]) {
        var[Chr<>] chars = text.chars();
        var[Int<>] codes = _codes(chars);
        var[Match<>] all = <>;
        var[Int] at = 0;
        while (at <= chars.length()) {
            var[Int<>] caps = this._run(codes, at, false);
            if (caps.isEmpty()) break;
            all.push(this._match(chars, caps));
            at = caps<1> > caps<0> ? caps<1> : caps<1> + 1;   // an empty match moves on by one
        }
        return all;
    }

    // The first match replaced. In `replacement`, $0 is the match, $1 the first group, $$ a $
    func[Str] replace(text[Str], replacement[Str]) {
        return this._replace(text, replacement, 1);
    }

    // Every match replaced
    func[Str] replaceAll(text[Str], replacement[Str]) {
        return this._replace(text, replacement, -1);
    }

    func[Str] _replace(text[Str], replacement[Str], limit[Int]) {
        var[Chr<>] chars = text.chars();
        var[Match<>] found = this.findAll(text);
        var[Str] out = "";
        var[Int] at = 0;
        var[Int] done = 0;
        for (var[Match] m in found) {
            if (limit >= 0 && done >= limit) break;
            out += _slice(chars, at, m.start) + _expand(replacement, m);
            at = m.end;
            done++;
        }
        return out + _slice(chars, at, chars.length());
    }

    // The pieces between the matches
    func[Str<>] split(text[Str]) {
        var[Chr<>] chars = text.chars();
        var[Str<>] pieces = <>;
        var[Int] at = 0;
        for (var[Match] m in this.findAll(text)) {
            if (m.end == m.start && (m.start == 0 || m.start == chars.length())) continue;   // empty match at an end
            pieces.push(_slice(chars, at, m.start));
            at = m.end;
        }
        pieces.push(_slice(chars, at, chars.length()));
        return pieces;
    }

    func[Str] toStr() {
        return $"Regex(\"{this.pattern}\")";
    }
}

// The text with every character that means something in a pattern escaped:
// new Regex(escape("1+1=2")) finds "1+1=2" itself
func[Str] escape(text[Str]) {
    var[Str] out = "";
    for (var[Chr] c in text) {
        if ("\\.^$|?*+()[]{}".includes(Str(c))) out += "\\";
        out += Str(c);
    }
    return out;
}

func[Int<>] _codes(chars[Chr<>]) {
    var[Int<>] codes = <>;
    for (var[Chr] c in chars) codes.push(c.code());
    return codes;
}

func[Str] _slice(chars[Chr<>], from[Int], to[Int]) {
    var[Str] out = "";
    for (var[Int] i = from; i < to; i++) out += Str(chars<i>);
    return out;
}

// $1, $0, $$ in a replacement
func[Str] _expand(replacement[Str], m[Match]) {
    var[Chr<>] w = replacement.chars();
    var[Str] out = "";
    var[Int] i = 0;
    while (i < w.length()) {
        var[Chr] c = w<i>;
        if (c == '$' && i + 1 < w.length()) {
            var[Chr] next = w<i + 1>;
            if (next == '$') {
                out += "$";
                i += 2;
                continue;
            }
            if (next.isDigit()) {
                var[Int] g = next.toInt();
                i += 2;
                // two digits when there are that many groups: $12
                if (i < w.length() && w<i>.isDigit() && g * 10 + w<i>.toInt() <= m.groups.length()) {
                    g = g * 10 + w<i>.toInt();
                    i++;
                }
                if (g <= m.groups.length()) {
                    var[Str | Nil] part = m.group(g);
                    if (part != nil) out += part;
                }
                continue;
            }
        }
        out += Str(c);
        i++;
    }
    return out;
}
