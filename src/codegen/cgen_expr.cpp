// The C backend: expressions. See cgen.hpp for how values are counted.
#include "cgen.hpp"
#include "cglue.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace dessa {

namespace {

const SemanticType kUnknown = SemanticType::unknown();

SemanticType simpleType(SemanticTypeKind k) { return SemanticType::simple(k); }

bool isNilLiteral(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::NIL;
}

const Expr* ungroup(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e;
}

bool isSuperExpr(const Expr* e) {
    return e && e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type == TokenType::SUPER;
}

bool isThisExpr(const Expr* e) {
    return e && e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type == TokenType::THIS;
}

bool hasSideEffects(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case ExprKind::Call:
        case ExprKind::Assign:
        case ExprKind::Postfix:
        case ExprKind::New:
            return true;
        case ExprKind::Unary: return hasSideEffects(static_cast<const UnaryExpr*>(e)->operand.get());
        case ExprKind::Binary:
            return hasSideEffects(static_cast<const BinaryExpr*>(e)->left.get()) || hasSideEffects(static_cast<const BinaryExpr*>(e)->right.get());
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            return hasSideEffects(t->condition.get()) || hasSideEffects(t->thenBranch.get()) || hasSideEffects(t->elseBranch.get());
        }
        case ExprKind::Index:
            return hasSideEffects(static_cast<const IndexExpr*>(e)->object.get()) || hasSideEffects(static_cast<const IndexExpr*>(e)->index.get());
        case ExprKind::Member: return hasSideEffects(static_cast<const MemberExpr*>(e)->object.get());
        case ExprKind::Grouping: return hasSideEffects(static_cast<const GroupingExpr*>(e)->inner.get());
        case ExprKind::Is: return hasSideEffects(static_cast<const IsExpr*>(e)->value.get());
        case ExprKind::Convert: return hasSideEffects(static_cast<const ConvertExpr*>(e)->value.get());
        case ExprKind::TemplateString:
            for (const auto& i : static_cast<const TemplateStringExpr*>(e)->interpolations) if (hasSideEffects(i.get())) return true;
            return false;
        case ExprKind::List:
            for (const auto& el : static_cast<const ListExpr*>(e)->elements) if (hasSideEffects(el.value.get())) return true;
            return false;
        case ExprKind::Map:
            for (const auto& entry : static_cast<const MapExpr*>(e)->entries) {
                if (hasSideEffects(entry.key.get()) || hasSideEffects(entry.value.get())) return true;
            }
            return false;
        default: return false;
    }
}

bool isNumber(const SemanticType& t) {
    return t.is(SemanticTypeKind::Int) || t.is(SemanticTypeKind::Dbl) || t.is(SemanticTypeKind::Flt);
}

} // namespace

// A name for a variable inside a ({ ... }) block
static std::string blockVar(int& counter) {
    return "ds_b" + std::to_string(counter++);
}

// ============================================================
// Counting helpers
// ============================================================

std::string CCodegen::use(const CV& v, const SemanticType& type) {
    if (!v.owned || !isCounted(type)) return v.code;
    if (temps.empty()) unsupported("a value outside a statement");
    std::string t = temp();
    temps.back().push_back({t, cType(type)});
    return "(" + t + " = " + v.code + ")";
}

std::string CCodegen::own(const CV& v, const SemanticType& type) {
    if (!isCounted(type) || v.owned) return v.code;
    return "((" + cType(type) + ")ds_retain(" + v.code + "))";
}

std::string CCodegen::discard(const CV& v, const SemanticType& type) {
    if (v.owned && isCounted(type)) return "ds_release(" + v.code + ")";
    return "(void)(" + v.code + ")";
}

// ============================================================
// Conversions between types
// ============================================================

CCodegen::CV CCodegen::convert(CV v, const SemanticType& from, const SemanticType& to) {
    if (to.isUnknown() || to.isVoidLike() || from.isUnknown()) return v;
    if (from == to) return v;
    if (from.is(SemanticTypeKind::Nil)) {
        if (isOptValue(to)) return {"(" + cType(to) + "){0}", false};
        return {"NULL", false};
    }
    if (isOptValue(to)) {
        const SemanticType base = withoutNil(to);
        if (isOptValue(from)) {
            if (cType(from) == cType(to)) return v;
            unsupported("converting " + from.toString() + " to " + to.toString());
        }
        CV inner = convert(v, from, base);
        return {"(" + cType(to) + "){true, " + inner.code + "}", false};
    }
    if (isCounted(to)) {
        if (!isCounted(from)) unsupported("converting " + from.toString() + " to " + to.toString());
        const std::string ct = cType(to);
        if (cType(from) == ct) return v;
        return {"((" + ct + ")(" + v.code + "))", v.owned};
    }
    // numbers, Chr, Bul, enums
    const SemanticType f = withoutNil(from);
    if (isOptValue(from)) return {"((" + cType(to) + ")DS_UNWRAP(" + v.code + "))", false};
    if (cType(f) == cType(to)) return v;
    return {"((" + cType(to) + ")(" + v.code + "))", false};
}

CCodegen::CV CCodegen::exprAs(const Expr* e, const SemanticType& target) {
    if (!e) return {"", false};
    const Expr* inner = ungroup(e);
    if (target.isUnknown() || target.isVoidLike()) return expr(e);
    if (isNilLiteral(inner)) return {isOptValue(target) ? "(" + cType(target) + "){0}" : "NULL", false};
    const SemanticType base = withoutNil(target);
    if (inner->kind == ExprKind::List && base.is(SemanticTypeKind::List)) {
        return listLiteral(static_cast<const ListExpr*>(inner)->elements, base);
    }
    if (inner->kind == ExprKind::Set && base.is(SemanticTypeKind::Map) && static_cast<const SetExpr*>(inner)->elements.empty()) {
        return {defaultValue(base), true};
    }
    if (inner->kind == ExprKind::Set && base.is(SemanticTypeKind::Set)) {
        return setLiteral(static_cast<const SetExpr*>(inner)->elements, base);
    }
    if (inner->kind == ExprKind::Map && base.is(SemanticTypeKind::Map)) {
        return mapLiteral(static_cast<const MapExpr*>(inner), base);
    }
    if (inner->kind == ExprKind::Ternary) {
        const auto* t = static_cast<const TernaryExpr*>(inner);
        const SemanticType bul = simpleType(SemanticTypeKind::Bul);
        std::string c = use(exprAs(t->condition.get(), bul), bul);
        CV a = exprAs(t->thenBranch.get(), target);
        CV b = exprAs(t->elseBranch.get(), target);
        if (isCounted(target) && (a.owned || b.owned)) {
            return {"(" + c + " ? " + own(a, target) + " : " + own(b, target) + ")", true};
        }
        return {"(" + c + " ? " + a.code + " : " + b.code + ")", false};
    }
    const SemanticType* source = analyser.typeOf(e);
    CV v = expr(e);
    return convert(v, source ? *source : kUnknown, target);
}

// ============================================================
// Expressions
// ============================================================

CCodegen::CV CCodegen::expr(const Expr* e) {
    if (!e) return {"", false};
    const SemanticType& type = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    switch (e->kind) {
        case ExprKind::Literal: return literalExpr(static_cast<const LiteralExpr*>(e));
        case ExprKind::TemplateString: return templateString(static_cast<const TemplateStringExpr*>(e));
        case ExprKind::Identifier: return identifier(static_cast<const IdentifierExpr*>(e));
        case ExprKind::Unary: {
            const auto* u = static_cast<const UnaryExpr*>(e);
            const SemanticType& ot = analyser.typeOf(u->operand.get()) ? *analyser.typeOf(u->operand.get()) : kUnknown;
            std::string operand = use(expr(u->operand.get()), ot);
            if (u->op.type == TokenType::MINUS && ot.isInteger()) return {"ds_neg(" + operand + ")", false};
            if (u->op.type == TokenType::NOT) return {"(!" + operand + ")", false};
            if (u->op.type == TokenType::BITWISE_NOT) return {"(~" + operand + ")", false};
            if (u->op.type == TokenType::MINUS) return {"(-" + operand + ")", false};
            return {"(" + u->op.lexeme + operand + ")", false};
        }
        case ExprKind::Postfix: {
            const auto* p = static_cast<const PostfixExpr*>(e);
            SemanticType t;
            std::string lv = lvalue(p->operand.get(), &t);
            if (t.isInteger() && !t.containsNil()) {
                return {std::string(p->op.type == TokenType::PLUS_PLUS ? "ds_post_inc(&" : "ds_post_dec(&") + lv + ")", false};
            }
            if (isNumber(t)) return {"(" + lv + p->op.lexeme + ")", false};
            unsupported("++ / -- on " + t.toString());
        }
        case ExprKind::Binary: return binary(static_cast<const BinaryExpr*>(e));
        case ExprKind::Assign: return assign(static_cast<const AssignExpr*>(e));
        case ExprKind::Ternary: return exprAs(e, type);
        case ExprKind::Call: return call(static_cast<const CallExpr*>(e));
        case ExprKind::Index: return index(static_cast<const IndexExpr*>(e));
        case ExprKind::Member: return member(static_cast<const MemberExpr*>(e));
        case ExprKind::Lambda: return lambda(static_cast<const LambdaExpr*>(e));
        case ExprKind::New: return newObject(static_cast<const NewExpr*>(e));
        case ExprKind::List: {
            const SemanticType* expected = analyser.expectedTypeOf(e);
            SemanticType listType = expected && withoutNil(*expected).is(SemanticTypeKind::List) ? withoutNil(*expected) : type;
            return listLiteral(static_cast<const ListExpr*>(e)->elements, listType);
        }
        case ExprKind::Set: {
            const SemanticType* expected = analyser.expectedTypeOf(e);
            SemanticType setType = expected && withoutNil(*expected).is(SemanticTypeKind::Set) ? withoutNil(*expected) : type;
            if (setType.is(SemanticTypeKind::Map)) return {defaultValue(setType), true};
            return setLiteral(static_cast<const SetExpr*>(e)->elements, setType);
        }
        case ExprKind::Grouping: {
            CV inner = expr(static_cast<const GroupingExpr*>(e)->inner.get());
            return {"(" + inner.code + ")", inner.owned};
        }
        case ExprKind::Map: {
            const SemanticType* expected = analyser.expectedTypeOf(e);
            SemanticType mapType = expected && withoutNil(*expected).is(SemanticTypeKind::Map) ? withoutNil(*expected) : type;
            return mapLiteral(static_cast<const MapExpr*>(e), mapType);
        }
        case ExprKind::Convert: return convertExpr(static_cast<const ConvertExpr*>(e));
        case ExprKind::Is: return isExpr(static_cast<const IsExpr*>(e));
    }
    return {"", false};
}

CCodegen::CV CCodegen::literalExpr(const LiteralExpr* e) {
    const std::string& text = e->token.lexeme;
    switch (e->token.type) {
        case TokenType::INT_LITERAL:
        case TokenType::HEX_LITERAL:
        case TokenType::BINARY_LITERAL:
            return {text + "LL", false};
        case TokenType::OCTAL_LITERAL: return {"0" + text.substr(2) + "LL", false};
        case TokenType::FLOAT_LITERAL: {
            std::string digits = text.substr(0, text.size() - 1);
            if (digits.find('.') == std::string::npos && digits.find('e') == std::string::npos) digits += ".0";
            return {digits + "f", false};
        }
        case TokenType::DOUBLE_LITERAL: {
            std::string digits = text.back() == 'd' ? text.substr(0, text.size() - 1) : text;
            if (digits.find('.') == std::string::npos && digits.find('e') == std::string::npos) digits += ".0";
            return {digits, false};
        }
        case TokenType::CHAR_LITERAL: {
            if (text.empty()) return {"((ds_chr)0)", false};
            unsigned char b = static_cast<unsigned char>(text[0]);
            unsigned long cp = b;
            if (b >= 0x80) {
                int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
                cp = extra == 3 ? (b & 0x07) : extra == 2 ? (b & 0x0F) : (b & 0x1F);
                for (int k = 1; k <= extra && k < static_cast<int>(text.size()); ++k) {
                    cp = (cp << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
                }
            }
            return {"((ds_chr)" + std::to_string(cp) + ")", false};
        }
        case TokenType::STRING: return {literal(text), false};
        case TokenType::TRUE: return {"true", false};
        case TokenType::FALSE: return {"false", false};
        case TokenType::NIL: return {"NULL", false};
        default: unsupported("this literal");
    }
}

// The C name of a local variable; a variable from outside the lambda being
// written is captured (copied into the closure when it's made)
std::string CCodegen::resolveLocal(const std::string& name, const SemanticType** type) {
    LambdaInfo* crossed = nullptr;
    for (size_t i = scopes.size(); i-- > 0;) {
        auto& locals = scopes[i].locals;
        for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
            if (it->name != name) continue;
            if (type) *type = &it->type;
            if (!crossed) return it->cName;
            for (const auto& c : crossed->captures) {
                if (c.name == name) return "ds_env->" + c.field;
            }
            Capture c{name, "c_" + (name == "this" ? std::string("this") : name) + "_" + std::to_string(crossed->captures.size()),
                      it->type, cType(it->type)};
            crossed->captures.push_back(c);
            return "ds_env->" + c.field;
        }
        if (scopes[i].lambda && !crossed) crossed = scopes[i].lambda;
        if (scopes[i].function) break;
    }
    unsupported("the variable '" + name + "' here");
}

CCodegen::CV CCodegen::thisValue() {
    return {resolveLocal("this"), false};
}

