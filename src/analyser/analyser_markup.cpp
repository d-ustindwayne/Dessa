// Markup of an .xds file: @(<div class={kind}>Hello {name}</div>)
//
// There is nothing for the generators to learn: the analyser rewrites markup
// into ordinary calls (Expr::lowered) and analyses those.
//
//   a page element      _xds._mk("div").attr("class", kind).add("Hello ").add(name).done()
//   <Card title="x" />  Card(title = "x")
//   nested in a comp    <Box>hi</Box> is Box(child = _xds._kids("hi"))
//   an Xml value        _xdsxml._el("item").attr("id", n).add(x).done()
//
// _xds and _xdsxml are imports the loader adds to the file (see loader.cpp).
#include "analyser.hpp"

#include <cctype>

namespace dessa {

namespace {

ExprPtr identifier(const std::string& name, int line, int column) {
    auto e = std::make_unique<IdentifierExpr>(Token(TokenType::IDENTIFIER, name, line, column));
    e->line = line;
    e->column = column;
    return e;
}

ExprPtr member(ExprPtr object, const std::string& name, int line, int column) {
    auto e = std::make_unique<MemberExpr>(std::move(object), Token(TokenType::IDENTIFIER, name, line, column), false);
    e->line = line;
    e->column = column;
    return e;
}

ExprPtr text(const std::string& value, int line, int column) {
    auto e = std::make_unique<LiteralExpr>(Token(TokenType::STRING, value, line, column));
    e->line = line;
    e->column = column;
    return e;
}

ExprPtr call(ExprPtr callee, std::vector<Argument> arguments, int line, int column) {
    auto e = std::make_unique<CallExpr>(std::move(callee), std::move(arguments), Token(TokenType::R_PAREN, ")", line, column));
    e->line = line;
    e->column = column;
    return e;
}

Argument argument(ExprPtr value) {
    Argument a;
    a.value = std::move(value);
    return a;
}

ExprPtr method(ExprPtr receiver, const std::string& name, std::vector<ExprPtr> values, int line, int column) {
    std::vector<Argument> arguments;
    for (auto& v : values) arguments.push_back(argument(std::move(v)));
    return call(member(std::move(receiver), name, 0, 0), std::move(arguments), line, column);
}

template <typename... Values>
std::vector<ExprPtr> list(Values&&... values) {
    std::vector<ExprPtr> out;
    (out.push_back(std::move(values)), ...);
    return out;
}

bool isComponentTag(const std::string& tag) {
    return std::isupper(static_cast<unsigned char>(tag[0])) || tag.find('.') != std::string::npos;
}

} // namespace

// Children: text, an element, or several of them
SemanticType Analyser::childrenType(int moduleId) const {
    const SemanticType element = SemanticType::userDefined("Element", moduleId, UserTypeKind::Class);
    const SemanticType str = SemanticType::simple(SemanticTypeKind::Str);
    return SemanticType::unionOf({element, str, SemanticType::listOf(SemanticType::unionOf({element, str}))});
}

// comp's return type: Element when it always ends in markup, Element | Nil when
// it can reach its end (where it then gives nil)
void Analyser::prepareComp(const FuncDeclStmt* declaration) {
    if (declaration->returnType.name != "Emp") return;
    auto* fn = const_cast<FuncDeclStmt*>(declaration);
    fn->returnType.name = "_xds.Element";
    auto* block = static_cast<BlockStmt*>(fn->body.get());
    if (!blockDefinitelyExits(block, false)) {
        TypeNode nil;
        nil.name = "Nil";
        fn->returnType.unionMembers.push_back(std::move(nil));
        auto value = std::make_unique<LiteralExpr>(Token(TokenType::NIL, "nil", declaration->line, declaration->column));
        value->line = declaration->line;
        value->column = declaration->column;
        auto end = std::make_unique<ReturnStmt>(std::move(value));
        end->line = declaration->line;
        end->column = declaration->column;
        block->statements.push_back(std::move(end));
    }
}

// The value that goes inside a component's Children parameter
ExprPtr Analyser::lowerNestedContent(MarkupNode& node) {
    std::vector<Argument> values;
    for (auto& child : node.children) {
        switch (child.kind) {
            case MarkupNode::Kind::Text: values.push_back(argument(text(child.text, child.line, child.column))); break;
            case MarkupNode::Kind::Value: values.push_back(argument(std::move(child.value))); break;
            case MarkupNode::Kind::Element: {
                ExprPtr element = lowerMarkupElement(child);
                if (!element) return nullptr;
                values.push_back(argument(std::move(element)));
                break;
            }
        }
    }
    return call(member(identifier("_xds", 0, 0), "_kids", 0, 0), std::move(values), node.line, node.column);
}

// <Card title="x">...</Card>: a call of the function or lambda named Card
ExprPtr Analyser::lowerComponent(MarkupNode& node) {
    const Symbol* symbol = node.tag.find('.') != std::string::npos ? resolveQualified(node.tag, node.line, node.column) : resolveSymbol(node.tag);
    if (!symbol) {
        if (node.tag.find('.') == std::string::npos) {
            error(node.line, node.column, "There is no component <" + node.tag + ">. Declare it with comp, or import it. (Page tags are lowercase.)");
        }
        return nullptr;
    }
    // Its parameters, by name
    std::vector<std::pair<std::string, SemanticType>> parameters;
    const bool isLambda = symbol->lambda != nullptr;
    if (isLambda) {
        for (const auto& p : symbol->lambda->params) parameters.emplace_back(p.name, resolveType(p.type, node.line, node.column));
    } else if (symbol->kind == SymbolKind::Function) {
        if (symbol->templateId >= 0) {
            error(node.line, node.column, "<" + node.tag + "> is generic, and a component can't be. Call it inside { } instead.");
            return nullptr;
        }
        for (const auto& p : symbol->parameters) parameters.emplace_back(p.name, p.type);
    } else {
        error(node.line, node.column, "<" + node.tag + "> isn't a component: " + node.tag + " is not a comp or a function.");
        return nullptr;
    }

    std::vector<ExprPtr> byPosition(parameters.size());
    std::vector<Argument> byName;
    const auto give = [&](const std::string& name, ExprPtr value, int line, int column) {
        if (!isLambda) {
            Argument a;
            a.name = name;
            a.value = std::move(value);
            byName.push_back(std::move(a));
            return;
        }
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (parameters[i].first != name) continue;
            if (byPosition[i]) error(line, column, "<" + node.tag + "> is given '" + name + "' more than once");
            byPosition[i] = std::move(value);
            return;
        }
        error(line, column, "<" + node.tag + "> has no parameter named '" + name + "'");
    };

