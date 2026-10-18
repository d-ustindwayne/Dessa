// src/analyser/analyser_expr.cpp
// Expression analysis: operators, calls, members, lambdas, collections.
#include "analyser.hpp"
#include "builtins.hpp"

#include <algorithm>
#include <unordered_set>
#include <string>

namespace dessa {

namespace {

SemanticType simple(SemanticTypeKind k) { return SemanticType::simple(k); }

bool isNilLiteral(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::NIL;
}

const Expr* unwrapGrouping(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e;
}

// list<-1>: a literal negative index, rejected at compile time.
bool isNegativeLiteral(const Expr* e) {
    e = unwrapGrouping(e);
    if (!e || e->kind != ExprKind::Unary) return false;
    const auto* u = static_cast<const UnaryExpr*>(e);
    const Expr* operand = unwrapGrouping(u->operand.get());
    return u->op.type == TokenType::MINUS && operand && operand->kind == ExprKind::Literal &&
           static_cast<const LiteralExpr*>(operand)->token.lexeme != "0";
}

std::string calleeName(const Expr* callee) {
    if (!callee) return "<expression>";
    if (callee->kind == ExprKind::Identifier) return static_cast<const IdentifierExpr*>(callee)->name.lexeme;
    if (callee->kind == ExprKind::Member) return static_cast<const MemberExpr*>(callee)->name.lexeme;
    return "<expression>";
}

} // namespace

// ============================================================
// Dispatch
// ============================================================

SemanticType Analyser::analyseExpression(const Expr* expression, bool allowTypeName) {
    if (!expression) return SemanticType::unknown();
    SemanticType type = analyseExpressionInner(expression, allowTypeName);
    exprTypes[expression] = type;
    return type;
}

SemanticType Analyser::analyseExpressionInner(const Expr* expression, bool allowTypeName) {
    switch (expression->kind) {
        case ExprKind::Literal: return analyseLiteral(static_cast<const LiteralExpr*>(expression));
        case ExprKind::TemplateString: return analyseTemplateString(static_cast<const TemplateStringExpr*>(expression));
        case ExprKind::Identifier: return analyseIdentifier(static_cast<const IdentifierExpr*>(expression), allowTypeName);
        case ExprKind::Unary: return analyseUnary(static_cast<const UnaryExpr*>(expression));
        case ExprKind::Postfix: return analysePostfix(static_cast<const PostfixExpr*>(expression));
        case ExprKind::Binary: return analyseBinary(static_cast<const BinaryExpr*>(expression));
        case ExprKind::Assign: return analyseAssignment(static_cast<const AssignExpr*>(expression));
        case ExprKind::Ternary: return analyseTernary(static_cast<const TernaryExpr*>(expression));
        case ExprKind::Call: return analyseCall(static_cast<const CallExpr*>(expression));
        case ExprKind::Index: return analyseIndex(static_cast<const IndexExpr*>(expression));
        case ExprKind::Member: return analyseMember(static_cast<const MemberExpr*>(expression));
        case ExprKind::Lambda: return analyseLambda(static_cast<const LambdaExpr*>(expression));
        case ExprKind::New: return analyseNew(static_cast<const NewExpr*>(expression));
        case ExprKind::List: {
            const auto* list = static_cast<const ListExpr*>(expression);
            return analyseCollection(list->elements, false, expression);
        }
        case ExprKind::Set: {
            const auto* set = static_cast<const SetExpr*>(expression);
            return analyseCollection(set->elements, true, expression);
        }
        case ExprKind::Grouping:
            return analyseExpression(static_cast<const GroupingExpr*>(expression)->inner.get());
        case ExprKind::Is:
            return analyseIs(static_cast<const IsExpr*>(expression));
        case ExprKind::Map:
            return analyseMapLiteral(static_cast<const MapExpr*>(expression));
        case ExprKind::Convert:
            return analyseConvert(static_cast<const ConvertExpr*>(expression));
    }
    return SemanticType::unknown();
}

// ============================================================
// Leaves
// ============================================================

SemanticType Analyser::analyseLiteral(const LiteralExpr* expression) {
    switch (expression->token.type) {
        case TokenType::STRING: return simple(SemanticTypeKind::Str);
        case TokenType::INT_LITERAL:
            try {
                (void)std::stoll(expression->token.lexeme);
            } catch (const std::exception&) {
                error(expression, "Integer literal " + expression->token.lexeme +
                      " is too large for Int; for bigger numbers use new Big(\"" + expression->token.lexeme + "\") from $(\"math\")");
            }
            return simple(SemanticTypeKind::Int);
        case TokenType::BINARY_LITERAL:
        case TokenType::OCTAL_LITERAL:
        case TokenType::HEX_LITERAL:
            return simple(SemanticTypeKind::Int);
        case TokenType::FLOAT_LITERAL: return simple(SemanticTypeKind::Flt);
        case TokenType::DOUBLE_LITERAL: return simple(SemanticTypeKind::Dbl);
        case TokenType::CHAR_LITERAL: return simple(SemanticTypeKind::Chr);
        case TokenType::TRUE:
        case TokenType::FALSE: return simple(SemanticTypeKind::Bul);
        case TokenType::NIL: return simple(SemanticTypeKind::Nil);
        case TokenType::ELLIPSIS: return SemanticType::unknown(); // stub placeholder
        default:
            error(expression, "Invalid literal '" + expression->token.lexeme + "'");
            return SemanticType::unknown();
    }
}

// Integer arithmetic that only became a Dbl through an Int / Int division,
// e.g. 192 + code / 64. Converting it to an Int or Chr truncates it.
bool Analyser::isWholeNumberMath(const Expr* e) const {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    if (!e) return false;
    const SemanticType* t = typeOf(e);
    if (t && t->isInteger()) return true;
    if (isIntDivision(e)) return true;
    if (e->kind == ExprKind::Binary) {
        const auto* b = static_cast<const BinaryExpr*>(e);
        switch (b->op.type) {
            case TokenType::PLUS: case TokenType::MINUS: case TokenType::MULT: case TokenType::MOD:
                return isWholeNumberMath(b->left.get()) && isWholeNumberMath(b->right.get());
            default:
                return false;
        }
    }
    return false;
}

// Int("42"), Str(42), Bul(1), Dbl("2.5"), Flt(x), Chr(65)
SemanticType Analyser::analyseConvert(const ConvertExpr* expression) {
    SemanticType value = analyseExpression(expression->value.get());
    SemanticType target = resolveTypeName(expression->type.lexeme, expression->line, expression->column);
    if (value.isUnknown()) return target;
    const std::string name = expression->type.lexeme;

    if (value.isVoidLike()) {
        error(expression, "Expression has no value (Emp) and cannot be converted to " + name);
        return target;
    }
    if (value.containsNil()) {
        error(expression, "Value of type " + value.toString() + " may be nil; check it with != nil or use ?? before " + name + "(...)");
        return target;
    }
    if (value.is(SemanticTypeKind::Obj)) return target;   // checked when the program runs

    using K = SemanticTypeKind;
    const bool isStr = value.is(K::Str), isBul = value.is(K::Bul), isChr = value.is(K::Chr);
    bool ok = false;
    switch (target.kind) {
        case K::Str: ok = true; break;                                   // anything can be shown as text
        case K::Int: ok = value.isNumeric() || isStr || isBul || isChr || value.isEnum(); break;
        case K::Flt:
        case K::Dbl: ok = value.isNumeric() || isStr || isBul; break;
        case K::Bul: ok = value.isNumeric() || isStr || isBul; break;
        case K::Chr: ok = value.isInteger() || isStr || isChr || isWholeNumberMath(expression->value.get()); break;
        default: break;
    }
    if (!ok) {
        error(expression, "Cannot convert " + value.toString() + " to " + name);
    }
    return target;
}

// value is Type
SemanticType Analyser::analyseIs(const IsExpr* expression) {
    SemanticType value = analyseExpression(expression->value.get());
    SemanticType target = resolveType(expression->type, expression->line, expression->column);
    isTargets[expression] = target;
    const SemanticType boolean = simple(SemanticTypeKind::Bul);
    if (value.isUnknown() || target.isUnknown()) return boolean;

    if (target.kind == SemanticTypeKind::Union) {
        error(expression, "'is' checks one type at a time, e.g. x is Int || x is Str");
        return boolean;
    }
    if (target.is(SemanticTypeKind::Nil)) {
        error(expression, "Use x == nil to check for nil");
        return boolean;
    }

    // Judge by the declared type: `nobody = new Cat(); nobody is Dog` is a
    // fair question even though `nobody` currently holds a Cat.
    if (const Symbol* s = expression->value->kind == ExprKind::Identifier ? symbolOf(expression->value.get()) : nullptr) {
        if (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter || s->kind == SymbolKind::Constant) value = s->type;
    }
    if (value.is(SemanticTypeKind::Obj)) return boolean;   // an Obj may hold anything
    const SemanticType base = withoutNil(value);
    std::vector<SemanticType> members = base.kind == SemanticTypeKind::Union ? base.unionMembers : std::vector<SemanticType>{base};

    // Could any member of the value's type hold a `target`?
    bool possible = false;
    bool always = !value.containsNil();
    for (const auto& m : members) {
        bool memberPossible = false;
        if (target.isReference() && m.isReference()) {
            // subclass check, or an interface that some class might implement
            memberPossible = isAssignable(m, target) || isAssignable(target, m) || m.isInterface() || target.isInterface();
        } else {
            memberPossible = m == target;
        }
        possible = possible || memberPossible;
        if (!isAssignable(target, m)) always = false;
    }

    if (!possible) {
        error(expression, "A value of type " + value.toString() + " can never be " + target.toString());
    } else if (always && members.size() == 1) {
        warning(expression, "This is always true: " + value.toString() + " is always " + target.toString());
    }
    return boolean;
}

SemanticType Analyser::analyseTemplateString(const TemplateStringExpr* expression) {
    for (const auto& interpolation : expression->interpolations) {
        SemanticType t = analyseExpression(interpolation.get());
        if (t.isVoidLike()) error(interpolation.get(), "Cannot interpolate an expression with no value (Emp)");
    }
    return simple(SemanticTypeKind::Str);
}

SemanticType Analyser::analyseIdentifier(const IdentifierExpr* expression, bool allowTypeName) {
    const std::string& name = expression->name.lexeme;

    if (expression->name.type == TokenType::THIS) {
        const ClassInfo* klass = currentClass();
        if (!klass) {
            error(expression, "'this' can only be used inside a class");
            return SemanticType::unknown();
        }
        return klass->type();
    }

    if (expression->name.type == TokenType::SUPER) {
        const ClassInfo* klass = currentClass();
        if (!klass || !klass->parent) {
            error(expression, "'super' can only be used in a class that has a parent class");
            return SemanticType::unknown();
        }
        if (!allowTypeName) {
            error(expression, "'super' can only be used to call a parent method, e.g. super(...) or super.method()");
        }
        return *klass->parent;
    }

    // Editor completion: everything visible here, innermost scope first
    if (!probeName.empty() && name == probeName) {
        if (!completion.found) {
            completion.found = true;
            std::unordered_set<std::string> seen;
            for (const Scope* s = currentScope; s; s = s->parent()) {
                for (const auto& [n, sym] : s->all()) {
                    if (!seen.insert(n).second) continue;
                    Symbol visible = sym;
                    if (auto narrowed = currentScope->narrowed(n)) visible.type = *narrowed;
                    completion.visible.push_back(visible);
                }
            }
        }
        return SemanticType::unknown();
    }

    const Symbol* symbol = resolveSymbol(name);
    if (!symbol) {
        // Exported later in this module but not declared yet?
        const auto& exports = modules[currentModule].exports;
        auto it = exports.find(name);
        if (it != exports.end() && (it->second.kind == SymbolKind::Variable || it->second.kind == SymbolKind::Constant) &&
            functionDepth == 0) {
            error(expression, "'" + name + "' is used before its declaration on line " + std::to_string(it->second.line));
        } else {
            error(expression, "Undefined name '" + name + "'" + (name == "self" ? " (Dessa uses 'this')" : ""));
        }
        return SemanticType::unknown();
    }

    if (symbol->kind == SymbolKind::Module) {
        error(expression, "'" + name + "' is a module; use one of its names, e.g. " + name + ".something");
        return SemanticType::unknown();
    }
    identSymbols[expression] = *symbol;

    // fact(n - 1) inside fact's own lambda
    if (definingLambda && definingLambda->name && name == *definingLambda->name &&
        symbol->line == definingLambda->line && symbol->column == definingLambda->column) {
        recursiveLambdas.insert(definingLambda);
        recursiveRefs.insert(expression);
    }

    switch (symbol->kind) {
        case SymbolKind::Class:
        case SymbolKind::Interface:
        case SymbolKind::Enum:
            if (!allowTypeName) {
                error(expression, "'" + name + "' is a type, not a value" +
                      std::string(symbol->kind == SymbolKind::Class ? " (use new " + name + "(...) to create one)" : ""));
            }
            return symbol->type;
        case SymbolKind::Method:
            error(expression, "Method '" + name + "' must be called, e.g. " + name + "(...)");
            return SemanticType::unknown();
        default:
            break;
    }

    // Narrowed by an assignment, `!= nil` or `is`
    SemanticType type = symbol->type;
    if (auto narrowed = currentScope->narrowed(name)) type = *narrowed;
    if (hoverHits(expression->name)) recordHover(expression->name, *symbol, type);
    return type;
}

// ============================================================
// Editor support
// ============================================================

bool Analyser::hoverHits(const Token& name) const {
    return currentModule == hoverModule && name.line == hoverLine &&
           hoverColumn >= name.column && hoverColumn <= name.column + static_cast<int>(name.lexeme.size());
}

void Analyser::recordHover(const Token& name, const Symbol& symbol, const SemanticType& type, const std::string& memberOf) {
    (void)name;
    hover.found = true;
    hover.symbol = symbol;
    hover.type = type;
    hover.memberOf = memberOf;
}

std::vector<Symbol> Analyser::membersOf(const SemanticType& type) const {
    std::vector<Symbol> members;
    std::unordered_set<std::string> seen;
    auto add = [&](const Symbol& s) {
        if (seen.insert(s.name).second) members.push_back(s);
    };
    if (const ClassInfo* info = findClass(type)) {
        for (const ClassInfo* k = info; k; k = k->parent ? findClass(*k->parent) : nullptr) {
            for (const auto& f : k->fieldOrder) add(k->fields.at(f));
            for (const auto& [n, m] : k->methods) add(m);
        }
    } else if (const InterfaceInfo* info = findInterface(type)) {
        for (const auto& f : info->fieldOrder) {
            Symbol s;
            s.kind = SymbolKind::Field;
            s.name = f;
            s.type = info->fields.at(f);
            add(s);
        }
        for (const auto& m : info->methodOrder) add(info->methods.at(m));
    }
    // Every name builtinMethod() knows; it decides which apply to this type.
    static const char* const builtinNames[] = {
        "length", "isEmpty", "upper", "lower", "trim", "title", "plain", "caseless", "includes", "startsWith", "endsWith", "indexOf",
        "replace", "replaceAll", "split", "substr", "repeat", "chars", "isDigit", "isAlpha", "isSpace",
        "isUpper", "isLower", "code", "toChr", "toFixed", "name", "value", "add", "push", "pop", "insert",
        "remove", "removeAt", "clear", "copy", "first", "last", "get", "getOr", "set", "has", "keys",
        "values", "join", "slice", "sort", "reverse", "filter", "map", "forEach", "toList", "compare",
        "toStr", "toInt", "toDbl", "toFlt",
    };
    for (const char* n : builtinNames) {
        if (auto m = builtinMethod(type, n)) add(*m);
    }
    return members;
}

// ============================================================
// Operators
// ============================================================

SemanticType Analyser::analyseUnary(const UnaryExpr* expression) {
    SemanticType operand = analyseExpression(expression->operand.get());
    if (operand.isUnknown()) return operand;
    switch (expression->op.type) {
        case TokenType::NOT:
            if (!operand.isBoolean()) error(expression, "Operator '!' requires a Bul operand, got " + operand.toString());
            return simple(SemanticTypeKind::Bul);
        case TokenType::MINUS:
            if (!operand.isNumeric()) {
                error(expression, "Unary '-' requires a numeric operand, got " + operand.toString());
                return SemanticType::unknown();
            }
            {
                SemanticType result = operand;
                result.radix.reset();
                return result;
            }
        case TokenType::BITWISE_NOT:
            if (!operand.isInteger()) error(expression, "Operator '~' requires an Int operand, got " + operand.toString());
            return simple(SemanticTypeKind::Int);
        default:
            error(expression, "Unsupported unary operator '" + expression->op.lexeme + "'");
            return SemanticType::unknown();
    }
}

SemanticType Analyser::analysePostfix(const PostfixExpr* expression) {
    if (!isAssignableExpression(expression->operand.get())) {
        error(expression, "'" + expression->op.lexeme + "' needs a variable, field or list element");
        analyseExpression(expression->operand.get());
        return SemanticType::unknown();
    }
    const Expr* target = expression->operand.get();
    SemanticType operand = analyseAssignmentTarget(target);
    if (target->kind == ExprKind::Identifier && operand.containsNil()) {
        if (auto narrowed = currentScope->narrowed(static_cast<const IdentifierExpr*>(target)->name.lexeme)) {
            operand = *narrowed;
            exprTypes[target] = operand; // codegen unwraps it
        }
    }
    if (!operand.isNumeric() && !operand.isUnknown()) {
        error(expression, "'" + expression->op.lexeme + "' requires a numeric value, got " + operand.toString());
    }
    return operand;
}

SemanticType Analyser::analyseBinary(const BinaryExpr* expression) {
    const TokenType op = expression->op.type;

    // Short-circuit operators narrow their right-hand side:
    //   x != nil && x > 3      x == nil || x > 3
    if (op == TokenType::AND_AND || op == TokenType::OR_OR) {
        SemanticType left = analyseExpression(expression->left.get());
        Narrowings whenTrue, whenFalse;
        conditionNarrowings(expression->left.get(), whenTrue, whenFalse);
        beginScope(ScopeKind::Block);
        for (const auto& [name, type] : (op == TokenType::AND_AND ? whenTrue : whenFalse)) currentScope->narrow(name, type);
        SemanticType right = analyseExpression(expression->right.get());
        endScope();
        return binaryResult(expression->op, left, right, expression);
    }

    SemanticType left = analyseExpression(expression->left.get());
    SemanticType right = analyseExpression(expression->right.get());

    if ((op == TokenType::EQUAL_EQUAL || op == TokenType::BANG_EQUAL) &&
        (isNilLiteral(expression->left.get()) || isNilLiteral(expression->right.get()))) {
        const SemanticType& other = isNilLiteral(expression->left.get()) ? right : left;
        // Use the declared type: `x` may currently be narrowed.
        const Expr* otherExpr = unwrapGrouping(isNilLiteral(expression->left.get()) ? expression->right.get() : expression->left.get());
        bool nullable = other.containsNil() || other.isUnknown() || other.is(SemanticTypeKind::Obj);
        if (otherExpr && otherExpr->kind == ExprKind::Identifier) {
            if (const Symbol* s = symbolOf(otherExpr)) nullable = nullable || s->type.containsNil();
        }
        if (!nullable) {
            warning(expression, "Comparison with nil is always " + std::string(op == TokenType::EQUAL_EQUAL ? "false" : "true") +
                    ": " + other.toString() + " can never be nil");
        }
        return simple(SemanticTypeKind::Bul);
    }

    if (op == TokenType::QUESTION_QUESTION) {
        if (left.is(SemanticTypeKind::Obj)) return left;   // an Obj may hold nil
        if (!left.containsNil() && !left.isUnknown()) {
            warning(expression, "Left side of '?" "?' can never be nil (" + left.toString() + ")");
            return left;
        }
        SemanticType base = withoutNil(left);
        if (base.isUnknown()) return right;
        if (!isAssignable(base, right) && !isAssignable(right, base)) {
            // Int | Nil ?? Str -> Int | Str
            return SemanticType::unionOf({base, right});
        }
        if (right.containsNil()) return commonType(base, right);
        expectedTypes[expression->right.get()] = commonType(base, right);
        return commonType(base, right);
    }

    return binaryResult(expression->op, left, right, expression);
}

SemanticType Analyser::binaryResult(const Token& op, const SemanticType& left, const SemanticType& right, const Expr* at) {
    if (left.isUnknown() || right.isUnknown()) {
        switch (op.type) {
            case TokenType::EQUAL_EQUAL: case TokenType::BANG_EQUAL:
            case TokenType::L_ANGLE: case TokenType::R_ANGLE:
            case TokenType::LESS_EQUAL: case TokenType::GREATER_EQUAL:
            case TokenType::AND_AND: case TokenType::OR_OR:
                return simple(SemanticTypeKind::Bul);
            default:
                return SemanticType::unknown();
        }
    }

    auto numericResult = [&]() {
        if (left.kind == SemanticTypeKind::Dbl || right.kind == SemanticTypeKind::Dbl) return simple(SemanticTypeKind::Dbl);
        if (left.kind == SemanticTypeKind::Flt || right.kind == SemanticTypeKind::Flt) return simple(SemanticTypeKind::Flt);
        return simple(SemanticTypeKind::Int);
    };
    auto nilHint = [&]() -> std::string {
        if (left.containsNil() || right.containsNil()) return " (a value may be nil; check it with != nil or use ?? first)";
        if (left.is(SemanticTypeKind::Obj) || right.is(SemanticTypeKind::Obj)) return " (convert the Obj first, e.g. Int(x), or check its type with is)";
        return "";
    };

    switch (op.type) {
        case TokenType::PLUS:
        case TokenType::PLUS_EQUAL: {
            bool lStr = left.is(SemanticTypeKind::Str), rStr = right.is(SemanticTypeKind::Str);
            if ((lStr && (rStr || right.is(SemanticTypeKind::Chr))) || (rStr && left.is(SemanticTypeKind::Chr))) {
                return simple(SemanticTypeKind::Str);
            }
            if (left.isNumeric() && right.isNumeric()) return numericResult();
            error(at, "Operator '+' cannot be used with " + left.toString() + " and " + right.toString() +
                  (lStr || rStr ? " (use a template string, e.g. $\"{a}{b}\")" : nilHint()));
            return SemanticType::unknown();
        }

        case TokenType::MINUS: case TokenType::MINUS_EQUAL:
        case TokenType::MULT: case TokenType::MULT_EQUAL:
        case TokenType::DIV: case TokenType::DIV_EQUAL:
        case TokenType::MOD: case TokenType::MOD_EQUAL:
        case TokenType::EXPONENT: case TokenType::EXPONENT_EQUAL: {
            std::string symbol = op.lexeme.substr(0, op.lexeme.size() - (op.lexeme.size() > 1 && op.lexeme.back() == '=' ? 1 : 0));
            if (!left.isNumeric() || !right.isNumeric()) {
                error(at, "Operator '" + symbol + "' requires numeric operands, got " + left.toString() + " and " + right.toString() + nilHint());
                return SemanticType::unknown();
            }
            // Int / Int is a real division: 7 / 2 is 3.5. Stored into an Int
            // it is truncated (see isIntDivision in checkConversion).
            if (op.type == TokenType::DIV && left.isInteger() && right.isInteger()) {
                return simple(SemanticTypeKind::Dbl);
            }
            return numericResult();
        }

        case TokenType::AND_AND:
        case TokenType::OR_OR:
            if (!left.isBoolean() || !right.isBoolean()) {
                error(at, "Operator '" + op.lexeme + "' requires Bul operands, got " + left.toString() + " and " + right.toString());
            }
            return simple(SemanticTypeKind::Bul);

        case TokenType::BITWISE_AND:
        case TokenType::BITWISE_OR:
        case TokenType::BITWISE_XOR:
        case TokenType::LEFT_SHIFT:
        case TokenType::RIGHT_SHIFT:
            if (!left.isInteger() || !right.isInteger()) {
                error(at, "Bitwise operator '" + op.lexeme + "' requires Int operands, got " + left.toString() + " and " + right.toString());
                return SemanticType::unknown();
            }
            return simple(SemanticTypeKind::Int);

        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL:
            if (!isComparable(left, right)) {
                error(at, "Cannot compare " + left.toString() + " with " + right.toString());
            }
            return simple(SemanticTypeKind::Bul);

        case TokenType::LESS_EQUAL:
        case TokenType::L_ANGLE:
        case TokenType::GREATER_EQUAL:
        case TokenType::R_ANGLE: {
            bool ok = (left.isNumeric() && right.isNumeric()) ||
                      (left.is(SemanticTypeKind::Str) && right.is(SemanticTypeKind::Str)) ||
                      (left.is(SemanticTypeKind::Chr) && right.is(SemanticTypeKind::Chr));
            if (!ok) error(at, "Operator '" + op.lexeme + "' cannot order " + left.toString() + " and " + right.toString() + nilHint());
            return simple(SemanticTypeKind::Bul);
        }

        default:
            error(at, "Unsupported operator '" + op.lexeme + "'");
            return SemanticType::unknown();
    }
}

void Analyser::conditionNarrowings(const Expr* condition, Narrowings& whenTrue, Narrowings& whenFalse) {
    condition = unwrapGrouping(condition);
    if (!condition) return;

    if (condition->kind == ExprKind::Unary) {
        const auto* u = static_cast<const UnaryExpr*>(condition);
        if (u->op.type == TokenType::NOT) conditionNarrowings(u->operand.get(), whenFalse, whenTrue);
        return;
    }

    // x is T: x is a T when true; for a union, x is one of the other
    // members when false (Int | Str: `x is Int` false -> x is Str).
    if (condition->kind == ExprKind::Is) {
        const auto* is = static_cast<const IsExpr*>(condition);
        const Expr* value = unwrapGrouping(is->value.get());
        if (!value || value->kind != ExprKind::Identifier) return;
        const auto* id = static_cast<const IdentifierExpr*>(value);
        const Symbol* symbol = resolveSymbol(id->name.lexeme);
        if (!symbol || (symbol->kind != SymbolKind::Variable && symbol->kind != SymbolKind::Constant &&
                        symbol->kind != SymbolKind::Parameter)) return;
        const size_t errorCount = diagnostics.size();
        SemanticType target = resolveType(is->type, is->line, is->column);
        diagnostics.resize(errorCount); // already reported by analyseIs
        if (target.isUnknown() || target.kind == SemanticTypeKind::Union) return;
        whenTrue.emplace_back(id->name.lexeme, target);

        SemanticType current = currentScope->narrowed(id->name.lexeme).value_or(symbol->type);
        if (current.kind == SemanticTypeKind::Union && !target.isReference()) {
            std::vector<SemanticType> rest;
            for (const auto& m : current.unionMembers) if (m != target) rest.push_back(m);
            if (rest.size() < current.unionMembers.size() && !rest.empty()) {
                whenFalse.emplace_back(id->name.lexeme, SemanticType::unionOf(rest));
            }
        }
        return;
    }
    if (condition->kind != ExprKind::Binary) return;
    const auto* b = static_cast<const BinaryExpr*>(condition);

    if (b->op.type == TokenType::AND_AND) {
        Narrowings ignore;
        conditionNarrowings(b->left.get(), whenTrue, ignore);
        conditionNarrowings(b->right.get(), whenTrue, ignore);
        return;
    }
    if (b->op.type == TokenType::OR_OR) {
        Narrowings ignore;
        conditionNarrowings(b->left.get(), ignore, whenFalse);
        conditionNarrowings(b->right.get(), ignore, whenFalse);
        return;
    }
    if (b->op.type != TokenType::EQUAL_EQUAL && b->op.type != TokenType::BANG_EQUAL) return;

    const Expr* other = nullptr;
    if (isNilLiteral(b->right.get())) other = unwrapGrouping(b->left.get());
    else if (isNilLiteral(b->left.get())) other = unwrapGrouping(b->right.get());
    if (!other || other->kind != ExprKind::Identifier) return;

    const auto* id = static_cast<const IdentifierExpr*>(other);
    const Symbol* symbol = resolveSymbol(id->name.lexeme);
    if (!symbol || !symbol->type.containsNil()) return;
    if (symbol->kind != SymbolKind::Variable && symbol->kind != SymbolKind::Constant && symbol->kind != SymbolKind::Parameter) return;

    auto& target = b->op.type == TokenType::BANG_EQUAL ? whenTrue : whenFalse;
    target.emplace_back(id->name.lexeme, withoutNil(symbol->type));
}

// ============================================================
// Assignment
// ============================================================

SemanticType Analyser::analyseAssignmentTarget(const Expr* target) {
    if (target->kind == ExprKind::Identifier) {
        const auto* identifier = static_cast<const IdentifierExpr*>(target);
        const std::string& name = identifier->name.lexeme;
        if (identifier->name.type == TokenType::THIS || identifier->name.type == TokenType::SUPER) {
            error(target, "Cannot assign to '" + name + "'");
            return SemanticType::unknown();
        }
        Symbol* symbol = resolveSymbol(name);
        if (!symbol) {
            error(target, "Undefined name '" + name + "'");
            return SemanticType::unknown();
        }
        identSymbols[target] = *symbol;
        switch (symbol->kind) {
            case SymbolKind::Variable:
            case SymbolKind::Parameter:
            case SymbolKind::Field:
                break;
            case SymbolKind::Constant:
                error(target, "Cannot assign to constant '" + name + "'");
                return symbol->type;
            default:
                error(target, "Cannot assign to '" + name + "'");
                return SemanticType::unknown();
        }
        if (symbol->isConst) {
            error(target, "Cannot assign to constant '" + name + "'");
            return symbol->type;
        }
        // Lambdas capture outer locals by value, so writes wouldn't be seen.
        if (!symbol->isGlobal && symbol->kind != SymbolKind::Field) {
            for (Scope* s = currentScope; s && !s->resolveLocal(name); s = s->parent()) {
                if (s->kind() == ScopeKind::Lambda) {
                    error(target, "A lambda cannot modify '" + name + "' from the enclosing function (it is captured by value)");
                    break;
                }
            }
        }
        exprTypes[target] = symbol->type;
        return symbol->type;
    }

    if (target->kind == ExprKind::Member) {
        const auto* member = static_cast<const MemberExpr*>(target);
        if (member->isOptional) {
            error(target, "Cannot assign through '?.'; check for nil first");
        }
        SemanticType type = analyseExpression(target);
        if (dynamicMembers.count(target)) {
            exprTypes[target] = type;
            return type;   // o.message = ... on an Obj
        }
        const Symbol* symbol = memberOf(target);
        if (!symbol) return SemanticType::unknown();
        if (symbol->kind != SymbolKind::Field) {
            error(target, "Cannot assign to '" + member->name.lexeme + "'");
            return SemanticType::unknown();
        }
        if (symbol->isConst) {
            error(target, "Cannot assign to constant field '" + member->name.lexeme + "'");
        }
        if (isOptionalChain(target)) {
            error(target, "Cannot assign through an optional chain; check for nil first");
        }
        exprTypes[target] = symbol->type;
        (void)type;
        return symbol->type;
    }

    if (target->kind == ExprKind::Index) {
        const auto* index = static_cast<const IndexExpr*>(target);
        SemanticType objectType = analyseExpression(index->object.get());
        SemanticType indexType = analyseExpression(index->index.get());
        if (objectType.is(SemanticTypeKind::Map)) {
            // ages<"Cy"> = 41 adds or replaces an entry
            if (objectType.keyType) checkConversion(*objectType.keyType, index->index.get(), indexType, "as a map key");
            SemanticType value = objectType.elementType ? *objectType.elementType : SemanticType::unknown();
            exprTypes[target] = value;
            return value;
        }
        if (!indexType.isInteger() && !indexType.isUnknown()) {
            error(index->index.get(), "List index must be Int, got " + indexType.toString());
        }
        if (isNegativeLiteral(index->index.get())) {
            error(index->index.get(), "Negative indexes are not allowed; indexes start at 0");
        }
        if (objectType.is(SemanticTypeKind::List)) {
            SemanticType element = objectType.elementType ? *objectType.elementType : SemanticType::unknown();
            exprTypes[target] = element;
            return element;
        }
        if (!objectType.isUnknown()) {
            error(target, objectType.is(SemanticTypeKind::Str) ? "Strings cannot be changed in place; build a new Str instead"
                                                              : "Only list elements can be assigned through <index>, not " + objectType.toString());
        }
        return SemanticType::unknown();
    }

    error(target, "Invalid assignment target");
    analyseExpression(target);
    return SemanticType::unknown();
}

SemanticType Analyser::analyseAssignment(const AssignExpr* expression) {
    const Expr* target = expression->target.get();
    if (!isAssignableExpression(target)) {
        error(expression, "Invalid assignment target: only variables, fields and list elements can be assigned");
        analyseExpression(target);
        analyseExpression(expression->value.get());
        return SemanticType::unknown();
    }

    SemanticType targetType = analyseAssignmentTarget(target);
    SemanticType valueType;

    if (expression->op.type == TokenType::EQUAL) {
        if (expression->value->kind == ExprKind::Lambda && targetType.kind == SemanticTypeKind::Function && targetType.returnType) {
            valueType = analyseLambda(static_cast<const LambdaExpr*>(expression->value.get()), *targetType.returnType);
            exprTypes[expression->value.get()] = valueType;
        } else {
            valueType = analyseExpression(expression->value.get());
        }
        if (valueType.isVoidLike()) {
            error(expression->value.get(), "Expression has no value (Emp) and cannot be assigned");
        } else if (!targetType.isUnknown()) {
            checkConversion(targetType, expression->value.get(), valueType, "in assignment");
        }
    } else {
        valueType = analyseExpression(expression->value.get());
        SemanticType current = targetType;
        if (target->kind == ExprKind::Identifier && targetType.containsNil()) {
            if (auto narrowed = currentScope->narrowed(static_cast<const IdentifierExpr*>(target)->name.lexeme)) current = *narrowed;
        }
        SemanticType result = binaryResult(expression->op, current, valueType, expression);
        if (!result.isUnknown() && !isAssignable(targetType, result)) {
            error(expression, "Result of '" + expression->op.lexeme + "' is " + result.toString() +
                  ", which cannot be stored in " + targetType.toString());
        }
        valueType = result;
    }

    if (target->kind == ExprKind::Identifier) {
        narrowAfterAssignment(static_cast<const IdentifierExpr*>(target)->name.lexeme, valueType);
    }
    return targetType;
}

// ============================================================
// Ternary, index, collections
// ============================================================

SemanticType Analyser::analyseTernary(const TernaryExpr* expression) {
    analyseCondition(expression->condition.get(), "Ternary condition");
    Narrowings whenTrue, whenFalse;
    conditionNarrowings(expression->condition.get(), whenTrue, whenFalse);

    beginScope(ScopeKind::Block);
    for (const auto& [n, t] : whenTrue) currentScope->narrow(n, t);
    SemanticType thenType = analyseExpression(expression->thenBranch.get());
    endScope();

    beginScope(ScopeKind::Block);
    for (const auto& [n, t] : whenFalse) currentScope->narrow(n, t);
    SemanticType elseType = analyseExpression(expression->elseBranch.get());
    endScope();

    SemanticType result = commonType(thenType, elseType);
    expectedTypes[expression->thenBranch.get()] = result;
    expectedTypes[expression->elseBranch.get()] = result;
    return result;
}

SemanticType Analyser::analyseIndex(const IndexExpr* expression) {
    SemanticType objectType = analyseExpression(expression->object.get());
    SemanticType indexType = analyseExpression(expression->index.get());

    if (objectType.is(SemanticTypeKind::Map)) {
        // ages<"Ann">: the value for a key (an error at run time if it's missing)
        if (objectType.keyType) checkConversion(*objectType.keyType, expression->index.get(), indexType, "as a map key");
        return objectType.elementType ? *objectType.elementType : SemanticType::unknown();
    }
    if (!indexType.isInteger() && !indexType.isUnknown()) {
        error(expression->index.get(), "Index must be Int, got " + indexType.toString());
    }
    if (isNegativeLiteral(expression->index.get())) {
        error(expression->index.get(), "Negative indexes are not allowed; indexes start at 0 (use list.len() - 1 for the last item)");
    }
    if (objectType.is(SemanticTypeKind::List)) {
        return objectType.elementType ? *objectType.elementType : SemanticType::unknown();
    }
    if (objectType.is(SemanticTypeKind::Str)) return simple(SemanticTypeKind::Chr);
    if (objectType.isUnknown()) return objectType;

    error(expression, "Type " + objectType.toString() + " cannot be indexed with <...>" +
          (objectType.is(SemanticTypeKind::Set) ? " (sets are unordered; use .contains() or a for-in loop)" : ""));
    return SemanticType::unknown();
}

// {"Ann": 30, "Bo": 25} -> Map[Str, Int]
SemanticType Analyser::analyseMapLiteral(const MapExpr* expression) {
    SemanticType keyType = SemanticType::unknown();
    SemanticType valueType = SemanticType::unknown();
    bool first = true;
    for (const auto& entry : expression->entries) {
        if (!entry.key) {
            // {...other, ...}
            SemanticType other = analyseExpression(entry.value.get());
            if (!other.isUnknown() && other.kind != SemanticTypeKind::Map) {
                error(entry.value.get(), "Map spread '...' requires a map, got " + other.toString());
                continue;
            }
            if (other.keyType && !other.keyType->isUnknown()) keyType = first ? *other.keyType : commonType(keyType, *other.keyType);
            if (other.elementType && !other.elementType->isUnknown()) {
                valueType = first ? *other.elementType : commonType(valueType, *other.elementType);
            }
            first = false;
            continue;
        }
        SemanticType k = analyseExpression(entry.key.get());
        SemanticType v = analyseExpression(entry.value.get());
        if (v.isVoidLike()) {
            error(entry.value.get(), "Expression with no value (Emp) cannot be a map value");
            continue;
        }
        keyType = first ? k : commonType(keyType, k);
        valueType = first ? v : commonType(valueType, v);
        first = false;
    }
    if (keyType.kind == SemanticTypeKind::Union) {
        error(expression, "All keys of a map literal must have the same type, got " + keyType.toString());
        keyType = SemanticType::unknown(); // don't report the same problem again
    }
    return SemanticType::mapOf(keyType, valueType);
}

SemanticType Analyser::analyseCollection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at) {
    SemanticType elementType = SemanticType::unknown();
    bool first = true;
    const SemanticTypeKind kind = isSet ? SemanticTypeKind::Set : SemanticTypeKind::List;

    for (const auto& element : elements) {
        SemanticType type = analyseExpression(element.value.get());
        if (element.isSpread) {
            if (type.kind != kind && !type.isUnknown()) {
                error(element.value.get(), std::string(isSet ? "Set" : "List") + " spread '...' requires a " +
                      (isSet ? "set" : "list") + ", got " + type.toString());
                continue;
            }
            if (!type.elementType || type.elementType->isUnknown()) continue;
            type = *type.elementType;
        }
        if (type.isVoidLike()) {
            error(element.value.get(), "Expression with no value (Emp) cannot be a collection element");
            continue;
        }
        if (first) {
            elementType = type;
            first = false;
        } else {
            elementType = commonType(elementType, type);
        }
    }
    (void)at;
    return isSet ? SemanticType::setOf(elementType) : SemanticType::listOf(elementType);
}