CCodegen::CV CCodegen::identifier(const IdentifierExpr* e) {
    if (e->name.type == TokenType::THIS) return thisValue();
    const Symbol* symbol = analyser.symbolOf(e);
    if (!symbol) unsupported("the name '" + e->name.lexeme + "'");
    if (analyser.isRecursiveRef(e)) {
        // inside its own lambda (not a lambda within it): the closure itself
        for (size_t i = scopes.size(); i-- > 0;) {
            if (!scopes[i].lambda) continue;
            if (scopes[i].lambda->selfName == e->name.lexeme) return {"ds_self", false};
            break;
        }
        unsupported("a lambda that calls itself from a lambda inside it");
    }

    std::string name;
    switch (symbol->kind) {
        case SymbolKind::Field:
            name = thisValue().code + "->" + mangle(symbol->name);
            break;
        case SymbolKind::Method: unsupported("a method used as a value");
        case SymbolKind::Builtin:
            if (symbol->cppName == "dessa::math::PI") return {"3.14159265358979323846", false};
            if (symbol->cppName == "dessa::math::E") return {"2.71828182845904523536", false};
            if (std::string c = glueConstant(symbol->cppName); !c.empty()) return {c, isCounted(symbol->type)};
            return builtinValue(*symbol);
        case SymbolKind::Function: return functionValue(*symbol);
        case SymbolKind::Class:
        case SymbolKind::Interface:
        case SymbolKind::Enum:
        case SymbolKind::Module:
            unsupported("a type used as a value");
        default:
            name = symbol->isGlobal ? globalName(*symbol) : resolveLocal(symbol->name);
    }

    // Narrowed by `!= nil`, `is` or an assignment
    const SemanticType& now = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    const SemanticType& declared = symbol->type;
    if (now.isUnknown() || now == declared) return {name, false};
    if (isOptValue(declared) && now == withoutNil(declared)) return {"DS_UNWRAP(" + name + ")", false};
    if (declared.is(SemanticTypeKind::Obj)) unsupported("Obj");
    if (now.isReference()) {
        if (withoutNil(declared) == now) return {name, false};
        if (now.isInterface()) return {"((ds_head*)(" + name + "))", false};
        return {"((" + cType(now) + ")ds_cast(" + name + ", &" + clsName(now) + ".t))", false};
    }
    if (isCounted(declared) && withoutNil(declared) == now) return {name, false};
    if (withoutNil(declared).kind == SemanticTypeKind::Union) unsupported("union types");
    return {name, false};
}

// ============================================================
// Operators
// ============================================================

CCodegen::CV CCodegen::binary(const BinaryExpr* e) {
    const Expr* l = e->left.get();
    const Expr* r = e->right.get();
    const SemanticType& lt = analyser.typeOf(l) ? *analyser.typeOf(l) : kUnknown;
    const SemanticType& rt = analyser.typeOf(r) ? *analyser.typeOf(r) : kUnknown;
    const SemanticType& result = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;

    if (const auto* op = analyser.operatorCallOf(e)) {
        const ClassInfo* info = classOf(lt);
        if (!info) unsupported("an operator method on " + lt.toString());
        std::string object = "ds_ref(" + use(expr(l), lt) + ")";
        std::string arg = use(exprAs(r, op->method.parameters[0].type), op->method.parameters[0].type);
        CV called = classMethodCall(info, nullptr, object, op->method.name, op->method, ", " + arg, false);
        std::string c = use(called, op->method.returnType);
        if (op->viaCmp) {
            const char* test = op->op == TokenType::L_ANGLE ? " < 0" : op->op == TokenType::R_ANGLE ? " > 0"
                             : op->op == TokenType::LESS_EQUAL ? " <= 0" : " >= 0";
            return {"(" + c + test + ")", false};
        }
        if (op->op == TokenType::BANG_EQUAL) return {"(!" + c + ")", false};
        return {c, called.owned && isCounted(op->method.returnType) ? false : false};
    }

    switch (e->op.type) {
        case TokenType::EQUAL_EQUAL: return equality(l, r, false);
        case TokenType::BANG_EQUAL: return equality(l, r, true);
        case TokenType::L_ANGLE:
        case TokenType::R_ANGLE:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER_EQUAL:
            return comparison(e);
        case TokenType::AND_AND:
        case TokenType::OR_OR: {
            const SemanticType bul = simpleType(SemanticTypeKind::Bul);
            std::string a = use(exprAs(l, bul), bul);
            std::string b = use(exprAs(r, bul), bul);
            return {"(" + a + (e->op.type == TokenType::AND_AND ? " && " : " || ") + b + ")", false};
        }
        case TokenType::QUESTION_QUESTION: {
            std::string v = blockVar(tempCounter);
            if (isOptValue(lt)) {
                std::string a = use(expr(l), lt);
                if (isOptValue(result)) {
                    CV b = exprAs(r, result);
                    return {"({ " + cType(lt) + " " + v + " = " + a + "; " + v + ".has ? " + v + " : " + b.code + "; })", false};
                }
                CV b = exprAs(r, result);
                return {"({ " + cType(lt) + " " + v + " = " + a + "; " + v + ".has ? (" + cType(result) + ")" + v + ".value : " +
                            b.code + "; })", false};
            }
            if (isCounted(lt)) {
                std::string a = use(expr(l), lt);
                CV b = exprAs(r, result);
                if (!isCounted(result)) unsupported("?? here");
                return {"({ " + cType(lt) + " " + v + " = " + a + "; " + v + " ? (" + cType(result) + ")ds_retain(" + v + ") : " +
                            own(b, result) + "; })", true};
            }
            return exprAs(l, result);
        }
        case TokenType::QUESTION_BANG: {
            std::string v = blockVar(tempCounter);
            std::string a = use(expr(l), lt);
            std::string error = own(expr(r), rt);
            std::string raise = rt.is(SemanticTypeKind::Str) ? "ds_throw_str(" + error + ")" : "ds_throw_obj(" + error + ")";
            if (isOptValue(lt)) {
                return {"({ " + cType(lt) + " " + v + " = " + a + "; if (!" + v + ".has) " + raise + "; " + v + ".value; })", false};
            }
            if (isCounted(lt)) {
                return {"({ " + cType(lt) + " " + v + " = " + a + "; if (!" + v + ") " + raise + "; " + v + "; })", false};
            }
            return {a, false};
        }
        default: break;
    }

    const bool ints = lt.isInteger() && rt.isInteger() && !lt.containsNil() && !rt.containsNil();
    if (e->op.type == TokenType::PLUS && result.is(SemanticTypeKind::Str)) return textConcat(e);
    std::string a = use(expr(l), lt);
    std::string b = use(expr(r), rt);
    if (isOptValue(lt)) a = "DS_UNWRAP(" + a + ")";
    if (isOptValue(rt)) b = "DS_UNWRAP(" + b + ")";
    const std::string rc = cType(result);
    switch (e->op.type) {
        case TokenType::PLUS: return {ints ? "ds_add(" + a + ", " + b + ")" : "(" + a + " + " + b + ")", false};
        case TokenType::MINUS: return {ints ? "ds_sub(" + a + ", " + b + ")" : "(" + a + " - " + b + ")", false};
        case TokenType::MULT: return {ints ? "ds_mul(" + a + ", " + b + ")" : "(" + a + " * " + b + ")", false};
        case TokenType::DIV:
            if (ints) return {"ds_div(" + a + ", " + b + ")", false};
            return {"(" + a + " / " + b + ")", false};
        case TokenType::MOD:
            if (ints) return {"ds_mod(" + a + ", " + b + ")", false};
            if (rc == "ds_flt") return {"ds_fmodf(" + a + ", " + b + ")", false};
            return {"((" + rc + ")ds_fmod(" + a + ", " + b + "))", false};
        case TokenType::EXPONENT:
            if (ints) return {"ds_pow(" + a + ", " + b + ")", false};
            return {"((" + rc + ")pow((double)(" + a + "), (double)(" + b + ")))", false};
        case TokenType::BITWISE_AND: return {"(" + a + " & " + b + ")", false};
        case TokenType::BITWISE_OR: return {"(" + a + " | " + b + ")", false};
        case TokenType::BITWISE_XOR: return {"(" + a + " ^ " + b + ")", false};
        case TokenType::LEFT_SHIFT: return {"(" + a + " << " + b + ")", false};
        case TokenType::RIGHT_SHIFT: return {"(" + a + " >> " + b + ")", false};
        default: unsupported("the operator " + e->op.lexeme);
    }
}

// a + b + c on text: one builder
CCodegen::CV CCodegen::textConcat(const BinaryExpr* e) {
    std::vector<const Expr*> parts;
    std::function<void(const Expr*)> collect = [&](const Expr* x) {
        const Expr* in = ungroup(x);
        if (in->kind == ExprKind::Binary) {
            const auto* b = static_cast<const BinaryExpr*>(in);
            const SemanticType* t = analyser.typeOf(in);
            if (b->op.type == TokenType::PLUS && t && t->is(SemanticTypeKind::Str) && !analyser.operatorCallOf(in)) {
                collect(b->left.get());
                collect(b->right.get());
                return;
            }
        }
        parts.push_back(x);
    };
    collect(e);
    std::string b = blockVar(tempCounter);
    std::string code = "({ ds_sb " + b + "; ds_sb_init(&" + b + "); ";
    for (const Expr* p : parts) appendText(code, b, p);
    code += "ds_sb_done(&" + b + "); })";
    return {code, true};
}

CCodegen::CV CCodegen::comparison(const BinaryExpr* e) {
    const SemanticType& lt = analyser.typeOf(e->left.get()) ? *analyser.typeOf(e->left.get()) : kUnknown;
    const SemanticType& rt = analyser.typeOf(e->right.get()) ? *analyser.typeOf(e->right.get()) : kUnknown;
    std::string a = use(expr(e->left.get()), lt);
    std::string b = use(expr(e->right.get()), rt);
    if (isOptValue(lt)) a = "DS_UNWRAP(" + a + ")";
    if (isOptValue(rt)) b = "DS_UNWRAP(" + b + ")";
    const std::string op = e->op.type == TokenType::L_ANGLE ? " < " : e->op.type == TokenType::R_ANGLE ? " > "
                         : e->op.type == TokenType::LESS_EQUAL ? " <= " : " >= ";
    if (withoutNil(lt).is(SemanticTypeKind::Str)) return {"(ds_str_cmp(" + a + ", " + b + ")" + op + "0)", false};
    if (isCounted(lt)) unsupported("comparing " + lt.toString());
    return {"(" + a + op + b + ")", false};
}

CCodegen::CV CCodegen::equality(const Expr* l, const Expr* r, bool negate) {
    const std::string no = negate ? "!" : "";
    if (isNilLiteral(l) || isNilLiteral(r)) {
        const Expr* other = isNilLiteral(l) ? r : l;
        const Expr* in = ungroup(other);
        const SemanticType* ot = analyser.typeOf(other);
        SemanticType type = ot ? *ot : kUnknown;
        std::string value;
        // The variable itself, not its narrowed value
        if (in->kind == ExprKind::Identifier) {
            const Symbol* s = analyser.symbolOf(in);
            if (s && s->type.containsNil() && (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter ||
                                               s->kind == SymbolKind::Constant || s->kind == SymbolKind::Field)) {
                type = s->type;
                value = s->kind == SymbolKind::Field ? thisValue().code + "->" + mangle(s->name)
                      : s->isGlobal ? globalName(*s) : resolveLocal(s->name);
            }
        }
        if (value.empty()) value = use(expr(other), type);
        if (isOptValue(type)) return {"(" + std::string(negate ? "" : "!") + "(" + value + ").has)", false};
        if (isCounted(type)) return {"(" + value + (negate ? " != NULL)" : " == NULL)"), false};
        if (type.is(SemanticTypeKind::Nil)) return {negate ? "false" : "true", false};
        return {negate ? "true" : "false", false};   // a plain value is never nil
    }
    const SemanticType& lt = analyser.typeOf(l) ? *analyser.typeOf(l) : kUnknown;
    const SemanticType& rt = analyser.typeOf(r) ? *analyser.typeOf(r) : kUnknown;
    std::string a = use(expr(l), lt);
    std::string b = use(expr(r), rt);
    const SemanticType lb = withoutNil(lt), rb = withoutNil(rt);
    if (lb.kind == SemanticTypeKind::Union || rb.kind == SemanticTypeKind::Union) unsupported("union types");
    if (lb.is(SemanticTypeKind::Obj) || rb.is(SemanticTypeKind::Obj)) unsupported("Obj");

    // The plain test on two present values
    auto test = [&](const std::string& x, const std::string& y) -> std::string {
        if (lb.is(SemanticTypeKind::Str)) return "ds_str_eq(" + x + ", " + y + ")";
        if (lb.is(SemanticTypeKind::List) || lb.is(SemanticTypeKind::Set)) return "ds_list_eq(" + x + ", " + y + ")";
        if (lb.is(SemanticTypeKind::Map)) return "ds_map_eq(" + x + ", " + y + ")";
        if (isCounted(lb)) return "((void*)(" + x + ") == (void*)(" + y + "))";
        return "(" + x + " == " + y + ")";
    };
    const bool lo = isOptValue(lt), ro = isOptValue(rt);
    const bool ln = isCounted(lt) && lt.containsNil(), rn = isCounted(rt) && rt.containsNil();
    if (!lo && !ro && !ln && !rn) return {"(" + no + test(a, b) + ")", false};
    std::string x = blockVar(tempCounter), y = blockVar(tempCounter);
    std::string decl = "({ " + cType(lt) + " " + x + " = " + a + "; " + cType(rt) + " " + y + " = " + b + "; ";
    std::string body;
    if (lo && ro) body = "(" + x + ".has == " + y + ".has && (!" + x + ".has || " + x + ".value == " + y + ".value))";
    else if (lo) body = "(" + x + ".has && " + x + ".value == " + y + ")";
    else if (ro) body = "(" + y + ".has && " + x + " == " + y + ".value)";
    else if (ln && rn) {
        body = isCounted(lb) && (lb.is(SemanticTypeKind::Str) || lb.is(SemanticTypeKind::List) || lb.is(SemanticTypeKind::Map))
             ? "(" + x + " == " + y + " || (" + x + " && " + y + " && " + test(x, y) + "))"
             : "((void*)" + x + " == (void*)" + y + ")";
    } else if (ln) {
        body = "(" + x + " && " + test(x, y) + ")";
    } else {
        body = "(" + y + " && " + test(x, y) + ")";
    }
    return {decl + no + body + "; })", false};
}

