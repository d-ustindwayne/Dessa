/***
 * The tree as text, one node per line: a twin of src/parser/ast_dump.cpp.
 * `dessa ast file.ds` and `selfhost/main.ds ast file.ds` print the same.
 */

$("lexer") { Token }
$("ast")

const[Chr<>] HEX = "0123456789abcdef".chars();

// "text" with what can't be seen escaped: \\ \" \n \r \t, other control characters as \xNN
func[Str] quote(text[Str]) {
    var[Str<>] out = <"\"">;
    for (var[Chr] c in text) {
        var[Int] code = c.code();
        if (c == '\\') out.push("\\\\");
        else if (c == '"') out.push("\\\"");
        else if (c == '\n') out.push("\\n");
        else if (c == '\r') out.push("\\r");
        else if (c == '\t') out.push("\\t");
        else if (code < 32 || code == 127) {
            var[Int] high = code >> 4;
            var[Int] low = code & 15;
            out.push("\\x" + Str(HEX<high>) + Str(HEX<low>));
        }
        else out.push(Str(c));
    }
    out.push("\"");
    return out.join("");
}

func[Str] typeList(types[TypeNode<>]) {
    var[Str<>] parts = <>;
    for (var[TypeNode] t in types) parts.push(typeText(t));
    return parts.join(", ");
}

// :Int or :[Str, Int]
func[Str] typeArgsText(types[TypeNode<>]) {
    if (types.isEmpty()) return "";
    return types.length() == 1 ? ":" + typeText(types<0>) : ":[" + typeList(types) + "]";
}

// A union member that is a union itself is in { }, a grouped type in ( )
func[Str] typeText(t[TypeNode]) {
    var[Str] out = t.group.isEmpty() ? t.name : "(" + typeText(t.group<0>) + ")";
    if (t.isFunction) out += "Func[" + typeList(t.funcReturn) + ", [" + typeList(t.funcParams) + "]]";
    if (t.isMap) out += "Map[" + typeList(t.mapTypes) + "]";
    if (t.radix >= 0) out += "[" + Str(t.radix) + "]";
    out += typeArgsText(t.typeArgs);
    if (t.isList) out += "<>";
    if (t.isSet) out += "{}";
    for (var[TypeNode] member in t.unionMembers) {
        out += " | " + (member.unionMembers.isEmpty() ? typeText(member) : "{" + typeText(member) + "}");
    }
    return out;
}

func[Str] at(line[Int], column[Int]) { return $"{line}:{column} "; }

class Dumper {
    var[Str<>] lines = <>;

    func[Emp] line(depth[Int], text[Str]) {
        this.lines.push(" ".repeat(depth * 2) + text);
    }

    func[Emp] param(p[Param], d[Int]) {
        this.line(d, at(p.line, p.column) + "param " + p.name + "[" + typeText(p.type) + "]" + (p.isVariadic ? " ..." : ""));
        var[Expr | Nil] value = p.defaultValue;
        if (value != nil) this.expr(value, d + 1);
    }

    func[Emp] args(list[Argument<>], d[Int], fieldTypes[(TypeNode | Nil)<>]) {
        var[Int] i = 0;
        for (var[Argument] a in list) {
            var[Str] text = "arg";
            var[Str | Nil] name = a.name;
            if (name != nil) text += " name=" + name;
            if (a.isSpread) text += " spread";
            if (a.oldNamed) text += " old";
            if (i < fieldTypes.length()) {
                var[TypeNode | Nil] type = fieldTypes<i>;
                if (type != nil) text += " type=" + typeText(type);
            }
            this.line(d, text);
            this.expr(a.value, d + 1);
            i++;
        }
    }

    func[Emp] elements(list[CollectionElement<>], d[Int]) {
        for (var[CollectionElement] e in list) {
            if (e.isSpread) {
                this.line(d, "spread");
                this.expr(e.value, d + 1);
            } else {
                this.expr(e.value, d);
            }
        }
    }

