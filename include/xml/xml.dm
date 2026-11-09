/***
 * xml -- read and write XML.
 *
 *     $("xml") { Xml, parseXml }
 *     var[Xml] doc = parseXml("<catalog><book id=\"1\">Dune</book></catalog>");
 *     print(doc.find("book")?.attr("id"));      1
 *     print(doc.find("book")?.text());          Dune
 *     print(doc);                               the XML again
 *
 * In an .xds file, the same value can be written as markup:
 *
 *     var[Xml] data = @(<catalog><book id={id}>{title}</book></catalog>);
 *
 * Values are escaped when written out, so text can't change the document's shape.
 * A DOCTYPE is skipped: entities it declares are never expanded, and nothing is
 * ever fetched. So a hostile file can't read files or blow up memory that way.
 */

class XmlError(Error) {}

// An element: its name, attributes, and what's inside (elements and text)
class Xml {
    var[Str] name;
    var[Map[Str, Str]] attributes = {:};
    var[(Xml | Str)<>] nodes = <>;

    init(name[Str]) { this.name = name; }

    // The attribute's value, nil if there is none
    func[Str | Nil] attr(name[Str]) {
        return this.attributes.has(name) ? this.attributes<name> : nil;
    }

    func[Xml] setAttr(name[Str], value[Str]) {
        this.attributes<name> = value;
        return this;
    }

    // Adds an element or text at the end
    func[Xml] append(node[Xml | Str]) {
        this.nodes.push(node);
        return this;
    }

    // The elements directly inside
    func[Xml<>] children() {
        var[Xml<>] out = <>;
        for (var[Xml | Str] node in this.nodes) if (node is Xml) out.push(node);
        return out;
    }

    // The first element with this name, at any depth
    func[Xml | Nil] find(name[Str]) {
        for (var[Xml] child in this.children()) {
            if (child.name == name) return child;
            var[Xml | Nil] inner = child.find(name);
            if (inner != nil) return inner;
        }
        return nil;
    }

    // All elements with this name, at any depth
    func[Xml<>] findAll(name[Str]) {
        var[Xml<>] out = <>;
        for (var[Xml] child in this.children()) {
            if (child.name == name) out.push(child);
            for (var[Xml] inner in child.findAll(name)) out.push(inner);
        }
        return out;
    }

    // All the text inside, at any depth
    func[Str] text() {
        var[Str] out = "";
        for (var[Xml | Str] node in this.nodes) {
            if (node is Xml) out += node.text();
            else if (node is Str) out += node;
        }
        return out;
    }

    func[Str] _open() {
        var[Str] out = "<" + this.name;
        for (var[Str] key in this.attributes) out += $" {key}=\"{_escape(this.attributes<key>, true)}\"";
        return out;
    }

    // On one line, nothing added: parseXml gives back the same document
    func[Str] toStr() {
        if (this.nodes.length() == 0) return this._open() + " />";
        var[Str] out = this._open() + ">";
        for (var[Xml | Str] node in this.nodes) {
            if (node is Xml) out += node.toStr();
            else if (node is Str) out += _escape(node, false);
        }
        return out + "</" + this.name + ">";
    }

    // Indented, for people. An element holding text stays on one line.
    func[Str] pretty(indent[Str] = "  ") {
        return this._pretty(indent, "");
    }

    func[Str] _pretty(indent[Str], at[Str]) {
        if (this.children().length() == 0) return at + this.toStr();
        var[Str] out = at + this._open() + ">\n";
        for (var[Xml | Str] node in this.nodes) {
            if (node is Xml) out += node._pretty(indent, at + indent) + "\n";
            else if (node is Str) out += at + indent + _escape(node.trim(), false) + "\n";
        }
        return out + at + "</" + this.name + ">";
    }
}

func[Str] _escape(text[Str], inAttribute[Bul]) {
    var[Str] out = text.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
    if (inAttribute) out = out.replaceAll("\"", "&quot;");
    return out;
}

// ---- Markup in an .xds file turns into these (src/analyser/analyser_markup.cpp) ----