// The place an assignment writes to (an lvalue)
std::string CCodegen::lvalue(const Expr* target, SemanticType* type) {
    const Expr* t = ungroup(target);
    const SemanticType* tt = analyser.typeOf(t);
    switch (t->kind) {
        case ExprKind::Identifier: {
            const Symbol* s = analyser.symbolOf(t);
            if (!s) unsupported("assigning this");
            if (type) *type = s->type;
            if (s->kind == SymbolKind::Field) return thisValue().code + "->" + mangle(s->name);
            if (s->isGlobal) return globalName(*s);
            const SemanticType* lt = nullptr;
            std::string name = resolveLocal(s->name, &lt);
            if (name.rfind("ds_env->", 0) == 0) unsupported("changing a captured variable");
            if (volatileLocals) return "(*(" + cType(s->type) + "*)&" + name + ")";
            return name;
        }
        case ExprKind::Member: {
            const auto* m = static_cast<const MemberExpr*>(t);
            const Symbol* s = analyser.memberOf(t);
            if (!s || s->kind != SymbolKind::Field || m->isOptional) unsupported("assigning this member");
            if (analyser.isDynamicMember(t)) unsupported("Obj");
            const SemanticType& ot = analyser.typeOf(m->object.get()) ? *analyser.typeOf(m->object.get()) : kUnknown;
            if (type) *type = s->type;
            if (isThisExpr(m->object.get()) || isSuperExpr(m->object.get())) return thisValue().code + "->" + mangle(m->name.lexeme);
            if (const InterfaceInfo* iface = interfaceOf(ot)) {
                auto it = interfaceIds.find(iface->type().key());
                if (it == interfaceIds.end()) unsupported("this interface");
                std::string o = blockVar(tempCounter);
                return "(*({ ds_head* " + o + " = ds_ref(" + use(expr(m->object.get()), ot) + "); ((const dst_m" + std::to_string(iface->moduleId) +
                       "_" + iface->name + "*)ds_interface(" + o + ", " + std::to_string(it->second) + "))->get_" + m->name.lexeme + "(" + o + "); }))";
            }
            return "((" + cType(withoutNil(ot)) + ")ds_ref(" + use(expr(m->object.get()), ot) + "))->" + mangle(m->name.lexeme);
        }
        case ExprKind::Index: {
            const auto* ix = static_cast<const IndexExpr*>(t);
            SemanticType ot;
            std::string container = lvalue(ix->object.get(), &ot);
            const SemanticType base = withoutNil(ot);
            if (type) *type = tt ? *tt : kUnknown;
            if (base.is(SemanticTypeKind::List)) {
                const SemanticType et = base.elementType ? *base.elementType : kUnknown;
                std::string i = use(expr(ix->index.get()), simpleType(SemanticTypeKind::Int));
                return "(*(" + cType(et) + "*)ds_list_at(ds_list_unique(&" + container + "), " + i + ", sizeof(" + cType(et) + ")))";
            }
            if (base.is(SemanticTypeKind::Map)) {
                const SemanticType kt = base.keyType ? *base.keyType : kUnknown;
                const SemanticType vt = base.elementType ? *base.elementType : kUnknown;
                std::string m = blockVar(tempCounter), k = blockVar(tempCounter), at = blockVar(tempCounter);
                return "(*({ ds_map* " + m + " = ds_map_unique(&" + container + "); " + cType(kt) + " " + k + " = " +
                       use(exprAs(ix->index.get(), kt), kt) + "; int64_t " + at + " = ds_map_find(" + m + ", &" + k + "); if (" + at +
                       " < 0) ds_map_missing(" + m + ", &" + k + "); &DS_MAP_VALUE(" + m + ", " + cType(vt) + ", " + at + "); }))";
            }
            unsupported("assigning into " + ot.toString());
        }
        default: unsupported("assigning this");
    }
}

CCodegen::CV CCodegen::assign(const AssignExpr* e) {
    const Expr* target = ungroup(e->target.get());
    SemanticType tt;
    const SemanticType& targetNow = analyser.typeOf(target) ? *analyser.typeOf(target) : kUnknown;

    if (target->kind == ExprKind::Member && analyser.isDynamicMember(target)) unsupported("Obj");

    // map<k> = v / list<i> = v
    if (e->op.type == TokenType::EQUAL && target->kind == ExprKind::Index) {
        const auto* ix = static_cast<const IndexExpr*>(target);
        SemanticType ot;
        const SemanticType& valueType = targetNow;
        std::string v = blockVar(tempCounter);
        if (const SemanticType* t = analyser.typeOf(ix->object.get()); t && withoutNil(*t).is(SemanticTypeKind::Map)) {
            const SemanticType base = withoutNil(*t);
            const SemanticType kt = base.keyType ? *base.keyType : kUnknown;
            const SemanticType vt = base.elementType ? *base.elementType : kUnknown;
            std::string container = lvalue(ix->object.get(), &ot);
            std::string k = blockVar(tempCounter);
            std::string value = own(exprAs(e->value.get(), vt), vt);
            std::string key = own(exprAs(ix->index.get(), kt), kt);
            return {"({ " + cType(vt) + " " + v + " = " + value + "; " + cType(kt) + " " + k + " = " + key + "; ds_map_put(&" +
                        container + ", &" + k + ", &" + v + "); " + v + "; })", false};
        }
        if (const SemanticType* t = analyser.typeOf(ix->object.get()); t && withoutNil(*t).is(SemanticTypeKind::List)) {
            const SemanticType base = withoutNil(*t);
            const SemanticType et = base.elementType ? *base.elementType : kUnknown;
            std::string value = own(exprAs(e->value.get(), et), et);
            std::string container = lvalue(ix->object.get(), &ot);
            std::string i = use(expr(ix->index.get()), simpleType(SemanticTypeKind::Int));
            std::string p = blockVar(tempCounter);
            std::string code = "({ " + cType(et) + " " + v + " = " + value + "; " + cType(et) + "* " + p + " = (" + cType(et) +
                               "*)ds_list_at(ds_list_unique(&" + container + "), " + i + ", sizeof(" + cType(et) + ")); ";
            if (isCounted(et)) code += cType(et) + " ds_old = *" + p + "; *" + p + " = " + v + "; ds_release(ds_old); ";
            else code += "*" + p + " = " + v + "; ";
            return {code + v + "; })", false};
        }
        (void)valueType;
    }

    std::string lv = lvalue(target, &tt);
    const SemanticType declared = tt;   // what the variable itself holds

    if (e->op.type == TokenType::QUESTION_EQUAL) {
        if (isOptValue(declared)) {
            CV v = exprAs(e->value.get(), declared);
            return {"(" + lv + ".has ? " + lv + " : (" + lv + " = " + v.code + "))", false};
        }
        if (isCounted(declared)) {
            std::string value = own(exprAs(e->value.get(), declared), declared);
            return {"(" + lv + " ? " + lv + " : (" + cType(declared) + ")ds_store(&" + lv + ", " + value + "))", false};
        }
        return {lv, false};
    }
    if (e->op.type == TokenType::EQUAL) {
        if (isCounted(declared)) {
            std::string value = own(exprAs(e->value.get(), declared), declared);
            return {"((" + cType(declared) + ")ds_store(&" + lv + ", " + value + "))", false};
        }
        CV v = exprAs(e->value.get(), declared);
        return {"(" + lv + " = " + v.code + ")", false};
    }

    // Compound: x += v ... (through a pointer: the target is read once)
    const SemanticType now = withoutNil(declared);
    const SemanticType& vt = analyser.typeOf(e->value.get()) ? *analyser.typeOf(e->value.get()) : kUnknown;
    std::string p = blockVar(tempCounter);
    const bool opt = isOptValue(declared);
    const std::string current = opt ? "DS_UNWRAP(*" + p + ")" : "(*" + p + ")";
    auto store = [&](const std::string& value) {
        if (opt) return "({ " + cType(declared) + "* " + p + " = &" + lv + "; " + p + "->value = " + value + "; " + p + "->has = true; " + p + "->value; })";
        return "({ " + cType(declared) + "* " + p + " = &" + lv + "; *" + p + " = " + value + "; })";
    };
    if (const auto* op = analyser.operatorCallOf(e)) {
        const ClassInfo* info = classOf(now);
        if (!info) unsupported("an operator method here");
        std::string arg = use(exprAs(e->value.get(), op->method.parameters[0].type), op->method.parameters[0].type);
        CV called = classMethodCall(info, nullptr, "ds_ref(*" + p + ")", op->method.name, op->method, ", " + arg, false);
        return {"({ " + cType(declared) + "* " + p + " = &" + lv + "; (" + cType(declared) + ")ds_store(" + p + ", " +
                    own(called, op->method.returnType) + "); })", false};
    }
    if (now.is(SemanticTypeKind::Str) && e->op.type == TokenType::PLUS_EQUAL) {
        if (declared.containsNil()) unsupported("+= on a Str | Nil");
        // text += a + b + $"..": every piece is added to the text itself, unless a
        // piece reads the text (then it must be read before anything is added)
        std::vector<const Expr*> pieces;
        std::function<void(const Expr*)> collect = [&](const Expr* x) {
            const Expr* in = ungroup(x);
            if (in->kind == ExprKind::Binary) {
                const auto* b = static_cast<const BinaryExpr*>(in);
                const SemanticType* bt = analyser.typeOf(in);
                if (b->op.type == TokenType::PLUS && bt && bt->is(SemanticTypeKind::Str) && !analyser.operatorCallOf(in)) {
                    collect(b->left.get());
                    collect(b->right.get());
                    return;
                }
            }
            pieces.push_back(x);
        };
        collect(e->value.get());
        std::function<bool(const Expr*)> readsTarget = [&](const Expr* x) -> bool {
            if (!x) return false;
            if (target->kind != ExprKind::Identifier) return true;
            const std::string& n = static_cast<const IdentifierExpr*>(target)->name.lexeme;
            if (x->kind == ExprKind::Identifier) return static_cast<const IdentifierExpr*>(x)->name.lexeme == n;
            switch (x->kind) {
                case ExprKind::Literal: return false;
                case ExprKind::Convert: return readsTarget(static_cast<const ConvertExpr*>(x)->value.get());
                case ExprKind::Grouping: return readsTarget(static_cast<const GroupingExpr*>(x)->inner.get());
                case ExprKind::Binary:
                    return readsTarget(static_cast<const BinaryExpr*>(x)->left.get()) || readsTarget(static_cast<const BinaryExpr*>(x)->right.get());
                case ExprKind::TemplateString:
                    for (const auto& i : static_cast<const TemplateStringExpr*>(x)->interpolations) if (readsTarget(i.get())) return true;
                    return false;
                default: return true;   // calls and the rest might
            }
        };
        if (pieces.size() > 1 && !readsTarget(e->value.get())) {
            std::string code = "({ ds_str** " + p + " = &" + lv + "; ";
            for (const Expr* piece : pieces) {
                const Expr* in = ungroup(piece);
                if (in->kind == ExprKind::Convert && static_cast<const ConvertExpr*>(in)->type.lexeme == "Str") {
                    const SemanticType* it = analyser.typeOf(static_cast<const ConvertExpr*>(in)->value.get());
                    if (it && !it->containsNil() && (it->is(SemanticTypeKind::Int) || it->is(SemanticTypeKind::Str) || it->is(SemanticTypeKind::Chr))) {
                        piece = static_cast<const ConvertExpr*>(in)->value.get();
                    }
                }
                const SemanticType& pt = analyser.typeOf(piece) ? *analyser.typeOf(piece) : kUnknown;
                CV pv = expr(piece);
                if (pt.is(SemanticTypeKind::Str)) code += "ds_str_append(" + p + ", " + use(pv, pt) + "); ";
                else if (pt.is(SemanticTypeKind::Int) && (!pt.radix || *pt.radix == 10)) code += "ds_str_append_int(" + p + ", " + pv.code + "); ";
                else if (pt.is(SemanticTypeKind::Chr)) code += "ds_str_append_chr(" + p + ", " + pv.code + "); ";
                else code += "ds_str_append_take(" + p + ", " + toText(pv, pt) + "); ";
            }
            return {code + "*" + p + "; })", false};
        }
        if (vt.is(SemanticTypeKind::Chr)) {
            std::string c = blockVar(tempCounter);
            return {"({ ds_str** " + p + " = &" + lv + "; ds_str* " + c + " = ds_chr_str(" + use(expr(e->value.get()), vt) +
                        "); ds_str_append(" + p + ", " + c + "); ds_release(" + c + "); *" + p + "; })", false};
        }
        return {"({ ds_str** " + p + " = &" + lv + "; ds_str_append(" + p + ", " + use(expr(e->value.get()), vt) + "); *" + p + "; })", false};
    }
    std::string value = use(expr(e->value.get()), vt);
    if (isOptValue(vt)) value = "DS_UNWRAP(" + value + ")";
    const bool ints = now.isInteger() && vt.isInteger();
    const std::string ct = cType(now);
    switch (e->op.type) {
        case TokenType::PLUS_EQUAL: return {store(ints ? "ds_add(" + current + ", " + value + ")" : "(" + ct + ")(" + current + " + " + value + ")"), false};
        case TokenType::MINUS_EQUAL: return {store(ints ? "ds_sub(" + current + ", " + value + ")" : "(" + ct + ")(" + current + " - " + value + ")"), false};
        case TokenType::MULT_EQUAL: return {store(ints ? "ds_mul(" + current + ", " + value + ")" : "(" + ct + ")(" + current + " * " + value + ")"), false};
        case TokenType::DIV_EQUAL:
            if (now.isInteger()) {
                if (vt.isInteger()) return {store("(ds_int)ds_div(" + current + ", " + value + ")"), false};
                return {store("(ds_int)(" + current + " / " + value + ")"), false};
            }
            return {store("(" + ct + ")(" + current + " / " + value + ")"), false};
        case TokenType::MOD_EQUAL:
            if (ints) return {store("ds_mod(" + current + ", " + value + ")"), false};
            return {store("(" + ct + ")ds_fmod(" + current + ", " + value + ")"), false};
        case TokenType::EXPONENT_EQUAL:
            if (ints) return {store("ds_pow(" + current + ", " + value + ")"), false};
            return {store("(" + ct + ")pow((double)" + current + ", (double)(" + value + "))"), false};
        default: unsupported("the operator " + e->op.lexeme);
    }
}

// ============================================================
// Text
// ============================================================

