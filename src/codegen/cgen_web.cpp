// The C backend, for WebAssembly: webutils' page objects (Element, Event,
// Document, ...) and functions (alert, log, setTimeout, ...).
//
// They live in JavaScript. A page object is a counted ds_js holding its
// number in the bridge's table (runtime/dessa_web.h, runtime/dessa_wasm.js);
// a field read, a field set and a call push their values to the bridge and
// take the result back:
//
//     el.textContent = "Hi"     ({ void* o = el; ds_str* v = "Hi"; dsw_push_str(v); dsw_set(o, "textContent"); v; })
//     document.getElementById("app")
//                               ({ void* o = document; ds_str* a0 = ...; dsw_push_str(a0); dsw_call(o, "getElementById");
//                                  (dsc_Element*)dsw_take_obj(&dsk_Element.t); })
#include "cgen.hpp"

namespace dessa {

namespace {

const SemanticType kUnknown = SemanticType::unknown();

bool isFunctionStub(const Stmt* body) {
    if (!body || body->kind != StmtKind::Block) return false;
    const auto& statements = static_cast<const BlockStmt*>(body)->statements;
    if (statements.size() != 1 || !statements[0] || statements[0]->kind != StmtKind::Expression) return false;
    const Expr* e = static_cast<const ExpressionStmt*>(statements[0].get())->expression.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS;
}

std::string cText(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

// A class of a library with a JavaScript part (webutils) whose methods are
// all `{ ... }` is a page object; a function of it with a `{ ... }` body is
// done by the bridge
void CCodegen::collectExtern() {
    for (const auto& module : program.modules) {
        if (module->jsNative.empty()) continue;
        for (const auto& s : module->ast) {
            if (!s) continue;
            if (s->kind == StmtKind::FuncDecl) {
                const auto* f = static_cast<const FuncDeclStmt*>(s.get());
                if (isFunctionStub(f->body.get())) stubFunctions.insert(std::to_string(module->id) + ":" + f->name);
            } else if (s->kind == StmtKind::ClassDecl) {
                const auto* decl = static_cast<const ClassDeclStmt*>(s.get());
                bool allStubs = true;
                for (const auto& m : decl->members) {
                    if (m && m->kind == StmtKind::FuncDecl && !isFunctionStub(static_cast<const FuncDeclStmt*>(m.get())->body.get())) allStubs = false;
                }
                if (allStubs) externClasses.insert(SemanticType::userDefined(decl->name, module->id, UserTypeKind::Class).key());
            }
        }
    }
}

bool CCodegen::isExtern(const SemanticType& type) const {
    const SemanticType t = withoutNil(type);
    return t.kind == SemanticTypeKind::UserDefined && externClasses.count(t.key()) != 0;
}

bool CCodegen::isStubFunction(int moduleId, const std::string& name) const {
    return stubFunctions.count(std::to_string(moduleId) + ":" + name) != 0;
}

std::string CCodegen::externClass(const SemanticType& type) const {
    return "&" + clsName(withoutNil(type)) + ".t";
}

// struct: a ds_js; the description: its name, and how it's dropped (the
// bridge forgets the object)
void CCodegen::emitExternClass(const ClassInfo& info) {
    const SemanticType t = info.type();
    const std::string s = structName(t);
    structs << "typedef struct " << s << "_cls {\n    ds_type t;\n} " << s << "_cls;\n";
    structs << "static const " << s << "_cls " << clsName(t) << " = {DSW_CLASS(" << cText(info.name) << ")};\n";
}

// One value to the bridge
std::string CCodegen::externPush(const std::string& value, const SemanticType& type) {
    const SemanticType t = withoutNil(type);
    if (t.is(SemanticTypeKind::Str)) return "dsw_push_str(" + value + ");";
    if (type.containsNil() && isOptValue(type)) {
        return "if (" + value + ".has) " + externPush(value + ".value", t) + " else dsw_push_obj(NULL);";
    }
    if (t.is(SemanticTypeKind::Int) || t.is(SemanticTypeKind::Dbl) || t.is(SemanticTypeKind::Flt)) return "dsw_push_num((double)(" + value + "));";
    if (t.is(SemanticTypeKind::Bul)) return "dsw_push_bool(" + value + ");";
    if (isExtern(t)) return "dsw_push_obj(" + value + ");";
    if (t.is(SemanticTypeKind::List) && t.elementType && t.elementType->is(SemanticTypeKind::Str)) return "dsw_push_strs(" + value + ");";
    if (t.is(SemanticTypeKind::Function)) {
        if (t.parameterTypes.empty()) return "dsw_push_fn(" + value + ", 0, NULL);";
        if (t.parameterTypes.size() == 1 && isExtern(t.parameterTypes[0])) {
            return "dsw_push_fn(" + value + ", 1, " + externClass(t.parameterTypes[0]) + ");";
        }
    }
    unsupported("passing " + type.toString() + " to the page");
}

// The bridge's last result as a value of `type` (owned when counted)
std::string CCodegen::externTake(const SemanticType& type) {
    const SemanticType t = withoutNil(type);
    switch (t.kind) {
        case SemanticTypeKind::Emp: return "(void)0";
        case SemanticTypeKind::Str: return type.containsNil() ? "dsw_take_str()" : "dsw_take_text()";
        case SemanticTypeKind::Int: return "(ds_int)dsw_take_num()";
        case SemanticTypeKind::Dbl: return "dsw_take_num()";
        case SemanticTypeKind::Flt: return "(ds_flt)dsw_take_num()";
        case SemanticTypeKind::Bul: return "dsw_take_bool()";
        default: break;
    }
    if (isOptValue(type)) unsupported("taking " + type.toString() + " from the page");
    if (isExtern(t)) return "(" + cType(t) + ")dsw_take_obj(" + externClass(t) + ")";
    if (t.is(SemanticTypeKind::List) && t.elementType && isExtern(*t.elementType)) return "dsw_take_objs(" + externClass(*t.elementType) + ")";
    unsupported("taking " + type.toString() + " from the page");
}

// object.name(args...) (object "NULL": a webutils function). The arguments
// are worked out first, then pushed: an argument may call the page itself
CCodegen::CV CCodegen::externCall(const std::string& object, const std::string& name, const CallArgs& args,
                                  const std::vector<FunctionParameterSymbol>& params, const SemanticType& result) {
    const std::string o = "ds_o" + std::to_string(tempCounter++);
    std::string code = "({ " + args.prelude + "void* " + o + " = " + object + "; ";
    // the values are in the parameters' order (a variadic one is its list)
    std::vector<std::string> values;
    for (size_t p = 0; p < args.values.size(); ++p) {
        const SemanticType& type = p < params.size() ? params[p].type : kUnknown;
        const std::string v = "ds_a" + std::to_string(tempCounter++);
        code += cType(type) + " " + v + " = " + args.values[p] + "; ";
        values.push_back(externPush(v, type));
    }
    for (const auto& push : values) code += push + " ";
    code += "dsw_call(" + o + ", " + cText(name) + "); " + externTake(result) + "; })";
    return {code, isCounted(result)};
}

// object.field
CCodegen::CV CCodegen::externGet(const MemberExpr* e, const SemanticType& type) {
    const Expr* object = e->object.get();
    const SemanticType& ot = analyser.typeOf(object) ? *analyser.typeOf(object) : kUnknown;
    const std::string o = "ds_o" + std::to_string(tempCounter++);
    return {"({ void* " + o + " = ds_ref(" + use(expr(object), ot) + "); dsw_get(" + o + ", " + cText(e->name.lexeme) + "); " +
                externTake(type) + "; })",
            isCounted(type)};
}

// object.field = value, += and -=
CCodegen::CV CCodegen::externAssign(const AssignExpr* e) {
    const auto* m = static_cast<const MemberExpr*>(e->target.get());
    const Symbol* field = analyser.memberOf(m);
    if (!field || field->kind != SymbolKind::Field) unsupported("assigning this member");
    const SemanticType& type = field->type;
    const SemanticType base = withoutNil(type);
    const SemanticType& ot = analyser.typeOf(m->object.get()) ? *analyser.typeOf(m->object.get()) : kUnknown;
    const std::string o = "ds_o" + std::to_string(tempCounter++);
    const std::string v = "ds_v" + std::to_string(tempCounter++);
    const std::string name = cText(m->name.lexeme);
    std::string code = "({ void* " + o + " = ds_ref(" + use(expr(m->object.get()), ot) + "); ";
    if (e->op.type == TokenType::EQUAL) {
        code += cType(type) + " " + v + " = " + own(exprAs(e->value.get(), type), type) + "; ";
    } else if (e->op.type == TokenType::PLUS_EQUAL || e->op.type == TokenType::MINUS_EQUAL) {
        const std::string now = "ds_n" + std::to_string(tempCounter++);
        code += "dsw_get(" + o + ", " + name + "); " + cType(base) + " " + now + " = " + externTake(base) + "; ";
        const bool minus = e->op.type == TokenType::MINUS_EQUAL;
        if (base.is(SemanticTypeKind::Str) && !minus) {
            const std::string add = use(exprAs(e->value.get(), base), base);
            code += "ds_str* " + v + " = ds_str_cat(" + now + ", " + add + "); ds_release(" + now + "); ";
        } else if (base.is(SemanticTypeKind::Int)) {
            const std::string add = use(exprAs(e->value.get(), base), base);
            code += "ds_int " + v + " = " + (minus ? "ds_sub(" : "ds_add(") + now + ", " + add + "); ";
        } else if (base.is(SemanticTypeKind::Dbl) || base.is(SemanticTypeKind::Flt)) {
            const std::string add = use(exprAs(e->value.get(), base), base);
            code += cType(base) + " " + v + " = " + now + (minus ? " - " : " + ") + add + "; ";
        } else {
            unsupported("the operator " + e->op.lexeme + " on " + type.toString());
        }
    } else {
        unsupported("the operator " + e->op.lexeme + " on a page object");
    }
    code += externPush(v, type) + " dsw_set(" + o + ", " + name + "); " + v + "; })";
    return {code, isCounted(type)};
}

} // namespace dessa