// ============================================================
// Members
// ============================================================

const Symbol* Analyser::findClassMember(const std::string& classKey, const std::string& name) const {
    auto it = classes.find(classKey);
    for (int guard = 0; it != classes.end() && guard < 64; ++guard) {
        const ClassInfo& info = it->second;
        if (auto f = info.fields.find(name); f != info.fields.end()) return &f->second;
        if (auto m = info.methods.find(name); m != info.methods.end()) return &m->second;
        if (!info.parent) break;
        it = classes.find(info.parent->key());
    }
    return nullptr;
}

const Symbol* Analyser::findMember(const SemanticType& objectType, const std::string& name) const {
    if (objectType.isClass()) return findClassMember(objectType.key(), name);
    if (const InterfaceInfo* iface = findInterface(objectType)) {
        if (auto m = iface->methods.find(name); m != iface->methods.end()) return &m->second;
        if (auto f = iface->fields.find(name); f != iface->fields.end()) {
            // Interface fields are synthesised as symbols on demand.
            static thread_local Symbol field;
            field = Symbol{};
            field.kind = SymbolKind::Field;
            field.name = name;
            field.type = f->second;
            field.ownerClass = objectType.key();
            return &field;
        }
    }
    return nullptr;
}

std::optional<Symbol> Analyser::builtinMethod(const SemanticType& receiver, const std::string& name) const {
    using K = SemanticTypeKind;
    auto make = [&](std::vector<FunctionParameterSymbol> params, SemanticType ret) {
        Symbol s;
        s.kind = SymbolKind::Method;
        s.name = name;
        s.cppName = "dessa::m_" + name;
        s.parameters = std::move(params);
        s.returnType = std::move(ret);
        std::vector<SemanticType> pt;
        for (const auto& p : s.parameters) pt.push_back(p.type);
        s.type = SemanticType::functionOf(pt, s.returnType);
        return std::optional<Symbol>(s);
    };
    const SemanticType Str = simple(K::Str), Int = simple(K::Int), Bul = simple(K::Bul),
                       Dbl = simple(K::Dbl), Chr = simple(K::Chr), Emp = simple(K::Emp), Flt = simple(K::Flt);

    if (receiver.is(K::Str)) {
        if (name == "length") return make({}, Int);
        if (name == "isEmpty") return make({}, Bul);
        if (name == "upper" || name == "lower" || name == "trim" || name == "title" || name == "plain" || name == "caseless") return make({}, Str);
        if (name == "includes" || name == "startsWith" || name == "endsWith") return make({{"text", Str}}, Bul);
        if (name == "indexOf") return make({{"text", Str}}, Int);
        if (name == "replace" || name == "replaceAll") return make({{"target", Str}, {"replacement", Str}}, Str);
        if (name == "split") return make({{"separator", Str}}, SemanticType::listOf(Str));
        if (name == "substr") return make({{"start", Int}, {"length", Int}}, Str);
        if (name == "repeat") return make({{"count", Int}}, Str);
        if (name == "chars") return make({}, SemanticType::listOf(Chr));
        if (name == "toInt") return make({}, Int);
        if (name == "toDbl") return make({}, Dbl);
        if (name == "toFlt") return make({}, Flt);
        if (name == "toStr") return make({}, Str);
        if (name == "compare") return make({{"other", Str}}, Int);
        return std::nullopt;
    }
    if (receiver.is(K::Chr)) {
        if (name == "isDigit" || name == "isAlpha" || name == "isSpace" || name == "isUpper" || name == "isLower") return make({}, Bul);
        if (name == "upper" || name == "lower") return make({}, Chr);
        if (name == "code") return make({}, Int);
        if (name == "toStr") return make({}, Str);
        return std::nullopt;
    }
    if (receiver.isNumeric()) {
        if (name == "toStr") return make({}, Str);
        if (name == "toInt") return make({}, Int);
        if (name == "toDbl") return make({}, Dbl);
        if (name == "toFlt") return make({}, Flt);
        if (name == "toChr" && receiver.isInteger()) return make({}, Chr);
        if (name == "toFixed" && !receiver.isInteger()) return make({{"digits", Int}}, Str);
        return std::nullopt;
    }
    if (receiver.is(K::Bul)) {
        if (name == "toStr") return make({}, Str);
        return std::nullopt;
    }
    if (receiver.isEnum()) {
        if (name == "name" || name == "toStr") return make({}, Str);
        if (name == "value") return make({}, Int);
        return std::nullopt;
    }
    if (receiver.is(K::Map)) {
        SemanticType Key = receiver.keyType ? *receiver.keyType : SemanticType::unknown();
        SemanticType Val = receiver.elementType ? *receiver.elementType : SemanticType::unknown();
        if (name == "length") return make({}, Int);
        if (name == "isEmpty") return make({}, Bul);
        if (name == "has") return make({{"key", Key}}, Bul);
        if (name == "get") return make({{"key", Key}}, withNil(Val));
        if (name == "getOr") return make({{"key", Key}, {"fallback", Val}}, Val);
        if (name == "set") return make({{"key", Key}, {"value", Val}}, Emp);
        if (name == "remove") return make({{"key", Key}}, Bul);
        if (name == "keys") return make({}, SemanticType::listOf(Key));
        if (name == "values") return make({}, SemanticType::listOf(Val));
        if (name == "clear") return make({}, Emp);
        if (name == "copy") return make({}, receiver);
        if (name == "forEach") return make({{"action", SemanticType::functionOf({Key, Val}, Emp)}}, Emp);
        return std::nullopt;
    }
    if (receiver.is(K::List) || receiver.is(K::Set)) {
        SemanticType T = receiver.elementType ? *receiver.elementType : SemanticType::unknown();
        SemanticType self = receiver;
        if (name == "length") return make({}, Int);
        if (name == "isEmpty") return make({}, Bul);
        if (name == "includes") return make({{"value", T}}, Bul);
        if (name == "clear") return make({}, Emp);
        if (name == "forEach") return make({{"action", SemanticType::functionOf({T}, Emp)}}, Emp);
        if (name == "filter") return make({{"keep", SemanticType::functionOf({T}, Bul)}}, self);
        if (name == "map") return make({{"transform", SemanticType::functionOf({T}, SemanticType::unknown())}}, SemanticType::unknown());
        if (name == "toList") return make({}, SemanticType::listOf(T));
        if (receiver.is(K::Set)) {
            if (name == "add") return make({{"value", T}}, Emp);
            if (name == "remove") return make({{"value", T}}, Bul);
            return std::nullopt;
        }
        if (name == "push") return make({{"value", T}}, Emp);
        if (name == "pop" || name == "first" || name == "last") return make({}, T);
        if (name == "insert") return make({{"index", Int}, {"value", T}}, Emp);
        if (name == "removeAt") return make({{"index", Int}}, T);
        if (name == "remove") return make({{"value", T}}, Bul);
        if (name == "indexOf") return make({{"value", T}}, Int);
        if (name == "reverse" || name == "sort") return make({}, Emp);
        if (name == "slice") return make({{"start", Int}, {"end", Int}}, self);
        if (name == "copy") return make({}, self);
        if (name == "join") return make({{"separator", Str}}, Str);
        return std::nullopt;
    }
    return std::nullopt;
}