// An owned ds_str* with the value as print() shows it
std::string CCodegen::toText(const CV& v, const SemanticType& type, bool forPrint) {
    (void)forPrint;
    const SemanticType base = withoutNil(type);
    if (isOptValue(type)) {
        std::string o = blockVar(tempCounter);
        std::string inner = toText({o + ".value", false}, base);
        return "({ " + cType(type) + " " + o + " = " + v.code + "; " + o + ".has ? " + inner + " : ds_str_cstr(\"Nil\"); })";
    }
    switch (type.kind) {
        case SemanticTypeKind::Int:
            if (type.radix && *type.radix != 10) return "ds_radix_str(" + v.code + ", " + std::to_string(*type.radix) + ")";
            return "ds_int_str(" + v.code + ")";
        case SemanticTypeKind::Dbl: return "ds_dbl_str(" + v.code + ")";
        case SemanticTypeKind::Flt: return "ds_flt_str(" + v.code + ")";
        case SemanticTypeKind::Bul: return "ds_bul_str(" + v.code + ")";
        case SemanticTypeKind::Chr: return "ds_chr_str(" + v.code + ")";
        case SemanticTypeKind::Str: return v.owned ? v.code : "((ds_str*)ds_retain(" + v.code + "))";
        case SemanticTypeKind::Nil: return "ds_str_cstr(\"Nil\")";
        case SemanticTypeKind::UserDefined:
            if (type.isEnum()) return "dse_m" + std::to_string(type.moduleId) + "_" + type.name + "(" + v.code + ")";
            break;
        default: break;
    }
    if (isCounted(type)) return "ds_object_str(" + use(v, type) + ")";
    unsupported("printing " + type.toString());
}

// Adds an expression's text to the builder `b` (in a ({ ... }) block)
void CCodegen::appendText(std::string& code, const std::string& b, const Expr* e) {
    const Expr* in = ungroup(e);
    if (in->kind == ExprKind::Convert && static_cast<const ConvertExpr*>(in)->type.lexeme == "Str") {
        const Expr* value = static_cast<const ConvertExpr*>(in)->value.get();
        const SemanticType* vt = analyser.typeOf(value);
        if (vt && !vt->containsNil() && (vt->is(SemanticTypeKind::Int) || vt->is(SemanticTypeKind::Str) || vt->is(SemanticTypeKind::Chr))) {
            e = value;
        }
    }
    const SemanticType& t = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    CV v = expr(e);
    if (t.is(SemanticTypeKind::Str)) {
        code += "ds_sb_str(&" + b + ", " + use(v, t) + "); ";
    } else if (t.is(SemanticTypeKind::Int) && (!t.radix || *t.radix == 10)) {
        code += "ds_sb_int(&" + b + ", " + v.code + "); ";
    } else if (t.is(SemanticTypeKind::Chr)) {
        code += "ds_sb_chr(&" + b + ", " + v.code + "); ";
    } else {
        code += "ds_sb_take(&" + b + ", " + toText(v, t) + "); ";
    }
}

CCodegen::CV CCodegen::templateString(const TemplateStringExpr* e) {
    std::string b = blockVar(tempCounter);
    std::string code = "({ ds_sb " + b + "; ds_sb_init(&" + b + "); ";
    for (size_t i = 0; i < e->literalParts.size(); ++i) {
        if (!e->literalParts[i].empty()) code += "ds_sb_str(&" + b + ", " + literal(e->literalParts[i]) + "); ";
        if (i < e->interpolations.size()) appendText(code, b, e->interpolations[i].get());
    }
    code += "ds_sb_done(&" + b + "); })";
    return {code, true};
}

CCodegen::CV CCodegen::printCall(const CallExpr* e) {
    std::string b = blockVar(tempCounter);
    std::string code = "({ ds_sb " + b + "; ds_sb_init(&" + b + "); ";
    std::string sep, end;
    const SemanticType str = simpleType(SemanticTypeKind::Str);
    for (const auto& arg : e->args) {
        if (arg.name && *arg.name == "sep") sep = use(exprAs(arg.value.get(), str), str);
        if (arg.name && *arg.name == "end") end = use(exprAs(arg.value.get(), str), str);
    }
    bool first = true;
    for (const auto& arg : e->args) {
        if (arg.name) continue;
        if (!first) code += sep.empty() ? "ds_sb_bytes(&" + b + ", \" \", 1); " : "ds_sb_str(&" + b + ", " + sep + "); ";
        first = false;
        appendText(code, b, arg.value.get());
    }
    std::string s = blockVar(tempCounter);
    if (end.empty()) {
        code += "ds_sb_bytes(&" + b + ", \"\\n\", 1); ds_str* " + s + " = ds_sb_done(&" + b + "); ds_write(" + s + "); ds_release(" + s + "); })";
    } else {
        code += "ds_str* " + s + " = ds_sb_done(&" + b + "); ds_write(" + s + "); ds_release(" + s + "); ds_write_end(" + end + "); })";
    }
    return {code, false};
}

// ============================================================
// Calls
// ============================================================

// Arguments in parameter order, borrowed, converted to the parameters' types
// (named arguments, defaults, a variadic parameter's list). When two or more
// have effects, C wouldn't promise their order: `prelude` then evaluates
// them first, in order, into variables.
CCodegen::CallArgs CCodegen::arguments(const Expr* callExpr, const std::vector<Argument>& args,
                                       const std::vector<FunctionParameterSymbol>& params) {
    const ArgumentOrder* order = callExpr ? analyser.argumentOrder(callExpr) : nullptr;
    const bool ordered = order && order->size() == params.size();
    std::vector<std::string> code(args.size());
    std::vector<SemanticType> types(args.size(), kUnknown);
    for (size_t a = 0; a < args.size(); ++a) {
        const SemanticType* p = &kUnknown;
        if (ordered) {
            for (size_t i = 0; i < params.size(); ++i) {
                const auto& sources = (*order)[i];
                if (std::find(sources.begin(), sources.end(), static_cast<int>(a)) == sources.end()) continue;
                p = &params[i].type;
                if (params[i].variadic && !args[a].isSpread && params[i].type.elementType) p = params[i].type.elementType.get();
            }
        } else if (a < params.size()) {
            p = &params[a].type;
        }
        types[a] = *p;
        code[a] = use(exprAs(args[a].value.get(), *p), *p);
    }
    CallArgs result;
    if (args.size() >= 2 && std::any_of(args.begin(), args.end(), [](const Argument& a) { return hasSideEffects(a.value.get()); })) {
        for (size_t a = 0; a < args.size(); ++a) {
            if (types[a].isUnknown()) {
                const SemanticType* t = analyser.typeOf(args[a].value.get());
                if (!t) unsupported("this call");
                types[a] = *t;
            }
            std::string v = blockVar(tempCounter);
            result.prelude += cType(types[a]) + " " + v + " = " + code[a] + "; ";
            code[a] = v;
        }
    }
    if (!ordered) {
        result.values = code;
        return result;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& sources = (*order)[i];
        const FunctionParameterSymbol& p = params[i];
        if (p.variadic) {
            const SemanticType element = p.type.elementType ? *p.type.elementType : kUnknown;
            std::string l = blockVar(tempCounter);
            std::string built = "({ ds_list* " + l + " = ds_list_new(" + elem(element) + ", " + std::to_string(sources.size()) + "); ";
            for (int a : sources) {
                const std::string& c = code[static_cast<size_t>(a)];
                if (args[static_cast<size_t>(a)].isSpread) {
                    built += "ds_list_append_all(&" + l + ", " + c + "); ";
                } else {
                    std::string item = isCounted(element) ? "(" + cType(element) + ")ds_retain(" + c + ")" : c;
                    built += "{ " + cType(element) + " ds_v = " + item + "; if (" + l + "->len == " + l + "->cap) ds_list_grow(" + l +
                             "); ((" + cType(element) + "*)" + l + "->data)[" + l + "->len++] = ds_v; } ";
                }
            }
            built += l + "; })";
            result.values.push_back(use({built, true}, p.type));
        } else if (!sources.empty()) {
            result.values.push_back(code[static_cast<size_t>(sources.front())]);
        } else if (p.defaultValue) {
            result.values.push_back(use(exprAs(p.defaultValue, p.type), p.type));
        }
    }
    return result;
}

// fn(first, args...) with the arguments' prelude in front
std::string CCodegen::callWith(const std::string& fn, const CallArgs& args, const std::string& first) {
    std::string list = first;
    for (const auto& a : args.values) list += (list.empty() ? "" : ", ") + a;
    if (args.prelude.empty()) return fn + "(" + list + ")";
    return "({ " + args.prelude + fn + "(" + list + "); })";
}

std::string CCodegen::callFn(const std::string& fn, const SemanticType& fnType, const CallArgs& args) {
    const SemanticType ft = withoutNil(fnType);
    const SemanticType ret = ft.returnType ? *ft.returnType : simpleType(SemanticTypeKind::Emp);
    std::string sig = cType(ret) + " (*)(ds_fn*";
    for (const auto& p : ft.parameterTypes) sig += ", " + cType(p);
    sig += ")";
    std::string f = blockVar(tempCounter);
    std::string list = f;
    for (const auto& x : args.values) list += ", " + x;
    return "({ " + args.prelude + "ds_fn* " + f + " = ds_callable(" + fn + "); ((" + sig + ")" + f + "->code)(" + list + "); })";
}

CCodegen::CV CCodegen::call(const CallExpr* e) {
    const Expr* callee = e->callee.get();
    const SemanticType& result = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;

    if (callee->kind == ExprKind::Identifier || analyser.isAliasRef(callee)) {
        const Symbol* symbol = analyser.symbolOf(callee);
        if (symbol && symbol->kind == SymbolKind::Builtin) {
            builtinPrelude.clear();
            CV v = builtinCall(e, *symbol);
            if (!builtinPrelude.empty()) v.code = "({ " + builtinPrelude + v.code + "; })";
            builtinPrelude.clear();
            return v;
        }
        if (symbol && symbol->kind == SymbolKind::Function) {
            const std::string& name = symbol->originalName.empty() ? symbol->name : symbol->originalName;
            return {callWith(functionName(symbol->moduleId, name), arguments(e, e->args, symbol->parameters)), isCounted(symbol->returnType)};
        }
        if (symbol && symbol->kind == SymbolKind::Method) {
            // a method of this class called without `this.`
            const ClassInfo* info = currentClass;
            if (!info) unsupported("this call");
            CallArgs args = arguments(e, e->args, symbol->parameters);
            std::string list;
            for (const auto& a : args.values) list += ", " + a;
            CV called = classMethodCall(info, nullptr, thisValue().code, symbol->name, *symbol, list, false);
            if (!args.prelude.empty()) called.code = "({ " + args.prelude + called.code + "; })";
            return called;
        }
    }
    if (callee->kind == ExprKind::Member) {
        const auto* m = static_cast<const MemberExpr*>(callee);
        const Symbol* symbol = analyser.memberOf(callee);
        if (symbol && symbol->kind == SymbolKind::Method) return methodCall(e, m, *symbol);
    }
    return functionValueCall(e);
    (void)result;
}

CCodegen::CV CCodegen::functionValueCall(const CallExpr* e) {
    const SemanticType& ft = analyser.typeOf(e->callee.get()) ? *analyser.typeOf(e->callee.get()) : kUnknown;
    const SemanticType fn = withoutNil(ft);
    if (!fn.is(SemanticTypeKind::Function)) unsupported("calling " + ft.toString());
    std::vector<FunctionParameterSymbol> params;
    for (const auto& p : fn.parameterTypes) params.push_back({"", p});
    std::string f = use(expr(e->callee.get()), ft);
    const SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Emp);
    return {callFn(f, fn, arguments(e, e->args, params)), isCounted(ret)};
}

