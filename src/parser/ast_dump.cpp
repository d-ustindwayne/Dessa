#include "ast_dump.hpp"

#include <sstream>

namespace dessa {

namespace {

// "text" with what can't be seen escaped: \\ \" \n \r \t, other control characters as \xNN
std::string quote(const std::string& text) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20 || c == 0x7f) {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

std::string typeText(const TypeNode& t);

std::string typeList(const std::vector<TypeNode>& types) {
    std::string out;
    for (size_t i = 0; i < types.size(); ++i) out += (i ? ", " : "") + typeText(types[i]);
    return out;
}

// :Int or :[Str, Int]
std::string typeArgsText(const std::vector<TypeNode>& types) {
    if (types.empty()) return "";
    return types.size() == 1 ? ":" + typeText(types[0]) : ":[" + typeList(types) + "]";
}

// Close to how it is written. A union member that is a union itself is in
// { } (A | B | C is A with the member B | C: A | {B | C}), a grouped type in ( ).
std::string typeText(const TypeNode& t) {
    std::string out = t.group.empty() ? t.name : "(" + typeText(t.group[0]) + ")";
    if (t.isFunction) out += "Func[" + typeList(t.funcReturn) + ", [" + typeList(t.funcParams) + "]]";
    if (t.isMap) out += "Map[" + typeList(t.mapTypes) + "]";
    if (t.radix) out += "[" + std::to_string(*t.radix) + "]";
    out += typeArgsText(t.typeArgs);
    if (t.isList) out += "<>";
    if (t.isSet) out += "{}";
    for (const auto& member : t.unionMembers) {
        out += " | " + (member.unionMembers.empty() ? typeText(member) : "{" + typeText(member) + "}");
    }
    return out;
}

class Dumper {
public:
    std::ostringstream out;

    void line(int depth, const std::string& text) {
        out << std::string(static_cast<size_t>(depth) * 2, ' ') << text << '\n';
    }

    static std::string at(int line, int column) {
        return std::to_string(line) + ":" + std::to_string(column) + " ";
    }

    void param(const Param& p, int d) {
        line(d, at(p.line, p.column) + "param " + p.name + "[" + typeText(p.type) + "]" + (p.isVariadic ? " ..." : ""));
        if (p.defaultValue) expr(p.defaultValue.get(), d + 1);
    }

    void args(const std::vector<Argument>& list, int d, const std::vector<std::optional<TypeNode>>* fieldTypes = nullptr) {
        for (size_t i = 0; i < list.size(); ++i) {
            const Argument& a = list[i];
            std::string text = "arg";
            if (a.name) text += " name=" + *a.name;
            if (a.isSpread) text += " spread";
            if (a.oldNamed) text += " old";
            if (fieldTypes && i < fieldTypes->size() && (*fieldTypes)[i]) text += " type=" + typeText(*(*fieldTypes)[i]);
            line(d, text);
            expr(a.value.get(), d + 1);
        }
    }

    void elements(const std::vector<CollectionElement>& list, int d) {
        for (const auto& e : list) {
            if (e.isSpread) {
                line(d, "spread");
                expr(e.value.get(), d + 1);
            } else {
                expr(e.value.get(), d);
            }
        }
    }

    void markup(const MarkupNode& node, int d) {
        const std::string where = at(node.line, node.column);
        switch (node.kind) {
            case MarkupNode::Kind::Text:
                line(d, where + "Text " + quote(node.text));
                return;
            case MarkupNode::Kind::Value:
                line(d, where + "Value");
                expr(node.value.get(), d + 1);
                return;
            case MarkupNode::Kind::Element:
                break;
        }
        line(d, where + "Element " + node.tag);
        for (const auto& a : node.attributes) {
            line(d + 1, at(a.line, a.column) + "attr " + a.name + (a.isText ? " text" : ""));
            if (a.value) expr(a.value.get(), d + 2);
        }
        for (const auto& child : node.children) markup(child, d + 1);
    }

    void expr(const Expr* e, int d) {
        if (!e) {
            line(d, "<none>");
            return;
        }
        const std::string where = at(e->line, e->column);
        switch (e->kind) {
            case ExprKind::Literal: {
                const auto* x = static_cast<const LiteralExpr*>(e);
                line(d, where + "Literal " + std::to_string(static_cast<int>(x->token.type)) + " " + quote(x->token.lexeme));
                return;
            }
            case ExprKind::TemplateString: {
                const auto* x = static_cast<const TemplateStringExpr*>(e);
                line(d, where + "Template " + quote(x->token.lexeme));
                for (size_t i = 0; i < x->literalParts.size() || i < x->interpolations.size(); ++i) {
                    if (i < x->literalParts.size()) line(d + 1, "part " + quote(x->literalParts[i]));
                    if (i < x->interpolations.size()) expr(x->interpolations[i].get(), d + 1);
                }
                return;
            }
            case ExprKind::Identifier: {
                const auto* x = static_cast<const IdentifierExpr*>(e);
                line(d, where + "Identifier " + x->name.lexeme + typeArgsText(x->typeArgs));
                return;
            }
            case ExprKind::Unary: {
                const auto* x = static_cast<const UnaryExpr*>(e);
                line(d, where + "Unary " + x->op.lexeme);
                expr(x->operand.get(), d + 1);
                return;
            }
            case ExprKind::Postfix: {
                const auto* x = static_cast<const PostfixExpr*>(e);
                line(d, where + "Postfix " + x->op.lexeme);
                expr(x->operand.get(), d + 1);
                return;
            }
            case ExprKind::Binary: {
                const auto* x = static_cast<const BinaryExpr*>(e);
                line(d, where + "Binary " + x->op.lexeme);
                expr(x->left.get(), d + 1);
                expr(x->right.get(), d + 1);
                return;
            }
            case ExprKind::Assign: {
                const auto* x = static_cast<const AssignExpr*>(e);
                line(d, where + "Assign " + x->op.lexeme);
                expr(x->target.get(), d + 1);
                expr(x->value.get(), d + 1);
                return;
            }
            case ExprKind::Ternary: {
                const auto* x = static_cast<const TernaryExpr*>(e);
                line(d, where + "Ternary");
                expr(x->condition.get(), d + 1);
                expr(x->thenBranch.get(), d + 1);
                expr(x->elseBranch.get(), d + 1);
                return;
            }
            case ExprKind::Call: {
                const auto* x = static_cast<const CallExpr*>(e);
                line(d, where + "Call");
                expr(x->callee.get(), d + 1);
                args(x->args, d + 1);
                return;
            }
            case ExprKind::Index: {
                const auto* x = static_cast<const IndexExpr*>(e);
                line(d, where + "Index");
                expr(x->object.get(), d + 1);
                expr(x->index.get(), d + 1);
                return;
            }
            case ExprKind::Member: {
                const auto* x = static_cast<const MemberExpr*>(e);
                line(d, where + "Member " + (x->isOptional ? "?." : "") + x->name.lexeme + typeArgsText(x->typeArgs));
                expr(x->object.get(), d + 1);
                return;
            }
            case ExprKind::Lambda: {
                const auto* x = static_cast<const LambdaExpr*>(e);
                line(d, where + "Lambda");
                for (const auto& p : x->params) param(p, d + 1);
                stmt(x->body.get(), d + 1);
                return;
            }
            case ExprKind::Markup: {
                const auto* x = static_cast<const MarkupExpr*>(e);
                line(d, where + "Markup");
                markup(x->root, d + 1);
                return;
            }
            case ExprKind::Await: {
                const auto* x = static_cast<const AwaitExpr*>(e);
                line(d, where + "Await");
                expr(x->operand.get(), d + 1);
                return;
            }
            case ExprKind::New: {
                const auto* x = static_cast<const NewExpr*>(e);
                line(d, where + "New " + x->className.lexeme + typeArgsText(x->typeArgs) + (x->isAnonymous ? " anonymous" : ""));
                args(x->args, d + 1, &x->fieldTypes);
                return;
            }
            case ExprKind::List: {
                const auto* x = static_cast<const ListExpr*>(e);
                line(d, where + "List" + (x->oldBrackets ? " old" : ""));
                elements(x->elements, d + 1);
                return;
            }
            case ExprKind::Set: {
                const auto* x = static_cast<const SetExpr*>(e);
                line(d, where + "Set");
                elements(x->elements, d + 1);
                return;
            }
            case ExprKind::Map: {
                const auto* x = static_cast<const MapExpr*>(e);
                line(d, where + "Map");
                for (const auto& entry : x->entries) {
                    if (!entry.key) {
                        line(d + 1, "spread");
                        expr(entry.value.get(), d + 2);
                    } else {
                        line(d + 1, "entry");
                        expr(entry.key.get(), d + 2);
                        expr(entry.value.get(), d + 2);
                    }
                }
                return;
            }
            case ExprKind::Convert: {
                const auto* x = static_cast<const ConvertExpr*>(e);
                line(d, where + "Convert " + x->type.lexeme);
                expr(x->value.get(), d + 1);
                return;
            }
            case ExprKind::Is: {
                const auto* x = static_cast<const IsExpr*>(e);
                line(d, where + "Is " + typeText(x->type));
                expr(x->value.get(), d + 1);
                return;
            }
            case ExprKind::Grouping: {
                const auto* x = static_cast<const GroupingExpr*>(e);
                line(d, where + "Grouping");
                expr(x->inner.get(), d + 1);
                return;
            }
        }
        line(d, where + "?");
    }

    void labelled(const std::string& label, const Stmt* s, int d) {
        line(d, label);
        stmt(s, d + 1);
    }

    void stmt(const Stmt* s, int d) {
        if (!s) {
            line(d, "<none>");
            return;
        }
        const std::string where = at(s->line, s->column);
        switch (s->kind) {
            case StmtKind::Expression:
                line(d, where + "Expression");
                expr(static_cast<const ExpressionStmt*>(s)->expression.get(), d + 1);
                return;
            case StmtKind::VarDecl: {
                const auto* x = static_cast<const VarDeclStmt*>(s);
                line(d, where + (x->isConst ? "Const[" : "Var[") + typeText(x->type) + "] " + (x->name ? *x->name : "<none>"));
                if (x->initializer) expr(x->initializer.get(), d + 1);
                return;
            }
            case StmtKind::FuncDecl: {
                const auto* x = static_cast<const FuncDeclStmt*>(s);
                std::string text = where + "Func[" + typeText(x->returnType) + "] " + x->name;
                if (!x->typeParams.empty()) {
                    text += " :";
                    for (size_t i = 0; i < x->typeParams.size(); ++i) text += (i ? "," : "") + x->typeParams[i];
                }
                if (x->isConstructor) text += " init";
                if (x->isComp) text += " comp";
                if (x->isAsync) text += " async[" + typeText(x->asyncResult) + "]";
                for (const auto& a : x->annotations) text += " @" + a.lexeme;
                if (!x->throwsTypes.empty()) text += " !! " + typeList(x->throwsTypes);
                if (!x->templateTokens.empty()) text += " tokens=" + std::to_string(x->templateTokens.size());
                for (const auto& [name, type] : x->templateTypes) text += " " + name + "=" + typeText(type);
                line(d, text);
                for (const auto& p : x->params) param(p, d + 1);
                if (x->body) stmt(x->body.get(), d + 1);
                return;
            }
            case StmtKind::ClassDecl: {
                const auto* x = static_cast<const ClassDeclStmt*>(s);
                std::string text = where + "Class " + x->name;
                if (!x->typeParams.empty()) {
                    text += " :";
                    for (size_t i = 0; i < x->typeParams.size(); ++i) text += (i ? "," : "") + x->typeParams[i];
                }
                if (x->interfaceName) text += " interface=" + *x->interfaceName;
                if (x->parentClassName) text += " parent=" + *x->parentClassName;
                if (x->parentType) text += " parentType=" + typeText(*x->parentType);
                if (!x->templateTokens.empty()) text += " tokens=" + std::to_string(x->templateTokens.size());
                line(d, text);
                for (const auto& m : x->members) stmt(m.get(), d + 1);
                for (const auto& m : x->methodTemplates) labelled("template", m.get(), d + 1);
                return;
            }
            case StmtKind::EnumDecl: {
                const auto* x = static_cast<const EnumDeclStmt*>(s);
                line(d, where + "Enum " + x->name);
                for (const auto& v : x->values) {
                    line(d + 1, v.name);
                    if (v.value) expr(v.value->get(), d + 2);
                }
                return;
            }
            case StmtKind::InterfaceDecl: {
                const auto* x = static_cast<const InterfaceDeclStmt*>(s);
                line(d, where + "Interface " + x->name);
                for (const auto& m : x->members) stmt(m.get(), d + 1);
                return;
            }
            case StmtKind::Import: {
                const auto* x = static_cast<const ImportStmt*>(s);
                line(d, where + "Import" + (x->importAll ? " all" : ""));
                for (const auto& m : x->modules) {
                    std::string text = at(m.line, m.column) + "module " + quote(m.path);
                    if (m.alias) text += " as " + *m.alias;
                    if (m.isBuiltin) text += " builtin";
                    if (m.isPrelude) text += " prelude";
                    if (m.isPageLibrary) text += " page";
                    if (m.isHidden) text += " hidden";
                    line(d + 1, text);
                }
                for (const auto& n : x->names) line(d + 1, at(n.line, n.column) + "name " + n.name + (n.alias ? " as " + *n.alias : ""));
                return;
            }
            case StmtKind::If: {
                const auto* x = static_cast<const IfStmt*>(s);
                line(d, where + "If");
                expr(x->condition.get(), d + 1);
                stmt(x->thenBranch.get(), d + 1);
                if (x->elseBranch) labelled("else", x->elseBranch.get(), d + 1);
                return;
            }
            case StmtKind::Switch: {
                const auto* x = static_cast<const SwitchStmt*>(s);
                line(d, where + "Switch");
                expr(x->discriminant.get(), d + 1);
                for (const auto& c : x->cases) {
                    if (c.value) {
                        line(d + 1, "case");
                        expr(c.value.get(), d + 2);
                    } else {
                        line(d + 1, "default");
                    }
                    stmt(c.body.get(), d + 2);
                }
                return;
            }
            case StmtKind::For: {
                const auto* x = static_cast<const ForStmt*>(s);
                line(d, where + "For");
                if (x->init) labelled("init", x->init.get(), d + 1);
                if (x->condition) {
                    line(d + 1, "condition");
                    expr(x->condition.get(), d + 2);
                }
                if (x->increment) {
                    line(d + 1, "step");
                    expr(x->increment.get(), d + 2);
                }
                stmt(x->body.get(), d + 1);
                return;
            }
            case StmtKind::ForIn: {
                const auto* x = static_cast<const ForInStmt*>(s);
                line(d, where + "ForIn");
                stmt(x->declaration.get(), d + 1);
                expr(x->iterable.get(), d + 1);
                stmt(x->body.get(), d + 1);
                return;
            }
            case StmtKind::While: {
                const auto* x = static_cast<const WhileStmt*>(s);
                line(d, where + "While");
                expr(x->condition.get(), d + 1);
                stmt(x->body.get(), d + 1);
                return;
            }
            case StmtKind::Loop:
                line(d, where + "Loop");
                stmt(static_cast<const LoopStmt*>(s)->body.get(), d + 1);
                return;
            case StmtKind::Break:
                line(d, where + "Break");
                return;
            case StmtKind::Continue:
                line(d, where + "Continue");
                return;
            case StmtKind::Return: {
                const auto* x = static_cast<const ReturnStmt*>(s);
                line(d, where + "Return" + (x->isMarkup ? " markup" : ""));
                if (x->value) expr(x->value.get(), d + 1);
                return;
            }
            case StmtKind::Block: {
                const auto* x = static_cast<const BlockStmt*>(s);
                line(d, where + "Block");
                for (const auto& inner : x->statements) stmt(inner.get(), d + 1);
                return;
            }
            case StmtKind::Try: {
                const auto* x = static_cast<const TryStmt*>(s);
                line(d, where + "Try");
                stmt(x->tryBlock.get(), d + 1);
                for (const auto& c : x->catches) {
                    std::string text = at(c.line, c.column) + "catch";
                    if (c.param) text += " " + *c.param;
                    if (c.type) text += "[" + typeText(*c.type) + "]";
                    line(d + 1, text);
                    stmt(c.body.get(), d + 2);
                }
                return;
            }
            case StmtKind::Throw:
                line(d, where + "Throw");
                expr(static_cast<const ThrowStmt*>(s)->value.get(), d + 1);
                return;
        }
        line(d, where + "?");
    }
};

} // namespace

std::string dumpAst(const std::vector<StmtPtr>& program, const std::vector<StmtPtr>& templates) {
    Dumper dumper;
    for (const auto& s : program) dumper.stmt(s.get(), 0);
    if (!templates.empty()) {
        dumper.line(0, "templates");
        for (const auto& s : templates) dumper.stmt(s.get(), 1);
    }
    return dumper.out.str();
}

} // namespace dessa