// m.name with $("math" as m): the symbol it names (nullptr if m isn't a module)
const Symbol* Analyser::aliasMember(const MemberExpr* expression, bool report) {
    const Expr* object = expression->object.get();
    if (object->kind != ExprKind::Identifier) return nullptr;
    const Symbol* module = resolveSymbol(static_cast<const IdentifierExpr*>(object)->name.lexeme);
    if (!module || module->kind != SymbolKind::Module) return nullptr;
    const auto& table = aliasTables[static_cast<size_t>(module->aliasTable)];
    auto it = table.find(expression->name.lexeme);
    if (it == table.end()) {
        if (report) error(expression, "Module '" + module->name + "' has no '" + expression->name.lexeme + "'");
        static const Symbol missing;
        return &missing;
    }
    return &it->second;
}

SemanticType Analyser::analyseMember(const MemberExpr* expression) {
    const Expr* object = expression->object.get();
    const std::string& name = expression->name.lexeme;

    // m.sqrt, m.PI, m.Color with $("math" as m)
    if (const Symbol* target = aliasMember(expression, true)) {
        if (target->name.empty()) return SemanticType::unknown();
        identSymbols[expression] = *target;
        aliasRefs.insert(expression);
        if (hoverHits(expression->name)) recordHover(expression->name, *target, target->type);
        return target->type;
    }
    SemanticType objectType = analyseExpression(object, true);

    // Editor completion: obj.|
    if (!probeName.empty() && name == probeName) {
        if (!completion.found) {
            completion.found = true;
            completion.isMember = true;
            completion.objectType = withoutNil(objectType);
            const Symbol* objSymbol = object->kind == ExprKind::Identifier ? symbolOf(object) : nullptr;
            completion.objectIsType = objSymbol && (objSymbol->kind == SymbolKind::Enum ||
                                                    objSymbol->kind == SymbolKind::Class ||
                                                    objSymbol->kind == SymbolKind::Interface);
            completion.objectIsThis = object->kind == ExprKind::Identifier &&
                                      static_cast<const IdentifierExpr*>(object)->name.type == TokenType::THIS;
        }
        return SemanticType::unknown();
    }
    if (objectType.isUnknown()) return objectType;

    // Enum value: Status.ONGOING (or m.Status.ONGOING)
    if (object->kind == ExprKind::Identifier || aliasRefs.count(object)) {
        const Symbol* objSymbol = symbolOf(object);
        if (objSymbol && objSymbol->kind == SymbolKind::Enum) {
            const EnumInfo* info = findEnum(objectType);
            if (info && std::find(info->values.begin(), info->values.end(), name) != info->values.end()) {
                Symbol value;
                value.kind = SymbolKind::Constant;
                value.name = name;
                value.type = objectType;
                value.ownerClass = objectType.key();
                value.isConst = true;
                memberSymbols[expression] = value;
                if (hoverHits(expression->name)) recordHover(expression->name, value, objectType, objectType.toString());
                return objectType;
            }
            error(expression, "Enum '" + objectType.name + "' has no value '" + name + "'");
            return SemanticType::unknown();
        }
        if (objSymbol && (objSymbol->kind == SymbolKind::Class || objSymbol->kind == SymbolKind::Interface)) {
            error(expression, "'" + name + "' must be accessed on an instance of " + objectType.name + ", not on the type itself");
            return SemanticType::unknown();
        }
    }

    const bool isSuper = object->kind == ExprKind::Identifier &&
                         static_cast<const IdentifierExpr*>(object)->name.type == TokenType::SUPER;

    // Nil-safety and optional chaining
    bool chained = expression->isOptional || isOptionalChain(object);
    if (objectType.containsNil()) {
        if (!chained) {
            error(expression, "Value of type " + objectType.toString() + " may be nil; use '?." + name +
                  "' or check it with != nil first");
            return SemanticType::unknown();
        }
        objectType = withoutNil(objectType);
        optionalChains.insert(expression);
    } else if (expression->isOptional) {
        // Only warn when the declared type can't be nil; a nullable variable
        // that is merely narrowed here is fine to access with ?.
        const Symbol* declared = object->kind == ExprKind::Identifier ? symbolOf(object) : nullptr;
        if (!declared || !declared->type.containsNil()) {
            warning(expression, "'?.' is unnecessary: " + objectType.toString() + " can never be nil");
        }
        chained = false;
    } else if (chained) {
        optionalChains.insert(expression);
    }

    // o.message on an Obj: whatever the value has, found when the program runs
    if (objectType.is(SemanticTypeKind::Obj)) {
        dynamicMembers.insert(expression);
        return objectType;
    }

    SemanticType memberType = SemanticType::unknown();
    if (isSuper && expression->name.type == TokenType::INIT) {
        const ClassInfo* parent = findClass(objectType);
        Symbol ctor;
        for (const ClassInfo* p = parent; p; p = p->parent ? findClass(*p->parent) : nullptr) {
            if (p->constructor) { ctor = *p->constructor; break; }
        }
        ctor.kind = SymbolKind::Method;
        ctor.name = "init";
        ctor.returnType = simple(SemanticTypeKind::Emp);
        ctor.ownerClass = objectType.key();
        if (!inConstructor) error(expression, "super(...) can only be called inside init");
        memberSymbols[expression] = ctor;
        return SemanticType::unknown(); // must be called; analyseCall handles it
    }

    if (const Symbol* member = findMember(objectType, name)) {
        memberSymbols[expression] = *member;
        memberType = member->type;
        if (hoverHits(expression->name)) recordHover(expression->name, *member, member->type, objectType.toString());
        if (member->kind == SymbolKind::Method) {
            // A method must be called; analyseCall reads memberSymbols.
            return memberType;
        }
        if (isSuper) {
            error(expression, "Use this." + name + " to access fields; super is only for calling parent methods");
        }
    } else if (auto builtin = builtinMethod(objectType, name)) {
        memberSymbols[expression] = *builtin;
        if (hoverHits(expression->name)) recordHover(expression->name, *builtin, builtin->type, objectType.toString());
        return builtin->type;
    } else {
        error(expression, "Type " + objectType.toString() + " has no member '" + name + "'");
        return SemanticType::unknown();
    }

    return chained ? withNil(memberType) : memberType;
}