CCodegen::CV CCodegen::builtinCall(const CallExpr* e, const Symbol& symbol) {
    const std::string& n = symbol.cppName;
    if (n == "dessa::print") return printCall(e);
    CallArgs callArgs = arguments(e, e->args, symbol.parameters);
    builtinPrelude = callArgs.prelude;   // call() puts it in front
    const std::vector<std::string>& a = callArgs.values;
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::string(); };
    const SemanticType& result = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    const std::string rc = result.isVoidLike() ? "void" : cType(result);
    if (n == "dessa::input") return {"ds_input(" + (a.empty() ? literal("") : arg(0)) + ")", true};
    if (n == "dessa::type_of") {
        const Expr* value = e->args[0].value.get();
        const SemanticType& t = analyser.typeOf(value) ? *analyser.typeOf(value) : kUnknown;
        std::string known = t.toString();
        if (withoutNil(t).isReference()) return {"ds_class_name(" + arg(0) + ")", true};
        std::string name = known;
        const SemanticType b = withoutNil(t);
        if (b.is(SemanticTypeKind::Int)) name = known.rfind("Int", 0) == 0 ? known.substr(0, known.find(' ')) : "Int";
        else if (b.is(SemanticTypeKind::Dbl)) name = "Dbl";
        else if (b.is(SemanticTypeKind::Flt)) name = "Flt";
        else if (b.is(SemanticTypeKind::Bul)) name = "Bul";
        else if (b.is(SemanticTypeKind::Chr)) name = "Chr";
        else if (b.is(SemanticTypeKind::Str)) name = "Str";
        else if (b.isEnum()) name = b.toString();
        if (isOptValue(t)) return {"((" + arg(0) + ").has ? " + literal(name) + " : " + literal("Nil") + ")", false};
        if (t.containsNil() && (b.is(SemanticTypeKind::Str) || b.is(SemanticTypeKind::List) || b.is(SemanticTypeKind::Map))) {
            return {"((" + arg(0) + ") ? " + literal(name) + " : " + literal("Nil") + ")", false};
        }
        return {"((void)(" + arg(0) + "), " + literal(t.containsNil() ? known : name) + ")", false};
    }
    // math
    if (n == "dessa::math::sqrt") return {"ds_math_sqrt(" + arg(0) + ")", false};
    if (n == "dessa::math::cbrt") return {"cbrt(" + arg(0) + ")", false};
    if (n == "dessa::math::pow") return {"pow(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::math::floor") return {"ds_math_floor(" + arg(0) + ")", false};
    if (n == "dessa::math::ceil") return {"ds_math_ceil(" + arg(0) + ")", false};
    if (n == "dessa::math::round") return {"ds_math_round(" + arg(0) + ")", false};
    if (n == "dessa::math::sin") return {"sin(" + arg(0) + ")", false};
    if (n == "dessa::math::cos") return {"cos(" + arg(0) + ")", false};
    if (n == "dessa::math::tan") return {"tan(" + arg(0) + ")", false};
    if (n == "dessa::math::atan2") return {"atan2(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::math::log") return {"ds_math_log(" + arg(0) + ")", false};
    if (n == "dessa::math::log10") return {"ds_math_log10(" + arg(0) + ")", false};
    if (n == "dessa::math::exp") return {"exp(" + arg(0) + ")", false};
    if (n == "dessa::math::random") return {"ds_math_random()", false};
    if (n == "dessa::math::randomInt") return {"ds_math_random_int(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::math::abs" || n == "dessa::math::min" || n == "dessa::math::max" || n == "dessa::math::clamp") {
        // generic in C++: the arguments' own types, the result is their common type
        std::vector<std::string> v;
        for (const auto& x : e->args) {
            const SemanticType& t = analyser.typeOf(x.value.get()) ? *analyser.typeOf(x.value.get()) : kUnknown;
            v.push_back("(" + rc + ")(" + use(expr(x.value.get()), t) + ")");
        }
        std::string x = blockVar(tempCounter), y = blockVar(tempCounter), z = blockVar(tempCounter);
        if (n == "dessa::math::abs") return {"({ " + rc + " " + x + " = " + v[0] + "; " + x + " < 0 ? -" + x + " : " + x + "; })", false};
        if (n == "dessa::math::min") {
            return {"({ " + rc + " " + x + " = " + v[0] + "; " + rc + " " + y + " = " + v[1] + "; " + x + " < " + y + " ? " + x + " : " + y + "; })", false};
        }
        if (n == "dessa::math::max") {
            return {"({ " + rc + " " + x + " = " + v[0] + "; " + rc + " " + y + " = " + v[1] + "; " + x + " < " + y + " ? " + y + " : " + x + "; })", false};
        }
        return {"({ " + rc + " " + x + " = " + v[0] + "; " + rc + " " + y + " = " + v[1] + "; " + rc + " " + z + " = " + v[2] + "; if (" + z +
                    " < " + y + ") ds_clamp_fail(); " + x + " < " + y + " ? " + y + " : (" + z + " < " + x + " ? " + z + " : " + x + "); })", false};
    }
    // random
    if (n == "dessa::random::seed") return {"ds_random_seed(" + arg(0) + ")", false};
    if (n == "dessa::random::rand") return {"ds_random_rand()", false};
    if (n == "dessa::random::randInt") return {"ds_random_int(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::random::randDbl") return {"ds_random_dbl(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::random::chance") return {"ds_random_chance(" + arg(0) + ")", false};
    if (n == "dessa::random::randStr") return {"ds_random_str(" + arg(0) + ")", true};
    if (n == "dessa::random::uuid") return {"ds_random_uuid()", true};
    if (n == "dessa::random::pick" || n == "dessa::random::shuffle" || n == "dessa::random::sample") {
        if (!builtinPrelude.empty()) unsupported("this call's argument order");
        const SemanticType& lt = analyser.typeOf(e->args[0].value.get()) ? *analyser.typeOf(e->args[0].value.get()) : kUnknown;
        std::string l = use(expr(e->args[0].value.get()), lt);
        if (n == "dessa::random::shuffle") return {"ds_random_shuffle(" + l + ")", true};
        if (n == "dessa::random::sample") {
            std::string c = use(exprAs(e->args[1].value.get(), simpleType(SemanticTypeKind::Int)), simpleType(SemanticTypeKind::Int));
            return {"ds_random_sample(" + l + ", " + c + ")", true};
        }
        const SemanticType et = withoutNil(lt).elementType ? *withoutNil(lt).elementType : kUnknown;
        return {"(*(" + cType(et) + "*)ds_random_pick(" + l + "))", false};
    }
    // sys
    if (n == "dessa::sys::args") return {"(ds_list*)ds_retain(ds_program_args)", true};
    if (n == "dessa::sys::exit") return {"ds_sys_exit(" + arg(0) + ")", false};
    if (n == "dessa::sys::platform") return {"ds_sys_platform()", true};
    // time
    if (n == "dessa::time::millis") return {"ds_time_millis()", false};
    if (n == "dessa::time::now") return {"ds_time_now()", false};
    if (n == "dessa::time::sleep") return {"ds_time_sleep(" + arg(0) + ")", false};
    if (n == "dessa::time::date") return {"ds_time_date()", true};
    // os
    if (n == "dessa::os::readFile") return {"ds_os_read_file(" + arg(0) + ")", true};
    if (n == "dessa::os::writeFile") return {"ds_os_write_file(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::os::appendFile") return {"ds_os_append_file(" + arg(0) + ", " + arg(1) + ")", false};
    if (n == "dessa::os::fileExists") return {"ds_os_file_exists(" + arg(0) + ")", false};
    if (n == "dessa::os::isDir") return {"ds_os_is_dir(" + arg(0) + ")", false};
    if (n == "dessa::os::deleteFile") return {"ds_os_delete_file(" + arg(0) + ")", false};
    if (n == "dessa::os::makeDir") return {"ds_os_make_dir(" + arg(0) + ")", false};
    if (n == "dessa::os::cwd") return {"ds_os_cwd()", true};
    if (n == "dessa::os::getEnv") return {"ds_os_get_env(" + arg(0) + ")", true};
    // Anything else: the library's C++ code, through its bridge
    std::string fn = glueFunction(n);
    if (fn.empty()) unsupported("the builtin '" + symbol.name + "'");
    std::string list;
    for (const auto& x : a) list += (list.empty() ? "" : ", ") + x;
    return {fn + "(" + list + ")", isCounted(result)};
}

std::string CCodegen::glueFunction(const std::string& cppName) {
    const BuiltinFunction* f = nullptr;
    const BuiltinLibrary* lib = glueLibraryOf(cppName, &f);
    if (!lib || !f || !glueable(*f)) return "";
    glueLibraries.insert(lib->name);
    std::string proto = gluePrototype(*lib, *f);
    if (gluePrototypes.insert(proto).second) protos << proto << "\n";
    return glueName(*lib, f->name);
}

std::string CCodegen::glueConstant(const std::string& cppName) {
    const BuiltinConstant* c = nullptr;
    const BuiltinLibrary* lib = glueLibraryOf(cppName, nullptr, &c);
    if (!lib || !c || !glueable(*c)) return "";
    glueLibraries.insert(lib->name);
    std::string proto = gluePrototype(*lib, *c);
    if (gluePrototypes.insert(proto).second) protos << proto << "\n";
    return glueName(*lib, c->name) + "()";
}

// object.method(args)
CCodegen::CV CCodegen::methodCall(const CallExpr* e, const MemberExpr* m, const Symbol& symbol) {
    const Expr* object = m->object.get();
    const SemanticType& ot = analyser.typeOf(object) ? *analyser.typeOf(object) : kUnknown;
    if (analyser.isOptionalChain(m)) return optionalChain(m, e);
    if (!symbol.cppName.empty()) {
        CallArgs args = arguments(e, e->args, symbol.parameters);
        CV v = builtinMethod(e, m, symbol, args.values);
        if (!args.prelude.empty()) v.code = "({ " + args.prelude + v.code + "; })";
        return v;
    }
    CallArgs args = arguments(e, e->args, symbol.parameters);
    const std::string prelude = args.prelude;
    std::string list;
    for (const auto& a : args.values) list += ", " + a;
    CV called;
    if (isSuperExpr(object)) {
        const ClassInfo* parent = classOf(ot);
        if (!parent) unsupported("super here");
        called = classMethodCall(parent, nullptr, thisValue().code, m->name.lexeme, symbol, list, true);
    } else {
        std::string objectCode = isThisExpr(object) ? thisValue().code : "ds_ref(" + use(expr(object), ot) + ")";
        if (const ClassInfo* info = classOf(ot)) {
            called = classMethodCall(info, nullptr, "((" + cType(withoutNil(ot)) + ")" + objectCode + ")", m->name.lexeme, symbol, list, false);
        } else if (const InterfaceInfo* iface = interfaceOf(ot)) {
            called = classMethodCall(nullptr, iface, objectCode, m->name.lexeme, symbol, list, false);
        } else {
            unsupported("calling a method on " + ot.toString());
        }
    }
    if (!prelude.empty()) called.code = "({ " + prelude + called.code + "; })";
    return called;
}

// A class's method: straight to its code when nothing overrides it, else
// through the object's method table
CCodegen::CV CCodegen::classMethodCall(const ClassInfo* info, const InterfaceInfo* iface, const std::string& object,
                                       const std::string& method, const Symbol& symbol, const std::string& args, bool isSuper) {
    const bool owned = isCounted(symbol.returnType);
    if (iface) {
        auto it = interfaceIds.find(iface->type().key());
        if (it == interfaceIds.end()) unsupported("this interface");
        std::string o = blockVar(tempCounter);
        return {"({ ds_head* " + o + " = (ds_head*)" + object + "; ((const dst_m" + std::to_string(iface->moduleId) + "_" + iface->name +
                    "*)ds_interface(" + o + ", " + std::to_string(it->second) + "))->m_" + method + "(" + o + args + "); })", owned};
    }
    if (method == "toStr" && !info->methods.count("toStr") && !methodOwner(*info, "toStr")) {
        return {"ds_object_str(" + object + ")", true};
    }
    const ClassInfo* owner = methodOwner(*info, method);
    if (!owner) unsupported("the method '" + method + "'");
    if (isSuper || !overridden.count(info->type().key() + "." + method)) {
        return {methodName(*owner, method) + "(" + object + args + ")", owned};
    }
    std::string o = blockVar(tempCounter);
    const std::string s = structName(info->type());
    return {"({ " + s + "* " + o + " = " + object + "; ((const " + s + "_cls*)" + o + "->h.type)->m_" + method + "(" + o + args + "); })", owned};
}