class _XmlMaker {
    var[Xml] made;
    init(name[Str]) { this.made = new Xml(name); }
    func[_XmlMaker] attr(name[Str], value[Obj]) {
        if (value != nil) this.made.setAttr(name, Str(value));
        return this;
    }
    func[_XmlMaker] add(value[Obj]) {
        if (value == nil) return this;
        if (value is Xml) this.made.append(value);
        else if (value is Xml<>) { for (var[Xml] one in value) this.made.append(one); }
        else if (value is Str<>) { for (var[Str] one in value) this.made.append(one); }
        else this.made.append(Str(value));
        return this;
    }
    func[Xml] done() { return this.made; }
}

func[_XmlMaker] _el(name[Str]) { return new _XmlMaker(name); }

// ---- Reading ----

const[Int] _MAX_DEPTH = 256;

class _XmlReader {
    var[Str] text;
    var[Chr<>] c;
    var[Int] at = 0;

    init(text[Str]) {
        this.text = text;
        this.c = text.chars();
    }

    func[Bul] done() { return this.at >= this.c.length(); }

    // Line and column of where the reader is
    func[Emp] fail(message[Str]) {
        var[Int] line = 1, column = 1;
        for (var[Int] i = 0; i < this.at && i < this.c.length(); i++) {
            if (this.c<i> == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }
        throw new XmlError($"line {line}, column {column}: {message}");
    }

    func[Bul] looking(word[Str]) {
        var[Int] i = this.at;
        for (var[Chr] w in word) {
            if (i >= this.c.length() || this.c<i> != w) return false;
            i++;
        }
        return true;
    }

    func[Emp] skipBlanks() {
        while (!this.done() && this.c<this.at>.isSpace()) this.at++;
    }

    // Past the next `word`, whatever comes before it
    func[Emp] skipPast(word[Str], what[Str]) {
        while (!this.done() && !this.looking(word)) this.at++;
        if (this.done()) this.fail($"{what} never ends");
        this.at += word.length();
    }

    func[Bul] isNameChar(ch[Chr]) {
        return ch.isAlpha() || ch.isDigit() || ch == '_' || ch == '-' || ch == ':' || ch == '.' || ch.code() >= 128;
    }

    func[Str] name() {
        var[Str] out = "";
        while (!this.done() && this.isNameChar(this.c<this.at>)) {
            out += this.c<this.at>;
            this.at++;
        }
        if (out == "") this.fail("expected a name");
        return out;
    }

    // &amp; &#65; &#x41;. The reader is on the &.
    func[Str] entity() {
        var[Str] body = "";
        this.at++;
        while (!this.done() && this.c<this.at> != ';') {
            if (body.length() > 12) this.fail("an & needs to be written &amp;");
            body += this.c<this.at>;
            this.at++;
        }
        if (this.done()) this.fail("an & needs to be written &amp;");
        this.at++;
        switch (body) {
            case "lt" := { return "<"; }
            case "gt" := { return ">"; }
            case "amp" := { return "&"; }
            case "quot" := { return "\""; }
            case "apos" := { return "'"; }
            default := { }
        }
        if (!body.startsWith("#")) this.fail($"unknown entity &{body};");
        var[Int] code = 0;
        var[Str] digits = body.substr(1, body.length() - 1);
        var[Bul] hex = digits.startsWith("x");
        if (hex) digits = digits.substr(1, digits.length() - 1);
        if (digits == "") this.fail("a character code needs digits");
        for (var[Chr] d in digits.lower()) {
            var[Int] value = d.isDigit() ? d.toInt() : (hex && d >= 'a' && d <= 'f' ? d.code() - 87 : -1);
            if (value < 0) this.fail($"bad character code &{body};");
            code = code * (hex ? 16 : 10) + value;
            if (code > 1114111) this.fail($"bad character code &{body};");
        }
        return Str(Chr(code));
    }

    func[Str] attributeValue() {
        if (this.done() || (this.c<this.at> != '"' && this.c<this.at> != '\'')) this.fail("an attribute's value goes in quotes");
        var[Chr] quote = this.c<this.at>;
        this.at++;
        var[Str] out = "";
        while (!this.done() && this.c<this.at> != quote) {
            if (this.c<this.at> == '<') this.fail("a < in an attribute needs to be written &lt;");
            if (this.c<this.at> == '&') {
                out += this.entity();
            } else {
                out += this.c<this.at>;
                this.at++;
            }
        }
        if (this.done()) this.fail("an attribute's value never ends");
        this.at++;
        return out;
    }

    // The reader is on the < of an element
    func[Xml] element(depth[Int]) {
        if (depth > _MAX_DEPTH) this.fail($"elements are nested more than {_MAX_DEPTH} deep");
        this.at++;
        var[Xml] node = new Xml(this.name());
        loop {
            this.skipBlanks();
            if (this.done()) this.fail($"<{node.name}> never ends");
            if (this.looking("/>")) {
                this.at += 2;
                return node;
            }
            if (this.c<this.at> == '>') {
                this.at++;
                break;
            }
            var[Str] key = this.name();
            this.skipBlanks();
            if (this.done() || this.c<this.at> != '=') this.fail($"attribute {key} needs a value");
            this.at++;
            this.skipBlanks();
            if (node.attributes.has(key)) this.fail($"attribute {key} is given twice");
            node.attributes<key> = this.attributeValue();
        }
        var[Str] pending = "";
        loop {
            if (this.done()) this.fail($"<{node.name}> has no closing tag");
            if (this.looking("<![CDATA[")) {
                this.at += 9;
                while (!this.done() && !this.looking("]]>")) {
                    pending += this.c<this.at>;
                    this.at++;
                }
                if (this.done()) this.fail("CDATA never ends");
                this.at += 3;
            } else if (this.looking("<!--")) {
                this.skipPast("-->", "a comment");
            } else if (this.looking("<?")) {
                this.skipPast("?>", "an instruction");
            } else if (this.looking("</")) {
                this.at += 2;
                var[Str] closing = this.name();
                if (closing != node.name) this.fail($"</{closing}> closes <{node.name}>");
                this.skipBlanks();
                if (this.done() || this.c<this.at> != '>') this.fail("a closing tag ends with >");
                this.at++;
                if (pending.trim() != "") node.nodes.push(pending);
                return node;
            } else if (this.c<this.at> == '<') {
                if (pending.trim() != "") node.nodes.push(pending);
                pending = "";
                node.nodes.push(this.element(depth + 1));
            } else if (this.c<this.at> == '&') {
                pending += this.entity();
            } else {
                pending += this.c<this.at>;
                this.at++;
            }
        }
        return node;
    }

    func[Xml] document() {
        loop {
            this.skipBlanks();
            if (this.looking("<?")) this.skipPast("?>", "an instruction");
            else if (this.looking("<!--")) this.skipPast("-->", "a comment");
            else if (this.looking("<!DOCTYPE")) this.skipDoctype();
            else break;
        }
        if (this.done() || this.c<this.at> != '<') this.fail("expected an element");
        var[Xml] root = this.element(0);
        loop {
            this.skipBlanks();
            if (this.looking("<?")) this.skipPast("?>", "an instruction");
            else if (this.looking("<!--")) this.skipPast("-->", "a comment");
            else break;
        }
        if (!this.done()) this.fail("only one element can be at the top");
        return root;
    }

    // Read past, never understood: <!DOCTYPE a [ <!ENTITY ...> ]>
    func[Emp] skipDoctype() {
        var[Int] brackets = 0;
        while (!this.done()) {
            var[Chr] ch = this.c<this.at>;
            if (ch == '[') brackets++;
            else if (ch == ']') brackets--;
            else if (ch == '>' && brackets <= 0) {
                this.at++;
                return;
            }
            this.at++;
        }
        this.fail("a DOCTYPE never ends");
    }
}

// Text to an Xml value. A XmlError says where it went wrong.
func[Xml] parseXml(text[Str]) {
    return new _XmlReader(text).document();
}