// ============================================================
// Calls
// ============================================================

ArgumentOrder Analyser::checkArguments(const std::string& name,
                                       const std::vector<FunctionParameterSymbol>& parameters,
                                       const std::vector<Argument>& args,
                                       const Expr* at,
                                       bool variadic,
                                       std::vector<SemanticType>* argTypes) {
    ArgumentOrder order(parameters.size());

    if (variadic) {
        for (const auto& arg : args) {
            if (arg.isSpread) error(arg.value.get(), "'..." "' can't be used with '" + name + "'; pass the list itself");
        }
        // print(a, b, sep: ", ", end: "") -- the only named options of a variadic builtin
        std::unordered_set<std::string> seenOptions;
        for (const auto& arg : args) {
            if (arg.name) {
                const std::string& option = *arg.name;
                SemanticType t = analyseExpression(arg.value.get());
                if (name != "print" || (option != "sep" && option != "end")) {
                    error(arg.value.get(), "'" + name + "' has no option named '" + option + "'" +
                          (name == "print" ? " (only sep and end)" : ""));
                } else if (!seenOptions.insert(option).second) {
                    error(arg.value.get(), "'" + option + "' is given more than once");
                } else {
                    checkConversion(SemanticType::simple(SemanticTypeKind::Str), arg.value.get(), t,
                                    "for '" + option + "' of print");
                }
                continue;
            }
            SemanticType t = analyseExpression(arg.value.get());
            if (t.isVoidLike()) error(arg.value.get(), "Expression has no value (Emp) and cannot be passed to '" + name + "'");
            if (argTypes) argTypes->push_back(t);
        }
        return order;
    }

    bool namedStarted = false;
    bool badName = false;
    size_t nextPositional = 0;
    size_t extra = 0; // positional arguments with no parameter left
    std::vector<SemanticType> types(parameters.size(), SemanticType::unknown());
    int variadicIndex = -1;
    for (size_t i = 0; i < parameters.size(); ++i) if (parameters[i].variadic) variadicIndex = static_cast<int>(i);

    for (size_t a = 0; a < args.size(); ++a) {
        const Argument& argument = args[a];
        size_t index = parameters.size();

        if (argument.isSpread) {
            // greet(...names) -- a whole list into the variadic parameter
            if (variadicIndex < 0 || argument.name) {
                error(argument.value.get(), "'...' can only pass a list to a variadic parameter (declared like names[Str*])");
                badName = true;
                analyseExpression(argument.value.get());
                continue;
            }
            if (namedStarted) error(argument.value.get(), "Positional arguments cannot follow named arguments");
            const auto& p = parameters[static_cast<size_t>(variadicIndex)];
            SemanticType actual = analyseExpression(argument.value.get());
            if (!actual.isUnknown() && actual.kind != SemanticTypeKind::List) {
                error(argument.value.get(), "'...' needs a list, got " + actual.toString());
            } else {
                checkConversion(p.type, argument.value.get(), actual, "for '..." + p.name + "' of '" + name + "'");
            }
            order[static_cast<size_t>(variadicIndex)].push_back(static_cast<int>(a));
            continue;
        }

        if (argument.name) {
            namedStarted = true;
            for (size_t i = 0; i < parameters.size(); ++i) {
                if (parameters[i].name == *argument.name) { index = i; break; }
            }
            if (index == parameters.size()) {
                error(argument.value.get(), "'" + name + "' has no parameter named '" + *argument.name + "'");
                badName = true;
                analyseExpression(argument.value.get());
                continue;
            }
            if (parameters[index].variadic) {
                error(argument.value.get(), "Variadic parameter '" + parameters[index].name + "' can't be passed by name; " +
                      "list the values without a name, or pass a list with ...");
                analyseExpression(argument.value.get());
                continue;
            }
        } else {
            if (namedStarted) error(argument.value.get(), "Positional arguments cannot follow named arguments");
            while (nextPositional < parameters.size() && !parameters[nextPositional].variadic && !order[nextPositional].empty()) {
                ++nextPositional;
            }
            index = nextPositional;
            if (index >= parameters.size()) {
                ++extra;
                analyseExpression(argument.value.get());
                continue; // reported below
            }
        }

        if (!parameters[index].variadic && !order[index].empty()) {
            error(argument.value.get(), "Parameter '" + parameters[index].name + "' of '" + name + "' is given more than once");
            analyseExpression(argument.value.get());
            continue;
        }
        order[index].push_back(static_cast<int>(a));

        // A variadic parameter checks each value against its element type.
        const SemanticType expected = parameters[index].variadic && parameters[index].type.elementType
            ? *parameters[index].type.elementType : parameters[index].type;
        const Expr* valueExpr = argument.value.get();
        SemanticType actual;
        if (valueExpr->kind == ExprKind::Lambda && expected.kind == SemanticTypeKind::Function && expected.returnType &&
            !expected.returnType->isUnknown() && !expected.returnType->isVoidLike()) {
            actual = analyseLambda(static_cast<const LambdaExpr*>(valueExpr), *expected.returnType);
            exprTypes[valueExpr] = actual;
        } else {
            actual = analyseExpression(valueExpr);
        }
        types[index] = actual;

        if (actual.isVoidLike()) {
            error(valueExpr, "Expression has no value (Emp) and cannot be passed as '" + parameters[index].name + "'");
        } else if (expected.isUnknown() && expected.name == "Num") {
            if (!actual.isNumeric() && !actual.isUnknown()) {
                error(valueExpr, "'" + parameters[index].name + "' of '" + name + "' must be numeric, got " + actual.toString());
            }
        } else if (expected.isUnknown() && expected.name == "List") {
            // any list (random's pick / shuffle)
            if (!actual.is(SemanticTypeKind::List) && !actual.isUnknown()) {
                error(valueExpr, "'" + parameters[index].name + "' of '" + name + "' must be a list, got " + actual.toString());
            }
        } else if (!expected.isUnknown()) {
            checkConversion(expected, valueExpr, actual, "for parameter '" + parameters[index].name + "' of '" + name + "'");
        }
    }

    if (extra > 0) {
        error(at, "'" + name + "' takes " + std::to_string(parameters.size()) + " argument(s), but " +
              std::to_string(args.size()) + " were given");
    } else if (!badName) {
        std::string missing;
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (order[i].empty() && !parameters[i].variadic && !parameters[i].defaultValue) {
                missing += (missing.empty() ? "" : ", ") + parameters[i].name;
            }
        }
        if (!missing.empty()) error(at, "Missing argument(s) for '" + name + "': " + missing);
    }

    if (argTypes) *argTypes = std::move(types);
    return order;
}