// Methods of the builtin types (Str, List, Map, numbers, ...)
CCodegen::CV CCodegen::builtinMethod(const CallExpr* e, const MemberExpr* m, const Symbol& symbol, const std::vector<std::string>& args) {
    const Expr* object = m->object.get();
    const SemanticType& ot = receiverOverride ? receiverOverrideType : analyser.typeOf(object) ? *analyser.typeOf(object) : kUnknown;
    const SemanticType rt = withoutNil(ot);
    const std::string& name = symbol.name;
    const SemanticType& result = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    auto receiver = [&]() {
        if (receiverOverride) return *receiverOverride;
        std::string r = use(expr(object), ot);
        if (isOptValue(ot)) r = "DS_UNWRAP(" + r + ")";
        return r;
    };
    // The receiver as a place, for methods that change it
    auto slot = [&]() -> std::string {
        if (receiverOverride) unsupported("?. with a method that changes the value");
        const Expr* in = ungroup(object);
        if (in->kind == ExprKind::Identifier || in->kind == ExprKind::Member || in->kind == ExprKind::Index) return lvalue(object);
        std::string t = temp();
        temps.back().push_back({t, cType(ot)});
        return "(*(" + t + " = " + own(expr(object), ot) + ", &" + t + "))";
    };

    if (rt.is(SemanticTypeKind::Str)) {
        std::string r = receiver();
        if (name == "length") return {"ds_str_length(" + r + ")", false};
        if (name == "isEmpty") return {"((" + r + ")->len == 0)", false};
        if (name == "upper") return {"ds_str_upper(" + r + ")", true};
        if (name == "lower") return {"ds_str_lower(" + r + ")", true};
        if (name == "trim") return {"ds_str_trim(" + r + ")", true};
        if (name == "title") return {"ds_str_title(" + r + ")", true};
        if (name == "plain") return {"ds_str_plain(" + r + ")", true};
        if (name == "caseless") return {"ds_str_caseless(" + r + ")", true};
        if (name == "includes") return {"ds_str_includes(" + r + ", " + arg(0) + ")", false};
        if (name == "startsWith") return {"ds_str_starts(" + r + ", " + arg(0) + ")", false};
        if (name == "endsWith") return {"ds_str_ends(" + r + ", " + arg(0) + ")", false};
        if (name == "indexOf") return {"ds_str_index_of(" + r + ", " + arg(0) + ")", false};
        if (name == "replace") return {"ds_str_replace(" + r + ", " + arg(0) + ", " + arg(1) + ", false)", true};
        if (name == "replaceAll") return {"ds_str_replace(" + r + ", " + arg(0) + ", " + arg(1) + ", true)", true};
        if (name == "split") return {"ds_str_split(" + r + ", " + arg(0) + ")", true};
        if (name == "substr") return {"ds_str_substr(" + r + ", " + arg(0) + ", " + arg(1) + ")", true};
        if (name == "repeat") return {"ds_str_repeat(" + r + ", " + arg(0) + ")", true};
        if (name == "chars") return {"ds_str_chars(" + r + ")", true};
        if (name == "toInt") return {"ds_str_to_int_exact(" + r + ")", false};
        if (name == "toDbl") return {"ds_str_to_dbl(" + r + ", \"Dbl\")", false};
        if (name == "toFlt") return {"((ds_flt)ds_str_to_dbl(" + r + ", \"Dbl\"))", false};
        if (name == "toStr") return {r, false};
        if (name == "compare") return {"ds_str_compare(" + r + ", " + arg(0) + ")", false};
    } else if (rt.is(SemanticTypeKind::Chr)) {
        std::string r = receiver();
        if (name == "isDigit") return {"({ ds_chr ds_c = " + r + "; ds_c >= '0' && ds_c <= '9'; })", false};
        if (name == "isAlpha") return {"ds_chr_is_alpha(" + r + ")", false};
        if (name == "isSpace") return {"ds_chr_is_space(" + r + ")", false};
        if (name == "isUpper") return {"({ ds_chr ds_c = " + r + "; ds_chr_lower(ds_c) != ds_c; })", false};
        if (name == "isLower") return {"({ ds_chr ds_c = " + r + "; ds_chr_upper(ds_c) != ds_c; })", false};
        if (name == "upper") return {"ds_chr_upper(" + r + ")", false};
        if (name == "lower") return {"ds_chr_lower(" + r + ")", false};
        if (name == "code") return {"((ds_int)" + r + ")", false};
        if (name == "toInt") return {"ds_chr_digit(" + r + ")", false};
        if (name == "toStr") return {"ds_chr_str(" + r + ")", true};
    } else if (isNumber(rt) || rt.is(SemanticTypeKind::Bul)) {
        std::string r = receiver();
        if (name == "toStr") return {toText({r, false}, rt), true};
        if (name == "toInt") return {"((ds_int)" + r + ")", false};
        if (name == "toDbl") return {"((ds_dbl)" + r + ")", false};
        if (name == "toFlt") return {"((ds_flt)" + r + ")", false};
        if (name == "toChr") return {"ds_int_to_chr(" + r + ")", false};
        if (name == "toFixed") return {"ds_to_fixed((ds_dbl)" + r + ", " + arg(0) + ")", true};
    } else if (rt.isEnum()) {
        std::string r = receiver();
        if (name == "name" || name == "toStr") return {"dse_m" + std::to_string(rt.moduleId) + "_" + rt.name + "(" + r + ")", true};
        if (name == "value") return {r, false};
    } else if (rt.isReference()) {
        if (name == "toStr") return {"ds_object_str(" + receiver() + ")", true};
    } else if (rt.is(SemanticTypeKind::Set)) {
        const SemanticType et = rt.elementType ? *rt.elementType : kUnknown;
        const std::string T = cType(et);
        std::string v = blockVar(tempCounter);
        if (name == "length") return {"((ds_int)(" + receiver() + ")->len)", false};
        if (name == "isEmpty") return {"((" + receiver() + ")->len == 0)", false};
        if (name == "includes") return {"({ " + T + " " + v + " = " + arg(0) + "; ds_set_has(" + receiver() + ", &" + v + "); })", false};
        if (name == "add") {
            std::string value = isCounted(et) ? "(" + T + ")ds_retain(" + arg(0) + ")" : arg(0);
            return {"({ " + T + " " + v + " = " + value + "; ds_set_add(&" + slot() + ", &" + v + "); })", false};
        }
        if (name == "remove") return {"({ " + T + " " + v + " = " + arg(0) + "; ds_set_remove(&" + slot() + ", &" + v + "); })", false};
        if (name == "clear") return {"ds_list_clear(ds_list_unique(&" + slot() + "))", false};
        if (name == "toList") return {"ds_set_to_list(" + receiver() + ")", true};
        if (name == "copy") return {"(ds_list*)ds_retain(" + receiver() + ")", true};
        if (name == "join") return {"ds_list_join(" + receiver() + ", " + arg(0) + ")", true};
        if (name == "forEach" || name == "filter" || name == "map") {
            const SemanticType* given = analyser.typeOf(e->args[0].value.get());
            if (!given || !withoutNil(*given).is(SemanticTypeKind::Function)) unsupported(name + " here");
            const SemanticType fn = withoutNil(*given);
            if (fn.parameterTypes.size() != 1 || cType(fn.parameterTypes[0]) != T) unsupported(name + " with a lambda of another type");
            std::string src = blockVar(tempCounter), f = blockVar(tempCounter), i = blockVar(tempCounter), out = blockVar(tempCounter);
            const SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Emp);
            std::string sig = cType(ret) + " (*)(ds_fn*, " + T + ")";
            std::string head = "({ ds_list* " + src + " = (ds_list*)ds_retain(" + receiver() + "); ds_fn* " + f + " = ds_callable(" + arg(0) + "); ";
            std::string item = "((" + T + "*)" + src + "->data)[" + i + "]";
            std::string callIt = "((" + sig + ")" + f + "->code)(" + f + ", " + item + ")";
            if (name == "forEach") {
                std::string body = isCounted(ret) ? "ds_release(" + callIt + ");" : callIt + ";";
                return {head + "for (int64_t " + i + " = 0; " + i + " < " + src + "->len; " + i + "++) " + body + " ds_release(" + src + "); })", false};
            }
            if (name == "filter") {
                std::string keep = isCounted(et) ? "ds_retain(" + item + "); " : "";
                return {head + "ds_list* " + out + " = ds_set_new(" + elem(et) + "); for (int64_t " + i + " = 0; " + i + " < " + src + "->len; " + i +
                            "++) { if (" + callIt + ") { " + keep + "if (" + out + "->len == " + out + "->cap) ds_list_grow(" + out + "); ((" + T +
                            "*)" + out + "->data)[" + out + "->len++] = " + item + "; } } ds_release(" + src + "); " + out + "; })", true};
            }
            const SemanticType resultElement = withoutNil(result).elementType ? *withoutNil(result).elementType : ret;
            const std::string R = cType(resultElement);
            std::string conv = convert({callIt, isCounted(ret)}, ret, resultElement).code;
            std::string rv = blockVar(tempCounter);
            return {head + "ds_list* " + out + " = ds_list_new(" + elem(resultElement) + ", " + src + "->len); for (int64_t " + i + " = 0; " + i +
                        " < " + src + "->len; " + i + "++) { " + R + " " + rv + " = " + conv + "; ((" + R + "*)" + out + "->data)[" + out +
                        "->len++] = " + rv + "; } ds_release(" + src + "); " + out + "; })", true};
        }
    } else if (rt.is(SemanticTypeKind::List)) {
        // ([]).length(): an empty list of nothing in particular
        const SemanticType et = rt.elementType && !rt.elementType->isUnknown() ? *rt.elementType : simpleType(SemanticTypeKind::Int);
        const std::string T = cType(et);
        const std::string E = elem(et);
        std::string v = blockVar(tempCounter), l = blockVar(tempCounter);
        if (name == "length") return {"((ds_int)(" + receiver() + ")->len)", false};
        if (name == "isEmpty") return {"((" + receiver() + ")->len == 0)", false};
        if (name == "includes") return {"({ " + T + " " + v + " = " + arg(0) + "; ds_list_index_of(" + receiver() + ", &" + v + ") >= 0; })", false};
        if (name == "indexOf") return {"({ " + T + " " + v + " = " + arg(0) + "; ds_list_index_of(" + receiver() + ", &" + v + "); })", false};
        if (name == "first" || name == "last") {
            std::string r = receiver();
            return {"({ ds_list* " + l + " = " + r + "; if (!" + l + "->len) ds_fail(\"" + name + "() on an empty list\"); ((" + T + "*)" + l +
                        "->data)[" + (name == "first" ? std::string("0") : l + "->len - 1") + "]; })", false};
        }
        if (name == "slice") return {"ds_list_slice(" + receiver() + ", " + arg(0) + ", " + arg(1) + ")", true};
        if (name == "copy" || name == "toList") return {"(ds_list*)ds_retain(" + receiver() + ")", true};
        if (name == "join") return {"ds_list_join(" + receiver() + ", " + arg(0) + ")", true};
        if (name == "push") {
            std::string value = isCounted(et) ? "(" + T + ")ds_retain(" + arg(0) + ")" : arg(0);
            std::string s = slot();
            return {"({ " + T + " " + v + " = " + value + "; ds_list* " + l + " = ds_list_unique(&" + s + "); if (" + l + "->len == " + l +
                        "->cap) ds_list_grow(" + l + "); ((" + T + "*)" + l + "->data)[" + l + "->len++] = " + v + "; })", false};
        }
        if (name == "pop") return {"({ " + T + " " + v + "; ds_list_pop(ds_list_unique(&" + slot() + "), &" + v + "); " + v + "; })", isCounted(et)};
        if (name == "insert") {
            std::string value = isCounted(et) ? "(" + T + ")ds_retain(" + arg(1) + ")" : arg(1);
            std::string i = blockVar(tempCounter);
            return {"({ ds_int " + i + " = " + arg(0) + "; " + T + " " + v + " = " + value + "; ds_list_insert(ds_list_unique(&" + slot() + "), " +
                        i + ", &" + v + "); })", false};
        }
        if (name == "removeAt") {
            std::string i = blockVar(tempCounter);
            return {"({ ds_int " + i + " = " + arg(0) + "; " + T + " " + v + "; ds_list_remove_at(ds_list_unique(&" + slot() + "), " + i + ", &" + v +
                        "); " + v + "; })", isCounted(et)};
        }
        if (name == "remove") return {"({ " + T + " " + v + " = " + arg(0) + "; ds_list_remove(ds_list_unique(&" + slot() + "), &" + v + "); })", false};
        if (name == "clear") return {"ds_list_clear(ds_list_unique(&" + slot() + "))", false};
        if (name == "reverse") return {"ds_list_reverse(ds_list_unique(&" + slot() + "))", false};
        if (name == "sort") {
            if (args.empty()) {
                if (isCounted(et) && !withoutNil(et).is(SemanticTypeKind::Str)) unsupported("sorting " + ot.toString());
                return {"ds_list_sort(ds_list_unique(&" + slot() + "))", false};
            }
            const SemanticType fnType = symbol.parameters.empty() ? kUnknown : symbol.parameters[0].type;
            const SemanticType* given = analyser.typeOf(e->args[0].value.get());
            std::string cmp = sortComparator(et, given ? *given : fnType);
            return {"ds_list_sort_by(ds_list_unique(&" + slot() + "), " + cmp + ", " + arg(0) + ")", false};
        }
        if (name == "forEach" || name == "filter" || name == "map") {
            const SemanticType* given = analyser.typeOf(e->args[0].value.get());
            if (!given || !withoutNil(*given).is(SemanticTypeKind::Function)) unsupported(name + " here");
            const SemanticType fn = withoutNil(*given);
            if (fn.parameterTypes.size() != 1 || !(withoutNil(fn.parameterTypes[0]) == withoutNil(et)) ||
                cType(fn.parameterTypes[0]) != T) {
                unsupported(name + " with a lambda of another type");
            }
            std::string src = blockVar(tempCounter), f = blockVar(tempCounter), i = blockVar(tempCounter), out = blockVar(tempCounter);
            const SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Emp);
            std::string sig = cType(ret) + " (*)(ds_fn*, " + T + ")";
            std::string head = "({ ds_list* " + src + " = (ds_list*)ds_retain(" + receiver() + "); ds_fn* " + f + " = ds_callable(" + arg(0) + "); ";
            std::string item = "((" + T + "*)" + src + "->data)[" + i + "]";
            std::string callIt = "((" + sig + ")" + f + "->code)(" + f + ", " + item + ")";
            if (name == "forEach") {
                std::string body = isCounted(ret) ? "ds_release(" + callIt + ");" : callIt + ";";
                return {head + "for (int64_t " + i + " = 0; " + i + " < " + src + "->len; " + i + "++) " + body + " ds_release(" + src + "); })", false};
            }
            if (name == "filter") {
                std::string keep = isCounted(et) ? "ds_retain(" + item + "); " : "";
                return {head + "ds_list* " + out + " = ds_list_new(" + E + ", 0); for (int64_t " + i + " = 0; " + i + " < " + src + "->len; " + i +
                            "++) { if (" + callIt + ") { " + keep + "if (" + out + "->len == " + out + "->cap) ds_list_grow(" + out + "); ((" + T +
                            "*)" + out + "->data)[" + out + "->len++] = " + item + "; } } ds_release(" + src + "); " + out + "; })", true};
            }
            // map: a list of whatever the lambda gives
            const SemanticType resultElement = withoutNil(result).elementType ? *withoutNil(result).elementType : ret;
            const std::string R = cType(resultElement);
            std::string conv = convert({callIt, isCounted(ret)}, ret, resultElement).code;
            std::string rv = blockVar(tempCounter);
            std::string value = isCounted(resultElement) && !isCounted(ret) ? conv : conv;
            return {head + "ds_list* " + out + " = ds_list_new(" + elem(resultElement) + ", " + src + "->len); for (int64_t " + i + " = 0; " + i +
                        " < " + src + "->len; " + i + "++) { " + R + " " + rv + " = " + value + "; ((" + R + "*)" + out + "->data)[" + out +
                        "->len++] = " + rv + "; } ds_release(" + src + "); " + out + "; })", true};
        }
    } else if (rt.is(SemanticTypeKind::Map)) {
        const SemanticType kt = rt.keyType ? *rt.keyType : kUnknown;
        const SemanticType vt = rt.elementType ? *rt.elementType : kUnknown;
        const std::string K = cType(kt), V = cType(vt);
        std::string mm = blockVar(tempCounter), k = blockVar(tempCounter), at = blockVar(tempCounter);
        if (name == "length") return {"((ds_int)(" + receiver() + ")->len)", false};
        if (name == "isEmpty") return {"((" + receiver() + ")->len == 0)", false};
        if (name == "has") return {"({ " + K + " " + k + " = " + arg(0) + "; ds_map_find(" + receiver() + ", &" + k + ") >= 0; })", false};
        if (name == "get") {
            std::string found = isOptValue(result) ? "(" + cType(result) + "){true, DS_MAP_VALUE(" + mm + ", " + V + ", " + at + ")}"
                                                   : "DS_MAP_VALUE(" + mm + ", " + V + ", " + at + ")";
            std::string missing = isOptValue(result) ? "(" + cType(result) + "){0}" : "NULL";
            return {"({ ds_map* " + mm + " = " + receiver() + "; " + K + " " + k + " = " + arg(0) + "; int64_t " + at + " = ds_map_find(" + mm +
                        ", &" + k + "); " + at + " >= 0 ? " + found + " : " + missing + "; })", false};
        }
        if (name == "getOr") {
            std::string fb = blockVar(tempCounter);
            return {"({ ds_map* " + mm + " = " + receiver() + "; " + K + " " + k + " = " + arg(0) + "; " + V + " " + fb + " = " + arg(1) +
                        "; int64_t " + at + " = ds_map_find(" + mm + ", &" + k + "); " + at + " >= 0 ? DS_MAP_VALUE(" + mm + ", " + V + ", " + at +
                        ") : " + fb + "; })", false};
        }
        if (name == "set") {
            std::string v = blockVar(tempCounter);
            std::string key = isCounted(kt) ? "(" + K + ")ds_retain(" + arg(0) + ")" : arg(0);
            std::string value = isCounted(vt) ? "(" + V + ")ds_retain(" + arg(1) + ")" : arg(1);
            return {"({ " + K + " " + k + " = " + key + "; " + V + " " + v + " = " + value + "; ds_map_put(&" + slot() + ", &" + k + ", &" + v + "); })", false};
        }
        if (name == "remove") return {"({ " + K + " " + k + " = " + arg(0) + "; ds_map_remove(&" + slot() + ", &" + k + "); })", false};
        if (name == "keys") return {"ds_map_keys(" + receiver() + ")", true};
        if (name == "values") return {"ds_map_values(" + receiver() + ")", true};
        if (name == "clear") return {"ds_map_clear(ds_map_unique(&" + slot() + "))", false};
        if (name == "copy") return {"(ds_map*)ds_retain(" + receiver() + ")", true};
        if (name == "forEach") {
            const SemanticType* given = analyser.typeOf(e->args[0].value.get());
            if (!given || !withoutNil(*given).is(SemanticTypeKind::Function)) unsupported("forEach here");
            const SemanticType fn = withoutNil(*given);
            if (fn.parameterTypes.size() != 2 || cType(fn.parameterTypes[0]) != K || cType(fn.parameterTypes[1]) != V) {
                unsupported("forEach with a lambda of another type");
            }
            const SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Emp);
            std::string f = blockVar(tempCounter), i = blockVar(tempCounter);
            std::string sig = cType(ret) + " (*)(ds_fn*, " + K + ", " + V + ")";
            std::string callIt = "((" + sig + ")" + f + "->code)(" + f + ", DS_MAP_KEY(" + mm + ", " + K + ", " + i + "), DS_MAP_VALUE(" + mm + ", " + V +
                                 ", " + i + "))";
            std::string body = isCounted(ret) ? "ds_release(" + callIt + ");" : callIt + ";";
            return {"({ ds_map* " + mm + " = (ds_map*)ds_retain(" + receiver() + "); ds_fn* " + f + " = ds_callable(" + arg(0) + "); for (int64_t " +
                        i + " = 0; " + i + " < " + mm + "->len; " + i + "++) " + body + " ds_release(" + mm + "); })", false};
        }
    }
    unsupported("the method " + ot.toString() + "." + name);
}