    func[Emp] markup(node[MarkupNode], d[Int]) {
        var[Str] where = at(node.line, node.column);
        if (node.kind == MarkupKind.TEXT) {
            this.line(d, where + "Text " + quote(node.text));
            return;
        }
        if (node.kind == MarkupKind.VALUE) {
            this.line(d, where + "Value");
            this.expr(node.value, d + 1);
            return;
        }
        this.line(d, where + "Element " + node.tag);
        for (var[MarkupAttribute] a in node.attributes) {
            this.line(d + 1, at(a.line, a.column) + "attr " + a.name + (a.isText ? " text" : ""));
            var[Expr | Nil] value = a.value;
            if (value != nil) this.expr(value, d + 2);
        }
        for (var[MarkupNode] child in node.children) this.markup(child, d + 1);
    }

    func[Emp] expr(e[Expr | Nil], d[Int]) {
        if (e == nil) {
            this.line(d, "<none>");
            return;
        }
        var[Str] where = at(e.line, e.column);
        if (e is LiteralExpr) {
            this.line(d, where + "Literal " + Str(e.token.type.value()) + " " + quote(e.token.lexeme));
        } else if (e is TemplateStringExpr) {
            this.line(d, where + "Template " + quote(e.token.lexeme));
            var[Int] i = 0;
            while (i < e.literalParts.length() || i < e.interpolations.length()) {
                if (i < e.literalParts.length()) this.line(d + 1, "part " + quote(e.literalParts<i>));
                if (i < e.interpolations.length()) this.expr(e.interpolations<i>, d + 1);
                i++;
            }
        } else if (e is IdentifierExpr) {
            this.line(d, where + "Identifier " + e.name.lexeme + typeArgsText(e.typeArgs));
        } else if (e is UnaryExpr) {
            this.line(d, where + "Unary " + e.op.lexeme);
            this.expr(e.operand, d + 1);
        } else if (e is PostfixExpr) {
            this.line(d, where + "Postfix " + e.op.lexeme);
            this.expr(e.operand, d + 1);
        } else if (e is BinaryExpr) {
            this.line(d, where + "Binary " + e.op.lexeme);
            this.expr(e.left, d + 1);
            this.expr(e.right, d + 1);
        } else if (e is AssignExpr) {
            this.line(d, where + "Assign " + e.op.lexeme);
            this.expr(e.target, d + 1);
            this.expr(e.value, d + 1);
        } else if (e is TernaryExpr) {
            this.line(d, where + "Ternary");
            this.expr(e.condition, d + 1);
            this.expr(e.thenBranch, d + 1);
            this.expr(e.elseBranch, d + 1);
        } else if (e is CallExpr) {
            this.line(d, where + "Call");
            this.expr(e.callee, d + 1);
            this.args(e.args, d + 1, <>);
        } else if (e is IndexExpr) {
            this.line(d, where + "Index");
            this.expr(e.object, d + 1);
            this.expr(e.index, d + 1);
        } else if (e is MemberExpr) {
            this.line(d, where + "Member " + (e.isOptional ? "?." : "") + e.name.lexeme + typeArgsText(e.typeArgs));
            this.expr(e.object, d + 1);
        } else if (e is LambdaExpr) {
            this.line(d, where + "Lambda");
            for (var[Param] p in e.params) this.param(p, d + 1);
            this.stmt(e.body, d + 1);
        } else if (e is MarkupExpr) {
            this.line(d, where + "Markup");
            this.markup(e.root, d + 1);
        } else if (e is AwaitExpr) {
            this.line(d, where + "Await");
            this.expr(e.operand, d + 1);
        } else if (e is NewExpr) {
            this.line(d, where + "New " + e.className.lexeme + typeArgsText(e.typeArgs) + (e.isAnonymous ? " anonymous" : ""));
            this.args(e.args, d + 1, e.fieldTypes);
        } else if (e is ListExpr) {
            this.line(d, where + "List" + (e.oldBrackets ? " old" : ""));
            this.elements(e.elements, d + 1);
        } else if (e is SetExpr) {
            this.line(d, where + "Set");
            this.elements(e.elements, d + 1);
        } else if (e is MapExpr) {
            this.line(d, where + "Map");
            for (var[MapEntry] entry in e.entries) {
                var[Expr | Nil] key = entry.key;
                if (key == nil) {
                    this.line(d + 1, "spread");
                    this.expr(entry.value, d + 2);
                } else {
                    this.line(d + 1, "entry");
                    this.expr(key, d + 2);
                    this.expr(entry.value, d + 2);
                }
            }
        } else if (e is ConvertExpr) {
            this.line(d, where + "Convert " + e.type.lexeme);
            this.expr(e.value, d + 1);
        } else if (e is IsExpr) {
            this.line(d, where + "Is " + typeText(e.type));
            this.expr(e.value, d + 1);
        } else if (e is GroupingExpr) {
            this.line(d, where + "Grouping");
            this.expr(e.inner, d + 1);
        } else {
            this.line(d, where + "?");
        }
    }