    for (auto& attribute : node.attributes) {
        if (attribute.name.rfind("on:", 0) == 0) {
            error(attribute.line, attribute.column, "on: goes on page tags like <button>. <" + node.tag + "> takes a handler as a parameter: onClick={f}");
            continue;
        }
        give(attribute.name, std::move(attribute.value), attribute.line, attribute.column);
    }

    if (!node.children.empty()) {
        const std::string childrenText = childrenType(0).toString();
        std::string target;
        for (const auto& [name, type] : parameters) {
            if (type.toString() == childrenText) {
                target = name;
                break;
            }
        }
        if (target.empty()) {
            error(node.line, node.column, "<" + node.tag + "> takes nothing nested inside it: none of its parameters is of type Children");
            return nullptr;
        }
        ExprPtr content = lowerNestedContent(node);
        if (!content) return nullptr;
        give(target, std::move(content), node.line, node.column);
    }

    std::vector<Argument> arguments;
    if (isLambda) {
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (!byPosition[i]) {
                error(node.line, node.column, "<" + node.tag + "> needs '" + parameters[i].first + "'");
                return nullptr;
            }
            arguments.push_back(argument(std::move(byPosition[i])));
        }
    } else {
        arguments = std::move(byName);
    }

    ExprPtr callee;
    const size_t dot = node.tag.find('.');
    if (dot == std::string::npos) {
        callee = identifier(node.tag, node.line, node.column + 1);
    } else {
        callee = identifier(node.tag.substr(0, dot), node.line, node.column);
        size_t from = dot + 1;
        for (;;) {
            const size_t next = node.tag.find('.', from);
            callee = member(std::move(callee), node.tag.substr(from, next == std::string::npos ? std::string::npos : next - from), node.line, node.column);
            if (next == std::string::npos) break;
            from = next + 1;
        }
    }
    return call(std::move(callee), std::move(arguments), node.line, node.column);
}