SemanticType Analyser::callResult(const Symbol& callee, const std::vector<SemanticType>& argTypes) const {
    const SemanticType& ret = callee.returnType;
    // Same: the type of the first argument; Elem: the type of its elements
    if (ret.isUnknown() && (ret.name == "Same" || ret.name == "Elem")) {
        if (argTypes.empty()) return SemanticType::unknown();
        if (ret.name == "Same") return argTypes.front();
        return argTypes.front().elementType ? *argTypes.front().elementType : SemanticType::unknown();
    }
    if (ret.isUnknown() && ret.name == "T") {
        SemanticType result = SemanticType::unknown();
        for (size_t i = 0; i < callee.parameters.size() && i < argTypes.size(); ++i) {
            if (callee.parameters[i].type.name == "Num") result = commonType(result, argTypes[i]);
        }
        result.radix.reset();
        return result;
    }
    return ret;
}

// zip(a, b)          -> [[a0, b0], [a1, b1], ...]  (A<><>, or (A | B)<><> when the types differ)
// zip(a, b, combine) -> [combine(a0, b0), ...]     (R<>)
// Stops at the shorter list. No generics in Dessa, so the types are worked out here.
SemanticType Analyser::analyseZip(const CallExpr* expression, const Symbol& symbol) {
    identSymbols[expression->callee.get()] = symbol;
    exprTypes[expression->callee.get()] = symbol.type;
    const auto& args = expression->args;
    bool bad = args.size() < 2 || args.size() > 3;
    for (const auto& a : args) bad = bad || a.name || a.isSpread;
    std::vector<SemanticType> types;
    for (const auto& a : args) types.push_back(analyseExpression(a.value.get()));
    if (bad) {
        error(expression, "zip takes two lists and an optional function: zip(a, b) or zip(a, b, (x, y) := ...)");
        return SemanticType::unknown();
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!types[i].isUnknown() && !types[i].is(SemanticTypeKind::List)) {
            error(args[i].value.get(), "zip needs lists, got " + types[i].toString());
            return SemanticType::unknown();
        }
    }
    if (types[0].isUnknown() || types[1].isUnknown()) return SemanticType::unknown();
    const SemanticType& a = *types[0].elementType;
    const SemanticType& b = *types[1].elementType;
    if (args.size() == 3) {
        const SemanticType& f = types[2];
        if (f.isUnknown()) return SemanticType::unknown();
        if (!f.is(SemanticTypeKind::Function) || f.parameterTypes.size() != 2 || !f.returnType || f.returnType->isVoidLike()) {
            error(args[2].value.get(), "zip's third argument must be a function of two values that returns a value, e.g. (x[" +
                  a.toString() + "], y[" + b.toString() + "]) := ...");
            return SemanticType::unknown();
        }
        if (!isAssignable(f.parameterTypes[0], a) || !isAssignable(f.parameterTypes[1], b)) {
            error(args[2].value.get(), "zip's function must take (" + a.toString() + ", " + b.toString() + "), not (" +
                  f.parameterTypes[0].toString() + ", " + f.parameterTypes[1].toString() + ")");
        }
        return SemanticType::listOf(*f.returnType);
    }
    SemanticType element = a == b ? a : SemanticType::unionOf({a, b});
    return SemanticType::listOf(SemanticType::listOf(element));
}