// list.sort((a, b) := ...): a C comparison function for the runtime's sort
std::string CCodegen::sortComparator(const SemanticType& element, const SemanticType& fnType) {
    const SemanticType fn = withoutNil(fnType);
    if (!fn.is(SemanticTypeKind::Function) || fn.parameterTypes.size() != 2) unsupported("this sort");
    const std::string T = cType(element);
    if (cType(fn.parameterTypes[0]) != T || cType(fn.parameterTypes[1]) != T) unsupported("sort with a lambda of another type");
    const SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Int);
    const std::string name = "ds_sortcmp" + std::to_string(sortCounter++);
    protos << "static int " << name << "(void* fn, const void* a, const void* b);\n";
    code << "static int " << name << "(void* fn, const void* a, const void* b) {\n"
         << "    ds_fn* f = (ds_fn*)fn;\n"
         << "    ds_int r = (ds_int)((" << cType(ret) << " (*)(ds_fn*, " << T << ", " << T << "))f->code)(f, *(" << T
         << " const*)a, *(" << T << " const*)b);\n"
         << "    return r < 0 ? -1 : r > 0;\n}\n\n";
    return name;
}

// ============================================================
// Members, indexes
// ============================================================

CCodegen::CV CCodegen::member(const MemberExpr* e) {
    if (analyser.isDynamicMember(e)) unsupported("Obj");
    if (analyser.isAliasRef(e)) {
        const Symbol* target = analyser.symbolOf(e);
        if (!target) unsupported("this name");
        if (target->kind == SymbolKind::Builtin) {
            if (target->cppName == "dessa::math::PI") return {"3.14159265358979323846", false};
            if (target->cppName == "dessa::math::E") return {"2.71828182845904523536", false};
            if (std::string c = glueConstant(target->cppName); !c.empty()) return {c, isCounted(target->type)};
            return builtinValue(*target);
        }
        if (target->kind == SymbolKind::Function) return functionValue(*target);
        if (target->kind == SymbolKind::Variable || target->kind == SymbolKind::Constant) return {globalName(*target), false};
        unsupported("this name");
    }
    const Symbol* symbol = analyser.memberOf(e);
    if (!symbol) unsupported("this member");
    const Expr* object = e->object.get();
    const SemanticType& ot = analyser.typeOf(object) ? *analyser.typeOf(object) : kUnknown;

    // Color.RED
    if (symbol->kind == SymbolKind::Constant && ot.isEnum() && analyser.symbolOf(object) &&
        analyser.symbolOf(object)->kind == SymbolKind::Enum) {
        return {"dsv_m" + std::to_string(ot.moduleId) + "_" + ot.name + "_" + e->name.lexeme, false};
    }
    if (analyser.isOptionalChain(e)) return optionalChain(e, nullptr);
    if (symbol->kind != SymbolKind::Field) unsupported("a method used as a value");
    if (isThisExpr(object) || isSuperExpr(object)) return {thisValue().code + "->" + mangle(e->name.lexeme), false};
    if (const InterfaceInfo* iface = interfaceOf(ot)) {
        auto it = interfaceIds.find(iface->type().key());
        if (it == interfaceIds.end()) unsupported("this interface");
        std::string o = blockVar(tempCounter);
        return {"(*({ ds_head* " + o + " = ds_ref(" + use(expr(object), ot) + "); ((const dst_m" + std::to_string(iface->moduleId) + "_" +
                    iface->name + "*)ds_interface(" + o + ", " + std::to_string(it->second) + "))->get_" + e->name.lexeme + "(" + o + "); }))", false};
    }
    if (!classOf(ot)) unsupported("a member of " + ot.toString());
    return {"((" + cType(withoutNil(ot)) + ")ds_ref(" + use(expr(object), ot) + "))->" + mangle(e->name.lexeme), false};
}

// a?.b / a?.m(args): nil when a is nil
CCodegen::CV CCodegen::optionalChain(const MemberExpr* m, const CallExpr* callExpr) {
    const Expr* object = m->object.get();
    const SemanticType& ot = analyser.typeOf(object) ? *analyser.typeOf(object) : kUnknown;
    const SemanticType& result = analyser.typeOf(callExpr ? static_cast<const Expr*>(callExpr) : m) ? *analyser.typeOf(callExpr ? static_cast<const Expr*>(callExpr) : m)
                                                                                                      : kUnknown;
    if (!isCounted(ot)) unsupported("?. on " + ot.toString());
    const ClassInfo* info = classOf(ot);
    std::string o = blockVar(tempCounter), r = blockVar(tempCounter);
    const std::string oc = cType(withoutNil(ot));
    std::string head = "({ " + oc + " " + o + " = (" + oc + ")" + use(expr(object), ot) + "; ";
    const Symbol* symbol = analyser.memberOf(m);
    if (!symbol) unsupported("this ?.");
    if (!info) {
        // text?.length(), maybeMap?.get(k): a builtin method on what isn't nil
        if (!callExpr || symbol->cppName.empty()) unsupported("?. on " + ot.toString());
        CallArgs args = arguments(callExpr, callExpr->args, symbol->parameters);
        if (!args.prelude.empty()) unsupported("this call's argument order");
        receiverOverride = &o;
        receiverOverrideType = withoutNil(ot);
        CV called = builtinMethod(callExpr, m, *symbol, args.values);
        receiverOverride = nullptr;
        const SemanticType& raw = symbol->returnType;
        if (result.isVoidLike() || raw.isVoidLike()) return {head + "if (" + o + ") " + discard(called, raw) + "; })", false};
        CV converted = convert(called, raw, result);
        if (isCounted(result)) return {head + cType(result) + " " + r + " = " + o + " ? " + own(converted, result) + " : NULL; " + r + "; })", true};
        return {head + cType(result) + " " + r + " = " + o + " ? " + converted.code + " : (" + cType(result) + "){0}; " + r + "; })", false};
    }
    if (!callExpr) {
        const SemanticType& ft = symbol->type;
        CV value{o + "->" + mangle(m->name.lexeme), false};
        if (result.isVoidLike()) unsupported("this ?.");
        CV converted = convert(value, ft, result);
        if (isCounted(result)) {
            return {head + cType(result) + " " + r + " = " + o + " ? (" + cType(result) + ")ds_retain(" + converted.code + ") : NULL; " + r + "; })", true};
        }
        return {head + cType(result) + " " + r + " = " + o + " ? " + converted.code + " : (" + cType(result) + "){0}; " + r + "; })", false};
    }
    if (symbol->kind != SymbolKind::Method || !symbol->cppName.empty()) unsupported("this ?. call");
    CallArgs args = arguments(callExpr, callExpr->args, symbol->parameters);
    if (!args.prelude.empty()) unsupported("this call's argument order");
    std::string list;
    for (const auto& a : args.values) list += ", " + a;
    CV called = classMethodCall(info, nullptr, o, m->name.lexeme, *symbol, list, false);
    if (result.isVoidLike() || symbol->returnType.isVoidLike()) {
        return {head + "if (" + o + ") " + discard(called, symbol->returnType) + "; })", false};
    }
    CV converted = convert(called, symbol->returnType, result);
    if (isCounted(result)) {
        return {head + cType(result) + " " + r + " = " + o + " ? " + own(converted, result) + " : NULL; " + r + "; })", true};
    }
    return {head + cType(result) + " " + r + " = " + o + " ? " + converted.code + " : (" + cType(result) + "){0}; " + r + "; })", false};
}

CCodegen::CV CCodegen::index(const IndexExpr* e) {
    const SemanticType& ot = analyser.typeOf(e->object.get()) ? *analyser.typeOf(e->object.get()) : kUnknown;
    const SemanticType base = withoutNil(ot);
    const SemanticType intType = simpleType(SemanticTypeKind::Int);
    if (base.is(SemanticTypeKind::Str)) {
        return {"ds_str_at(" + use(expr(e->object.get()), ot) + ", " + use(expr(e->index.get()), intType) + ")", false};
    }
    if (base.is(SemanticTypeKind::List)) {
        const SemanticType et = base.elementType ? *base.elementType : kUnknown;
        return {"(*(" + cType(et) + "*)ds_list_at(" + use(expr(e->object.get()), ot) + ", " + use(exprAs(e->index.get(), intType), intType) +
                    ", sizeof(" + cType(et) + ")))", false};
    }
    if (base.is(SemanticTypeKind::Map)) {
        const SemanticType kt = base.keyType ? *base.keyType : kUnknown;
        const SemanticType vt = base.elementType ? *base.elementType : kUnknown;
        std::string m = blockVar(tempCounter), k = blockVar(tempCounter), at = blockVar(tempCounter);
        return {"({ ds_map* " + m + " = " + use(expr(e->object.get()), ot) + "; " + cType(kt) + " " + k + " = " +
                    use(exprAs(e->index.get(), kt), kt) + "; int64_t " + at + " = ds_map_find(" + m + ", &" + k + "); if (" + at +
                    " < 0) ds_map_missing(" + m + ", &" + k + "); DS_MAP_VALUE(" + m + ", " + cType(vt) + ", " + at + "); })", false};
    }
    unsupported("indexing " + ot.toString());
}

// ============================================================
// Values: functions, lambdas, objects, collections
// ============================================================

// A named function passed as a value: a never-freed closure that calls it
CCodegen::CV CCodegen::functionValue(const Symbol& symbol) {
    const std::string& name = symbol.originalName.empty() ? symbol.name : symbol.originalName;
    const std::string fn = functionName(symbol.moduleId, name);
    if (!functionValues.count(fn)) {
        functionValues.insert(fn);
        std::string params, args;
        for (size_t i = 0; i < symbol.parameters.size(); ++i) {
            params += ", " + cType(symbol.parameters[i].type) + " a" + std::to_string(i);
            args += (i ? ", " : "") + std::string("a") + std::to_string(i);
        }
        protos << "static " << cType(symbol.returnType) << " dsw_" << fn << "(ds_fn* self" << params << ");\n";
        code << "static " << cType(symbol.returnType) << " dsw_" << fn << "(ds_fn* self" << params << ") {\n    (void)self;\n    "
             << (symbol.returnType.isVoidLike() ? "" : "return ") << fn << "(" << args << ");\n}\n\n";
        data << "static ds_fn dsfv_" << fn << " = {{DS_FOREVER, &ds_fn_plain_type}, (void*)dsw_" << fn << "};\n";
    }
    return {"(&dsfv_" + fn + ")", false};
}

// A builtin function passed as a value: a never-freed closure that calls it.
// Only builtins that are one C function with the same parameters (most of
// math) or that cross the library bridge.
CCodegen::CV CCodegen::builtinValue(const Symbol& symbol) {
    static const std::unordered_map<std::string, std::string> direct = {
        {"dessa::math::sqrt", "ds_math_sqrt"}, {"dessa::math::cbrt", "cbrt"}, {"dessa::math::pow", "pow"},
        {"dessa::math::floor", "ds_math_floor"}, {"dessa::math::ceil", "ds_math_ceil"}, {"dessa::math::round", "ds_math_round"},
        {"dessa::math::sin", "sin"}, {"dessa::math::cos", "cos"}, {"dessa::math::tan", "tan"}, {"dessa::math::atan2", "atan2"},
        {"dessa::math::log", "ds_math_log"}, {"dessa::math::log10", "ds_math_log10"}, {"dessa::math::exp", "exp"},
        {"dessa::time::millis", "ds_time_millis"}, {"dessa::time::now", "ds_time_now"},
        {"dessa::random::rand", "ds_random_rand"}, {"dessa::random::randInt", "ds_random_int"},
        {"dessa::random::randDbl", "ds_random_dbl"}, {"dessa::random::chance", "ds_random_chance"},
    };
    std::string target;
    auto it = direct.find(symbol.cppName);
    if (it != direct.end()) target = it->second;
    else target = glueFunction(symbol.cppName);
    if (target.empty() || symbol.variadic) unsupported("the builtin '" + symbol.name + "' as a value");
    for (const auto& p : symbol.parameters) {
        if (p.type.isUnknown()) unsupported("the builtin '" + symbol.name + "' as a value");
    }
    if (symbol.returnType.isUnknown()) unsupported("the builtin '" + symbol.name + "' as a value");
    std::string id;
    for (char c : symbol.cppName) id += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    const std::string fn = "dswb_" + id;
    if (!functionValues.count(fn)) {
        functionValues.insert(fn);
        std::string params, args;
        for (size_t i = 0; i < symbol.parameters.size(); ++i) {
            params += ", " + cType(symbol.parameters[i].type) + " a" + std::to_string(i);
            args += (i ? ", " : "") + std::string("a") + std::to_string(i);
        }
        protos << "static " << cType(symbol.returnType) << " " << fn << "(ds_fn* self" << params << ");\n";
        code << "static " << cType(symbol.returnType) << " " << fn << "(ds_fn* self" << params << ") {\n    (void)self;\n    "
             << (symbol.returnType.isVoidLike() ? "" : "return ") << target << "(" << args << ");\n}\n\n";
        data << "static ds_fn dsfv_" << fn << " = {{DS_FOREVER, &ds_fn_plain_type}, (void*)" << fn << "};\n";
    }
    return {"(&dsfv_" + fn + ")", false};
}