ExprPtr Analyser::lowerMarkupElement(MarkupNode& node) {
    if (isComponentTag(node.tag)) return lowerComponent(node);
    const int line = node.line, column = node.column;
    ExprPtr chain = call(member(identifier("_xds", 0, 0), "_mk", 0, 0), {}, line, column);
    static_cast<CallExpr*>(chain.get())->args.push_back(argument(text(node.tag, line, column)));

    for (auto& attribute : node.attributes) {
        const int aLine = attribute.line, aColumn = attribute.column;
        std::string lowered = attribute.name;
        for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (attribute.name.rfind("on:", 0) == 0) {
            const std::string type = attribute.name.substr(3);
            if (attribute.isText || type.empty()) {
                error(aLine, aColumn, "on:" + type + " takes a function: on:" + (type.empty() ? "click" : type) + "={f}");
                continue;
            }
            // A handler with or without the event. What it is decides which call it becomes.
            bool takesEvent = false;
            const Expr* value = attribute.value.get();
            if (value->kind == ExprKind::Lambda) {
                takesEvent = !static_cast<const LambdaExpr*>(value)->params.empty();
            } else {
                // Looked at now and again when lowered: its errors only count once
                const size_t before = diagnostics.size();
                const SemanticType handler = withoutNil(analyseExpression(value));
                diagnostics.resize(before);
                if (handler.kind == SemanticTypeKind::Function) {
                    takesEvent = !handler.parameterTypes.empty();
                } else if (!handler.isUnknown()) {
                    error(aLine, aColumn, "on:" + type + " takes a function, not " + handler.toString());
                    continue;
                }
            }
            chain = method(std::move(chain), takesEvent ? "on1" : "on0", list(text(type, aLine, aColumn), std::move(attribute.value)), aLine, aColumn);
            continue;
        }
        if (lowered.rfind("on", 0) == 0 && lowered.size() > 2 && lowered.find('-') == std::string::npos) {
            error(aLine, aColumn, "'" + attribute.name + "' is a page script: use on:" + lowered.substr(2) + "={f}");
            continue;
        }
        chain = method(std::move(chain), "attr", list(text(attribute.name, aLine, aColumn), std::move(attribute.value)), aLine, aColumn);
    }

    for (auto& child : node.children) {
        ExprPtr value;
        switch (child.kind) {
            case MarkupNode::Kind::Text: value = text(child.text, child.line, child.column); break;
            case MarkupNode::Kind::Value: value = std::move(child.value); break;
            case MarkupNode::Kind::Element: value = lowerMarkupElement(child); break;
        }
        if (!value) return nullptr;
        chain = method(std::move(chain), "add", list(std::move(value)), child.line, child.column);
    }
    return method(std::move(chain), "done", {}, line, column);
}

// XML is data: tags are its own names, and {expr} is escaped when it's written out
ExprPtr Analyser::lowerXmlElement(MarkupNode& node) {
    const int line = node.line, column = node.column;
    ExprPtr chain = call(member(identifier("_xdsxml", 0, 0), "_el", 0, 0), {}, line, column);
    static_cast<CallExpr*>(chain.get())->args.push_back(argument(text(node.tag, line, column)));
    for (auto& attribute : node.attributes) {
        if (attribute.name.rfind("on:", 0) == 0) {
            error(attribute.line, attribute.column, "XML has no events: on: is for page markup");
            continue;
        }
        chain = method(std::move(chain), "attr", list(text(attribute.name, attribute.line, attribute.column), std::move(attribute.value)), attribute.line, attribute.column);
    }
    for (auto& child : node.children) {
        ExprPtr value;
        switch (child.kind) {
            case MarkupNode::Kind::Text: value = text(child.text, child.line, child.column); break;
            case MarkupNode::Kind::Value: value = std::move(child.value); break;
            case MarkupNode::Kind::Element: value = lowerXmlElement(child); break;
        }
        if (!value) return nullptr;
        chain = method(std::move(chain), "add", list(std::move(value)), child.line, child.column);
    }
    return method(std::move(chain), "done", {}, line, column);
}

SemanticType Analyser::analyseMarkup(const MarkupExpr* expression) {
    auto& root = const_cast<MarkupExpr*>(expression)->root;
    if (root.tag.empty()) return SemanticType::unknown();   // the parser has said what's wrong with it
    bool xml = false;
    if (auto hint = typeHints.find(expression); hint != typeHints.end()) {
        const SemanticType target = withoutNil(hint->second);
        xml = target.isClass() && target.name == "Xml";
    }
    if (xml && !resolveSymbol("_xdsxml")) {
        error(expression, "Markup for an Xml value needs the xml library: $(\"xml\") { Xml }");
        return SemanticType::unknown();
    }
    if (!xml && !resolveSymbol("_xds")) {
        error(expression, "This markup makes page elements, which needs a comp in the file. For data, give it a type: var[Xml] data = @(...)");
        return SemanticType::unknown();
    }
    ExprPtr lowered = xml ? lowerXmlElement(root) : lowerMarkupElement(root);
    if (!lowered) return SemanticType::unknown();
    expression->lowered = std::move(lowered);
    return analyseExpression(expression->lowered.get());
}

} // namespace dessa