SemanticType Analyser::analyseCall(const CallExpr* expression) {
    const Expr* callee = expression->callee.get();
    std::vector<SemanticType> argTypes;

    // f(...) -- a named function, builtin, method (inside a class) or a
    // variable holding a lambda.
    if (callee->kind == ExprKind::Identifier &&
        static_cast<const IdentifierExpr*>(callee)->name.type == TokenType::IDENTIFIER) {
        const auto* id = static_cast<const IdentifierExpr*>(callee);
        const Symbol* symbol = resolveSymbol(id->name.lexeme);
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->cppName == "dessa::zip") {
            return analyseZip(expression, *symbol);
        }
        if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method ||
                       (symbol->kind == SymbolKind::Builtin && symbol->type.kind == SemanticTypeKind::Function))) {
            identSymbols[callee] = *symbol;
            exprTypes[callee] = symbol->type;
            if (hoverHits(id->name)) recordHover(id->name, *symbol, symbol->type);
            argOrders[expression] = checkArguments(symbol->name, symbol->parameters, expression->args, expression,
                                                   symbol->variadic, &argTypes);
            return callResult(*symbol, argTypes);
        }
        if (symbol && symbol->kind == SymbolKind::Class) {
            identSymbols[callee] = *symbol;
            error(expression, "Use 'new " + id->name.lexeme + "(...)' to create an instance of a class");
            for (const auto& a : expression->args) analyseExpression(a.value.get());
            return SemanticType::unknown();
        }
    }

    // m.sqrt(2) with $("math" as m): like calling sqrt
    if (callee->kind == ExprKind::Member) {
        if (const Symbol* target = aliasMember(static_cast<const MemberExpr*>(callee), true)) {
            if (target->name.empty()) {
                for (const auto& a : expression->args) analyseExpression(a.value.get());
                return SemanticType::unknown();
            }
            identSymbols[callee] = *target;
            aliasRefs.insert(callee);
            exprTypes[callee] = target->type;
            if (target->kind == SymbolKind::Builtin && target->cppName == "dessa::zip") return analyseZip(expression, *target);
            if (target->kind == SymbolKind::Function || (target->kind == SymbolKind::Builtin && target->type.kind == SemanticTypeKind::Function)) {
                argOrders[expression] = checkArguments(target->name, target->parameters, expression->args, expression,
                                                       target->variadic, &argTypes);
                return callResult(*target, argTypes);
            }
            if (target->kind == SymbolKind::Class) {
                const auto* m = static_cast<const MemberExpr*>(callee);
                error(expression, "Use 'new " + static_cast<const IdentifierExpr*>(m->object.get())->name.lexeme + "." + m->name.lexeme +
                                      "(...)' to create an instance of a class");
                for (const auto& a : expression->args) analyseExpression(a.value.get());
                return SemanticType::unknown();
            }
        }
    }

    // obj.method(...) / obj?.method(...) / super.method(...) / super.init(...)
    if (callee->kind == ExprKind::Member) {
        const auto* member = static_cast<const MemberExpr*>(callee);
        SemanticType memberType = analyseExpression(callee);
        const Symbol* symbol = memberOf(callee);
        if (!symbol) {
            if (dynamicMembers.count(callee)) {
                error(expression, "Cannot call '" + member->name.lexeme + "' on an Obj; check its type with is first, e.g. if (x is Dog) x." +
                                      member->name.lexeme + "()");
            }
            for (const auto& a : expression->args) analyseExpression(a.value.get());
            return SemanticType::unknown();
        }
        if (symbol->kind == SymbolKind::Method) {
            // Mutating a captured local from inside a lambda would only change
            // the lambda's copy, so it's rejected like an assignment would be.
            static const std::unordered_set<std::string> mutating = {
                "push", "pop", "insert", "removeAt", "remove", "add", "clear", "reverse", "sort", "set"};
            const Expr* receiver = member->object.get();
            if (!symbol->cppName.empty() && mutating.count(member->name.lexeme) && receiver->kind == ExprKind::Identifier) {
                const std::string& name = static_cast<const IdentifierExpr*>(receiver)->name.lexeme;
                const Symbol* target = resolveSymbol(name);
                // A narrowed Obj gives a copy of its value
                if (target && target->type.is(SemanticTypeKind::Obj)) {
                    const SemanticType* now = typeOf(receiver);
                    error(expression, "'" + name + "' is an Obj: its value can't be changed in place; copy it first, e.g. var[" +
                                          (now ? now->toString() : std::string("Int<>")) + "] items = " + name + ";");
                }
                if (target && !target->isGlobal && target->kind != SymbolKind::Field) {
                    for (Scope* s = currentScope; s && !s->resolveLocal(name); s = s->parent()) {
                        if (s->kind() == ScopeKind::Lambda) {
                            error(expression, "A lambda cannot modify '" + name + "' from the enclosing function (it is captured by value)");
                            break;
                        }
                    }
                }
            }
            // list.sort((a, b) := ...) -- the comparator form: negative, 0 or positive like JS / Java
            bool sortWithComparator = !symbol->cppName.empty() && member->name.lexeme == "sort" && !expression->args.empty();
            if (sortWithComparator) {
                const SemanticType* receiver = typeOf(member->object.get());
                SemanticType element = receiver && receiver->elementType ? *receiver->elementType : SemanticType::unknown();
                Symbol& stored = memberSymbols[callee];
                stored.parameters = {{"compare", SemanticType::functionOf({element, element}, simple(SemanticTypeKind::Int))}};
                symbol = &stored;
            }
            argOrders[expression] = checkArguments(member->name.lexeme, symbol->parameters, expression->args, expression, false, &argTypes);
            SemanticType result = symbol->returnType;
            if (!symbol->cppName.empty() && member->name.lexeme == "map" && !argTypes.empty() &&
                argTypes[0].kind == SemanticTypeKind::Function && argTypes[0].returnType) {
                if (argTypes[0].returnType->isVoidLike()) {
                    error(expression, "The function passed to map must return a value");
                }
                result = SemanticType::listOf(*argTypes[0].returnType);
            }
            if (!symbol->cppName.empty() && member->name.lexeme == "sort" && !sortWithComparator) {
                const SemanticType* receiver = typeOf(member->object.get());
                const SemanticType* element = receiver && receiver->elementType ? receiver->elementType.get() : nullptr;
                if (element && !element->isUnknown() && !element->isNumeric() &&
                    !element->is(SemanticTypeKind::Str) && !element->is(SemanticTypeKind::Chr)) {
                    error(expression, "sort() needs a list of numbers, Str or Chr, not " + receiver->toString() +
                          "; pass a comparator for other values, e.g. sort((a[T], b[T]) := ...)");
                }
            }
            bool chained = isOptionalChain(callee);
            if (chained && !result.isVoidLike()) result = withNil(result);
            return result;
        }
        // A field holding a lambda
        if (memberType.kind == SemanticTypeKind::Function || (memberType.containsNil() && withoutNil(memberType).kind == SemanticTypeKind::Function)) {
            SemanticType fn = withoutNil(memberType);
            std::vector<FunctionParameterSymbol> params;
            for (size_t i = 0; i < fn.parameterTypes.size(); ++i) params.push_back({"#" + std::to_string(i + 1), fn.parameterTypes[i]});
            argOrders[expression] = checkArguments(member->name.lexeme, params, expression->args, expression, false, &argTypes);
            return fn.returnType ? *fn.returnType : SemanticType::unknown();
        }
        if (!memberType.isUnknown()) error(expression, "'" + member->name.lexeme + "' is not callable");
        for (const auto& a : expression->args) analyseExpression(a.value.get());
        return SemanticType::unknown();
    }

    // Any other expression evaluating to a function (a lambda variable, ...)
    SemanticType calleeType = analyseExpression(callee);
    if (calleeType.isUnknown()) {
        for (const auto& a : expression->args) analyseExpression(a.value.get());
        return SemanticType::unknown();
    }
    if (calleeType.kind != SemanticTypeKind::Function) {
        error(expression, "'" + calleeName(callee) + "' is not callable (it is " + calleeType.toString() + ")");
        for (const auto& a : expression->args) analyseExpression(a.value.get());
        return SemanticType::unknown();
    }
    std::vector<FunctionParameterSymbol> params;
    for (size_t i = 0; i < calleeType.parameterTypes.size(); ++i) params.push_back({"#" + std::to_string(i + 1), calleeType.parameterTypes[i]});
    for (const auto& a : expression->args) {
        if (a.name) error(a.value.get(), "Named arguments can't be used when calling a lambda");
    }
    argOrders[expression] = checkArguments(calleeName(callee), params, expression->args, expression, false, &argTypes);
    return calleeType.returnType ? *calleeType.returnType : SemanticType::unknown();
}