// (x[Int]) := x * 2: a C function plus a closure holding what it captured
CCodegen::CV CCodegen::lambda(const LambdaExpr* e) {
    const SemanticType& type = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    const SemanticType* expected = analyser.expectedTypeOf(e);
    const SemanticType fn = (expected && withoutNil(*expected).is(SemanticTypeKind::Function)) ? withoutNil(*expected) : type;
    SemanticType ret = fn.returnType ? *fn.returnType : simpleType(SemanticTypeKind::Emp);
    if (type.returnType && !type.returnType->isUnknown() && ret.isUnknown()) ret = *type.returnType;
    if (analyser.objParamsOf(e)) unsupported("Obj");

    const int id = lambdaCounter++;
    LambdaInfo info;
    info.id = id;
    info.selfName = recursiveName;
    recursiveName.clear();
    const std::string fnName = "dsl_" + std::to_string(id);
    const std::string envName = "dsenv_" + std::to_string(id);

    // Write the body on its own
    std::ostringstream body;
    std::ostringstream* savedOut = out;
    const int savedIndent = indentLevel;
    const auto savedReturn = returnType;
    const bool savedVolatile = volatileLocals;
    auto savedTemps = std::move(temps);
    temps.clear();
    out = &body;
    indentLevel = 1;
    returnType = ret;
    volatileLocals = e->body && e->body->kind != StmtKind::Expression && hasTry(e->body.get());
    Scope scope;
    scope.lambda = &info;
    scopes.push_back(scope);

    std::string sig = "static " + cType(ret) + " " + fnName + "(ds_fn* ds_self";
    std::vector<std::string> copies;
    for (size_t i = 0; i < e->params.size(); ++i) {
        const SemanticType& p = i < fn.parameterTypes.size() ? fn.parameterTypes[i] : (i < type.parameterTypes.size() ? type.parameterTypes[i] : kUnknown);
        const bool copy = volatileLocals || (e->body && assignsName(e->body.get(), e->params[i].name));
        sig += ", " + cType(p) + " " + mangle(e->params[i].name) + (copy ? "_in" : "");
        if (copy) {
            const bool counted = isCounted(p);
            std::string cname = declareLocal(e->params[i].name, p, counted);
            std::string value = mangle(e->params[i].name) + "_in";
            if (counted) value = "(" + cType(p) + ")ds_retain(" + value + ")";
            copies.push_back(cType(p) + (volatileLocals ? " volatile " : " ") + cname + " = " + value + ";");
        } else {
            scopes.back().locals.push_back({e->params[i].name, mangle(e->params[i].name), p, false});
        }
    }
    sig += ")";
    for (const auto& c : copies) line(c);
    if (e->body && e->body->kind == StmtKind::Expression) {
        const Expr* value = static_cast<const ExpressionStmt*>(e->body.get())->expression.get();
        beginTemps();
        std::string v;
        if (ret.isVoidLike()) {
            CV x = expr(value);
            v = discard(x, analyser.typeOf(value) ? *analyser.typeOf(value) : kUnknown);
        } else {
            v = own(exprAs(value, ret), ret);
        }
        auto ts = endTemps();
        declareTemps(ts);
        if (ret.isVoidLike()) {
            line(v + ";");
            releaseTemps(ts);
            releaseScopes(scopes.size() - 1, false);
        } else {
            line(cType(ret) + " ds_r = " + v + ";");
            releaseTemps(ts);
            releaseScopes(scopes.size() - 1, false);
            line("return ds_r;");
        }
    } else if (e->body) {
        for (const auto& s : static_cast<const BlockStmt*>(e->body.get())->statements) stmt(s.get());
        releaseScopes(scopes.size() - 1, false);
    }
    scopes.pop_back();

    std::string full = sig + " {\n";
    if (!info.captures.empty()) full += "    " + envName + "* ds_env = (" + envName + "*)ds_self;\n";
    full += "    (void)ds_self;\n" + body.str() + "}\n\n";

    out = savedOut;
    indentLevel = savedIndent;
    returnType = savedReturn;
    volatileLocals = savedVolatile;
    temps = std::move(savedTemps);

    // The closure: its captured values, how to free it
    structs << "typedef struct " << envName << " {\n    ds_fn base;\n";
    for (const auto& c : info.captures) structs << "    " << c.cType << " " << c.field << ";\n";
    structs << "} " << envName << ";\n";
    protos << "static " << cType(ret) << " " << fnName << "(ds_fn* ds_self";
    for (size_t i = 0; i < e->params.size(); ++i) {
        const SemanticType& p = i < fn.parameterTypes.size() ? fn.parameterTypes[i] : kUnknown;
        protos << ", " << cType(p);
    }
    protos << ");\n";
    protos << "static void dsd_" << envName << "(ds_head* h);\n";
    code << full;
    code << "static void dsd_" << envName << "(ds_head* h) {\n    " << envName << "* e = (" << envName << "*)h;\n    (void)e;\n";
    for (const auto& c : info.captures) {
        if (isCounted(c.type)) code << "    ds_release(e->" << c.field << ");\n";
    }
    code << "    ds_object_free(e, sizeof(" << envName << "));\n}\n";
    code << "static const ds_type dsty_" << envName << " = {\"Func\", DS_K_FN, dsd_" << envName << ", ds_fn_str, NULL, NULL, 0};\n\n";

    // Making it, where the lambda is written: copy the captured values
    std::string v = blockVar(tempCounter);
    std::string make = "({ " + envName + "* " + v + " = (" + envName + "*)ds_new_object(&dsty_" + envName + ", sizeof(" + envName + ")); " +
                       v + "->base.code = (void*)" + fnName + "; ";
    for (const auto& c : info.captures) {
        std::string source = resolveLocal(c.name);
        make += v + "->" + c.field + " = " + (isCounted(c.type) ? "(" + c.cType + ")ds_retain(" + source + ")" : source) + "; ";
    }
    make += "(ds_fn*)" + v + "; })";
    return {make, true};
}

CCodegen::CV CCodegen::newObject(const NewExpr* e) {
    if (e->isAnonymous) unsupported("anonymous objects (Obj)");
    const SemanticType& type = analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown;
    const ClassInfo* info = classOf(type);
    if (!info) unsupported("new " + type.toString());
    std::vector<FunctionParameterSymbol> params;
    if (const Symbol* ctor = nearestConstructor(*info)) params = ctor->parameters;
    std::string fn = "dsn_m" + std::to_string(info->moduleId) + "_" + info->name;
    return {callWith(fn, arguments(e, e->args, params)), true};
}

CCodegen::CV CCodegen::listLiteral(const std::vector<CollectionElement>& elements, const SemanticType& type) {
    // [] with nothing to say what it holds: any element kind will do
    if ((!type.elementType || type.elementType->isUnknown()) && elements.empty()) return {"ds_list_new(&ds_elem_ref, 0)", true};
    const SemanticType et = type.elementType ? *type.elementType : kUnknown;
    const std::string T = cType(et);
    const std::string E = elem(et);
    std::string l = blockVar(tempCounter);
    std::string code = "({ ds_list* " + l + " = ds_list_new(" + E + ", " + std::to_string(elements.size()) + "); ";
    for (const auto& el : elements) {
        if (el.isSpread) {
            const SemanticType& st = analyser.typeOf(el.value.get()) ? *analyser.typeOf(el.value.get()) : kUnknown;
            code += "ds_list_append_all(&" + l + ", " + use(expr(el.value.get()), st) + "); ";
        } else {
            std::string v = own(exprAs(el.value.get(), et), et);
            code += "{ " + T + " ds_v = " + v + "; if (" + l + "->len == " + l + "->cap) ds_list_grow(" + l + "); ((" + T + "*)" + l +
                    "->data)[" + l + "->len++] = ds_v; } ";
        }
    }
    code += l + "; })";
    return {code, true};
}

CCodegen::CV CCodegen::setLiteral(const std::vector<CollectionElement>& elements, const SemanticType& type) {
    const SemanticType et = type.elementType ? *type.elementType : kUnknown;
    const std::string T = cType(et);
    std::string l = blockVar(tempCounter);
    std::string code = "({ ds_list* " + l + " = ds_set_new(" + elem(et) + "); ";
    for (const auto& el : elements) {
        if (el.isSpread) {
            const SemanticType& st = analyser.typeOf(el.value.get()) ? *analyser.typeOf(el.value.get()) : kUnknown;
            code += "ds_set_add_all(&" + l + ", " + use(expr(el.value.get()), st) + "); ";
        } else {
            code += "{ " + T + " ds_v = " + own(exprAs(el.value.get(), et), et) + "; ds_set_add(&" + l + ", &ds_v); } ";
        }
    }
    code += l + "; })";
    return {code, true};
}

CCodegen::CV CCodegen::mapLiteral(const MapExpr* e, const SemanticType& type) {
    const SemanticType kt = type.keyType ? *type.keyType : kUnknown;
    const SemanticType vt = type.elementType ? *type.elementType : kUnknown;
    std::string m = blockVar(tempCounter);
    std::string code = "({ ds_map* " + m + " = ds_map_new(" + elem(kt) + ", " + elem(vt) + "); ";
    for (const auto& entry : e->entries) {
        if (!entry.key) {
            const SemanticType& st = analyser.typeOf(entry.value.get()) ? *analyser.typeOf(entry.value.get()) : kUnknown;
            code += "ds_map_put_all(&" + m + ", " + use(expr(entry.value.get()), st) + "); ";
            continue;
        }
        std::string k = own(exprAs(entry.key.get(), kt), kt);
        std::string v = own(exprAs(entry.value.get(), vt), vt);
        code += "{ " + cType(kt) + " ds_k = " + k + "; " + cType(vt) + " ds_v = " + v + "; ds_map_put(&" + m + ", &ds_k, &ds_v); } ";
    }
    code += m + "; })";
    return {code, true};
}

CCodegen::CV CCodegen::convertExpr(const ConvertExpr* e) {
    const SemanticType& from = analyser.typeOf(e->value.get()) ? *analyser.typeOf(e->value.get()) : kUnknown;
    const std::string& to = e->type.lexeme;
    const SemanticType f = withoutNil(from);
    if (f.is(SemanticTypeKind::Obj) || f.kind == SemanticTypeKind::Union) unsupported("converting " + from.toString());
    CV v = expr(e->value.get());
    std::string x = use(v, from);
    if (isOptValue(from)) x = "DS_UNWRAP(" + x + ")";
    if (to == "Str") return {toText({x, false}, from.containsNil() && !isOptValue(from) ? from : f), true};
    const bool text = f.is(SemanticTypeKind::Str);
    if (to == "Int") {
        if (text) return {"ds_str_to_int(" + x + ")", false};
        return {"((ds_int)(" + x + "))", false};
    }
    if (to == "Dbl") {
        if (text) return {"ds_str_to_dbl(" + x + ", \"Dbl\")", false};
        return {"((ds_dbl)(" + x + "))", false};
    }
    if (to == "Flt") {
        if (text) return {"((ds_flt)ds_str_to_dbl(" + x + ", \"Flt\"))", false};
        return {"((ds_flt)(" + x + "))", false};
    }
    if (to == "Bul") {
        if (text) return {"ds_str_to_bul(" + x + ")", false};
        return {"((" + x + ") != 0)", false};
    }
    if (to == "Chr") {
        if (text) return {"ds_str_to_chr(" + x + ")", false};
        if (f.is(SemanticTypeKind::Chr)) return {x, false};
        return {"ds_int_to_chr((ds_int)(" + x + "))", false};
    }
    return v;
}

CCodegen::CV CCodegen::isExpr(const IsExpr* e) {
    const SemanticType* target = analyser.isTargetOf(e);
    if (!target) return {"false", false};
    const SemanticType& vt = analyser.typeOf(e->value.get()) ? *analyser.typeOf(e->value.get()) : kUnknown;
    if (target->is(SemanticTypeKind::Obj) || vt.is(SemanticTypeKind::Obj)) unsupported("Obj");
    std::string value = use(expr(e->value.get()), vt);
    if (target->isClass()) {
        if (!isCounted(vt)) return {"((void)(" + value + "), false)", false};
        return {"ds_is_class(" + value + ", &" + clsName(*target) + ".t)", false};
    }
    if (target->isInterface()) {
        auto it = interfaceIds.find(target->key());
        if (it == interfaceIds.end()) return {"((void)(" + value + "), false)", false};
        return {"(ds_interface(" + value + ", " + std::to_string(it->second) + ") != NULL)", false};
    }
    const SemanticType base = withoutNil(vt);
    if (base.kind == SemanticTypeKind::Union) unsupported("union types");
    if (isOptValue(vt)) return {base == *target ? "(" + value + ").has" : "((void)(" + value + "), false)", false};
    if (isCounted(vt) && vt.containsNil()) return {base == *target ? "((" + value + ") != NULL)" : "((void)(" + value + "), false)", false};
    return {base == *target ? "((void)(" + value + "), true)" : "((void)(" + value + "), false)", false};
}

} // namespace dessa