    func[Emp] labelled(label[Str], s[Stmt | Nil], d[Int]) {
        this.line(d, label);
        this.stmt(s, d + 1);
    }

    func[Str] typeParamsText(params[Str<>]) {
        return params.isEmpty() ? "" : " :" + params.join(",");
    }

    func[Emp] stmt(s[Stmt | Nil], d[Int]) {
        if (s == nil) {
            this.line(d, "<none>");
            return;
        }
        var[Str] where = at(s.line, s.column);
        if (s is ExpressionStmt) {
            this.line(d, where + "Expression");
            this.expr(s.expression, d + 1);
        } else if (s is VarDeclStmt) {
            var[Str | Nil] name = s.name;
            this.line(d, where + (s.isConst ? "Const[" : "Var[") + typeText(s.type) + "] " + (name ?? "<none>"));
            var[Expr | Nil] value = s.initializer;
            if (value != nil) this.expr(value, d + 1);
        } else if (s is FuncDeclStmt) {
            var[Str] text = where + "Func[" + typeText(s.returnType) + "] " + s.name + this.typeParamsText(s.typeParams);
            if (s.isConstructor) text += " init";
            if (s.isComp) text += " comp";
            if (s.isAsync) text += " async[" + typeText(s.asyncResult) + "]";
            for (var[Token] a in s.annotations) text += " @" + a.lexeme;
            if (!s.throwsTypes.isEmpty()) text += " !! " + typeList(s.throwsTypes);
            if (!s.templateTokens.isEmpty()) text += " tokens=" + Str(s.templateTokens.length());
            this.line(d, text);
            for (var[Param] p in s.params) this.param(p, d + 1);
            var[Stmt | Nil] body = s.body;
            if (body != nil) this.stmt(body, d + 1);
        } else if (s is ClassDeclStmt) {
            var[Str] text = where + "Class " + s.name + this.typeParamsText(s.typeParams);
            var[Str | Nil] interfaceName = s.interfaceName;
            if (interfaceName != nil) text += " interface=" + interfaceName;
            var[Str | Nil] parent = s.parentClassName;
            if (parent != nil) text += " parent=" + parent;
            var[TypeNode | Nil] parentType = s.parentType;
            if (parentType != nil) text += " parentType=" + typeText(parentType);
            if (!s.templateTokens.isEmpty()) text += " tokens=" + Str(s.templateTokens.length());
            this.line(d, text);
            for (var[Stmt] m in s.members) this.stmt(m, d + 1);
            for (var[Stmt] m in s.methodTemplates) this.labelled("template", m, d + 1);
        } else if (s is EnumDeclStmt) {
            this.line(d, where + "Enum " + s.name);
            for (var[EnumEntry] v in s.values) {
                this.line(d + 1, v.name);
                var[Expr | Nil] value = v.value;
                if (value != nil) this.expr(value, d + 2);
            }
        } else if (s is InterfaceDeclStmt) {
            this.line(d, where + "Interface " + s.name);
            for (var[Stmt] m in s.members) this.stmt(m, d + 1);
        } else if (s is ImportStmt) {
            this.line(d, where + "Import" + (s.importAll ? " all" : ""));
            for (var[ModuleSpec] m in s.modules) {
                var[Str] text = at(m.line, m.column) + "module " + quote(m.path);
                var[Str | Nil] alias = m.alias;
                if (alias != nil) text += " as " + alias;
                if (m.isBuiltin) text += " builtin";
                if (m.isPrelude) text += " prelude";
                if (m.isPageLibrary) text += " page";
                if (m.isHidden) text += " hidden";
                this.line(d + 1, text);
            }
            for (var[ImportName] n in s.names) {
                var[Str | Nil] alias = n.alias;
                this.line(d + 1, at(n.line, n.column) + "name " + n.name + (alias != nil ? " as " + alias : ""));
            }
        } else if (s is IfStmt) {
            this.line(d, where + "If");
            this.expr(s.condition, d + 1);
            this.stmt(s.thenBranch, d + 1);
            var[Stmt | Nil] otherwise = s.elseBranch;
            if (otherwise != nil) this.labelled("else", otherwise, d + 1);
        } else if (s is SwitchStmt) {
            this.line(d, where + "Switch");
            this.expr(s.discriminant, d + 1);
            for (var[SwitchCase] c in s.cases) {
                var[Expr | Nil] value = c.value;
                if (value != nil) {
                    this.line(d + 1, "case");
                    this.expr(value, d + 2);
                } else {
                    this.line(d + 1, "default");
                }
                this.stmt(c.body, d + 2);
            }
        } else if (s is ForStmt) {
            this.line(d, where + "For");
            var[Stmt | Nil] start = s.init_;
            if (start != nil) this.labelled("init", start, d + 1);
            var[Expr | Nil] condition = s.condition;
            if (condition != nil) {
                this.line(d + 1, "condition");
                this.expr(condition, d + 2);
            }
            var[Expr | Nil] step = s.increment;
            if (step != nil) {
                this.line(d + 1, "step");
                this.expr(step, d + 2);
            }
            this.stmt(s.body, d + 1);
        } else if (s is ForInStmt) {
            this.line(d, where + "ForIn");
            this.stmt(s.declaration, d + 1);
            this.expr(s.iterable, d + 1);
            this.stmt(s.body, d + 1);
        } else if (s is WhileStmt) {
            this.line(d, where + "While");
            this.expr(s.condition, d + 1);
            this.stmt(s.body, d + 1);
        } else if (s is LoopStmt) {
            this.line(d, where + "Loop");
            this.stmt(s.body, d + 1);
        } else if (s is BreakStmt) {
            this.line(d, where + "Break");
        } else if (s is ContinueStmt) {
            this.line(d, where + "Continue");
        } else if (s is ReturnStmt) {
            this.line(d, where + "Return" + (s.isMarkup ? " markup" : ""));
            var[Expr | Nil] value = s.value;
            if (value != nil) this.expr(value, d + 1);
        } else if (s is BlockStmt) {
            this.line(d, where + "Block");
            for (var[Stmt] inner in s.statements) this.stmt(inner, d + 1);
        } else if (s is TryStmt) {
            this.line(d, where + "Try");
            this.stmt(s.tryBlock, d + 1);
            for (var[CatchClause] c in s.catches) {
                var[Str] text = at(c.line, c.column) + "catch";
                var[Str | Nil] param = c.param;
                if (param != nil) text += " " + param;
                var[TypeNode | Nil] type = c.type;
                if (type != nil) text += "[" + typeText(type) + "]";
                this.line(d + 1, text);
                this.stmt(c.body, d + 2);
            }
        } else if (s is ThrowStmt) {
            this.line(d, where + "Throw");
            this.expr(s.value, d + 1);
        } else {
            this.line(d, where + "?");
        }
    }
}

// The program, then the generic declarations the parser kept aside
func[Str<>] dumpAst(program[Stmt<>], templates[Stmt<>]) {
    var[Dumper] dumper = new Dumper();
    for (var[Stmt] s in program) dumper.stmt(s, 0);
    if (!templates.isEmpty()) {
        dumper.line(0, "templates");
        for (var[Stmt] s in templates) dumper.stmt(s, 1);
    }
    return dumper.lines;
}