SemanticType Analyser::analyseNew(const NewExpr* expression) {
    if (expression->isAnonymous) {
        std::unordered_set<std::string> seen;
        for (const auto& field : expression->args) {
            SemanticType t = analyseExpression(field.value.get());
            if (t.isVoidLike()) error(field.value.get(), "Field '" + field.name.value_or("") + "' has no value (Emp)");
            if (field.name && !seen.insert(*field.name).second) error(field.value.get(), "Duplicate field '" + *field.name + "'");
            expectedTypes[field.value.get()] = simple(SemanticTypeKind::Obj);
        }
        return simple(SemanticTypeKind::Obj);
    }
    const std::string& className = expression->className.lexeme;
    const Symbol* symbol = className.find('.') != std::string::npos
        ? resolveQualified(className, expression->line, expression->column) : resolveSymbol(className);
    if (!symbol || symbol->kind != SymbolKind::Class) {
        if (symbol && symbol->kind == SymbolKind::Interface) {
            error(expression, "Cannot create an instance of interface '" + className + "'; create a class that implements it");
        } else if (symbol && symbol->kind == SymbolKind::Enum) {
            error(expression, "Cannot create an instance of enum '" + className + "'; use " + className + ".VALUE");
        } else {
            error(expression, "Unknown class '" + className + "'");
        }
        for (const auto& arg : expression->args) analyseExpression(arg.value.get());
        return SemanticType::unknown();
    }

    const ClassInfo* info = findClass(symbol->type);
    const ClassInfo* withCtor = info;
    while (withCtor && !withCtor->constructor) withCtor = withCtor->parent ? findClass(*withCtor->parent) : nullptr;

    if (hoverHits(expression->className)) {
        // new Dog(|  -- the constructor's signature (no init: no parameters)
        Symbol ctor = withCtor ? *withCtor->constructor : *symbol;
        ctor.kind = SymbolKind::Method;
        ctor.name = "init";
        ctor.returnType = symbol->type;
        recordHover(expression->className, ctor, symbol->type, className);
    }

    if (withCtor) {
        argOrders[expression] = checkArguments(className, withCtor->constructor->parameters, expression->args, expression);
    } else if (!expression->args.empty()) {
        error(expression, "Class '" + className + "' has no init(...) and takes no arguments");
        for (const auto& arg : expression->args) analyseExpression(arg.value.get());
    }
    return symbol->type;
}

// ============================================================
// Lambdas
// ============================================================

SemanticType Analyser::analyseLambda(const LambdaExpr* expression, std::optional<SemanticType> declaredReturn) {
    std::vector<SemanticType> parameters;
    for (const auto& p : expression->params) {
        if (p.isVariadic || p.defaultValue) {
            error(p.line, p.column, "Lambda parameters can't be variadic or have default values; use a named function (func) instead");
        }
        parameters.push_back(resolveType(p.type, expression->line, expression->column));
    }

    const auto previousFunction = currentFunctionName;
    const auto previousReturn = currentReturnType;
    const bool previousInferring = inferringReturn;
    const auto previousInferred = inferredReturn;
    const int previousLoopDepth = loopDepth;
    const bool previousInConstructor = inConstructor;
    loopDepth = 0;
    inConstructor = false;
    functionDepth++;
    currentFunctionName = "<lambda>";

    beginScope(ScopeKind::Lambda);
    for (size_t i = 0; i < expression->params.size(); ++i) {
        Symbol parameter;
        parameter.kind = SymbolKind::Parameter;
        parameter.name = expression->params[i].name;
        parameter.type = parameters[i];
        parameter.moduleId = currentModule;
        declareSymbol(parameter, expression->line, expression->column);
    }

    SemanticType returnType = SemanticType::simple(SemanticTypeKind::Emp);
    if (expression->body && expression->body->kind == StmtKind::Expression) {
        const Expr* body = static_cast<const ExpressionStmt*>(expression->body.get())->expression.get();
        SemanticType bodyType = analyseExpression(body);
        if (declaredReturn && !declaredReturn->isVoidLike()) {
            checkConversion(*declaredReturn, body, bodyType, "as the lambda's result");
            returnType = *declaredReturn;
        } else if (declaredReturn) {
            returnType = *declaredReturn;
        } else {
            returnType = bodyType;
        }
    } else if (expression->body) {
        const auto* block = static_cast<const BlockStmt*>(expression->body.get());
        if (declaredReturn) {
            currentReturnType = *declaredReturn;
            inferringReturn = false;
            for (const auto& s : block->statements) analyseStatement(s.get());
            if (!declaredReturn->isVoidLike() && !declaredReturn->isUnknown() && !blockDefinitelyExits(block, false)) {
                error(expression, "Lambda may reach the end without returning " + declaredReturn->toString());
            }
            returnType = *declaredReturn;
        } else {
            currentReturnType = SemanticType::unknown();
            inferringReturn = true;
            inferredReturn.reset();
            for (const auto& s : block->statements) analyseStatement(s.get());
            returnType = inferredReturn.value_or(SemanticType::simple(SemanticTypeKind::Emp));
        }
    }

    endScope();
    functionDepth--;
    currentFunctionName = previousFunction;
    currentReturnType = previousReturn;
    inferringReturn = previousInferring;
    inferredReturn = previousInferred;
    loopDepth = previousLoopDepth;
    inConstructor = previousInConstructor;

    SemanticType result = SemanticType::functionOf(std::move(parameters), returnType);
    exprTypes[expression] = result;
    return result;
}

} // namespace dessa
