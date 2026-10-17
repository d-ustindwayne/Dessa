#include "jsgen.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace dessa {

namespace {

const SemanticType kUnknown = SemanticType::unknown();

const Expr* unwrapGrouping(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e;
}

bool isNilLiteral(const Expr* e) {
    e = unwrapGrouping(e);
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::NIL;
}

bool isSelf(const Expr* e) {
    return e && e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type == TokenType::THIS;
}

bool isSuper(const Expr* e) {
    return e && e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type == TokenType::SUPER;
}

bool isSuperInitCall(const Stmt* s) {
    if (!s || s->kind != StmtKind::Expression) return false;
    const Expr* e = static_cast<const ExpressionStmt*>(s)->expression.get();
    if (!e || e->kind != ExprKind::Call) return false;
    const Expr* callee = static_cast<const CallExpr*>(e)->callee.get();
    if (!callee || callee->kind != ExprKind::Member) return false;
    const auto* m = static_cast<const MemberExpr*>(callee);
    return m->name.type == TokenType::INIT && isSuper(m->object.get());
}

// `{ ... }`: a body that is only a placeholder (declaration files, JS-native libraries)
bool isStub(const Stmt* body) {
    if (!body || body->kind != StmtKind::Block) return false;
    const auto& statements = static_cast<const BlockStmt*>(body)->statements;
    if (statements.size() != 1 || !statements[0] || statements[0]->kind != StmtKind::Expression) return false;
    const Expr* e = static_cast<const ExpressionStmt*>(statements[0].get())->expression.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS;
}

bool containsUnknown(const SemanticType& t) {
    if (t.isUnknown()) return true;
    if (t.elementType && containsUnknown(*t.elementType)) return true;
    if (t.returnType && containsUnknown(*t.returnType)) return true;
    for (const auto& m : t.unionMembers) if (containsUnknown(m)) return true;
    for (const auto& p : t.parameterTypes) if (containsUnknown(p)) return true;
    return false;
}

} // namespace

JsCodegen::JsCodegen(const Program& program, const Analyser& analyser) : program(program), analyser(analyser) {}

// ============================================================
// Output helpers
// ============================================================

void JsCodegen::line(const std::string& text) {
    if (text.empty()) {
        out << "\n";
        return;
    }
    out << std::string(static_cast<size_t>(indentLevel) * 4, ' ') << text << "\n";
}

std::string JsCodegen::temp(const std::string& base) {
    return "$" + base + std::to_string(tempCounter++);
}

const SemanticType& JsCodegen::typeOf(const Expr* e) const {
    const SemanticType* t = analyser.typeOf(e);
    return t ? *t : kUnknown;
}

// ============================================================
// Names
// ============================================================

std::string JsCodegen::jsName(const std::string& name) {
    static const std::unordered_set<std::string> reserved = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
        "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof",
        "new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void",
        "while", "with", "yield", "let", "static", "implements", "interface", "package", "private", "protected",
        "public", "await", "async", "arguments", "eval", "undefined", "NaN", "Infinity", "of",
        // globals the generated code and its runtime rely on
        "Object", "Array", "Map", "Set", "String", "Number", "Math", "JSON", "Symbol", "BigInt", "Date",
        "Promise", "Error", "console", "window", "document", "globalThis", "process", "require", "module",
        "Boolean", "RegExp", "Intl", "parseInt", "parseFloat", "isNaN", "isFinite", "performance", "Atomics",
        "SharedArrayBuffer", "Buffer", "Int32Array", "WeakMap", "Reflect", "Proxy",
    };
    if (reserved.count(name)) return name + "_";
    return name;
}

std::string JsCodegen::memberName(const std::string& name) {
    static const std::unordered_set<std::string> reserved = {
        "constructor", "__proto__", "prototype", "toString", "valueOf", "hasOwnProperty", "isPrototypeOf",
        "propertyIsEnumerable", "toLocaleString",
    };
    if (reserved.count(name)) return name + "_";
    return name;
}

std::string JsCodegen::jsString(const std::string& text) {
    std::string s = "\"";
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '"': s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\x%02x", c);
                    s += buf;
                } else {
                    s += static_cast<char>(c);   // UTF-8 bytes pass through; the file is UTF-8
                }
        }
    }
    // U+2028 / U+2029 end lines in old JS engines
    std::string fixed;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xE2 && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(s[i + 2]) == 0xA8 || static_cast<unsigned char>(s[i + 2]) == 0xA9)) {
            fixed += static_cast<unsigned char>(s[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            fixed += s[i];
        }
    }
    return fixed + "\"";
}

std::string JsCodegen::moduleName(int id, const std::string& name) const {
    return "m" + std::to_string(id) + "$" + name;
}

std::string JsCodegen::globalName(const Symbol& symbol) const {
    if (!symbol.cppName.empty()) {
        // dessa::print -> $.print, dessa::math::sqrt -> $.math.sqrt
        std::string name = symbol.cppName;
        if (name.rfind("dessa::", 0) == 0) name = "$." + name.substr(7);
        size_t at;
        while ((at = name.find("::")) != std::string::npos) name.replace(at, 2, ".");
        return name;
    }
    const std::string& name = symbol.originalName.empty() ? symbol.name : symbol.originalName;
    return moduleName(symbol.moduleId, name);
}

std::string JsCodegen::className(const SemanticType& type) const {
    return moduleName(type.moduleId, type.name);
}

// ============================================================
// Types
// ============================================================

bool JsCodegen::isExtern(const SemanticType& type) const {
    SemanticType base = withoutNil(type);
    return base.isClass() && externClasses.count(base.key()) != 0;
}

bool JsCodegen::isExternMember(const Symbol* symbol) const {
    return symbol && !symbol->ownerClass.empty() && externClasses.count(symbol->ownerClass) != 0;
}

bool JsCodegen::isNumber(const SemanticType& type) {
    return type.is(SemanticTypeKind::Dbl) || type.is(SemanticTypeKind::Flt);
}

bool JsCodegen::isPrimitive(const SemanticType& type) {
    switch (type.kind) {
        case SemanticTypeKind::Int: case SemanticTypeKind::Dbl: case SemanticTypeKind::Flt: case SemanticTypeKind::Str:
        case SemanticTypeKind::Bul: case SemanticTypeKind::Chr: case SemanticTypeKind::Function:
            return true;
        case SemanticTypeKind::UserDefined:
            return true;   // classes compare by identity, enums are interned
        default:
            return false;
    }
}

bool JsCodegen::needsCopy(const SemanticType& type) {
    switch (type.kind) {
        case SemanticTypeKind::List: case SemanticTypeKind::Set: case SemanticTypeKind::Map: return true;
        case SemanticTypeKind::Union:
            for (const auto& m : type.unionMembers) if (needsCopy(m)) return true;
            return false;
        default: return false;
    }
}

std::string JsCodegen::defaultValue(const SemanticType& type) const {
    switch (type.kind) {
        case SemanticTypeKind::Int: return "0n";
        case SemanticTypeKind::Dbl: case SemanticTypeKind::Flt: return "0";
        case SemanticTypeKind::Str: return "\"\"";
        case SemanticTypeKind::Bul: return "false";
        case SemanticTypeKind::Chr: return "$.chr(0)";
        case SemanticTypeKind::List: return "[]";
        case SemanticTypeKind::Set: return "new $.DsSet()";
        case SemanticTypeKind::Map: return "new Map()";
        case SemanticTypeKind::UserDefined:
            if (type.isEnum()) return className(type) + ".$values[0]";
            return "null";
        default: return "null";
    }
}

// Where the Flt numbers are inside a value of this type (see $.fview), "" if nowhere.
std::string JsCodegen::fltShape(const SemanticType& type) const {
    switch (type.kind) {
        case SemanticTypeKind::Flt: return "\"f\"";
        case SemanticTypeKind::List: case SemanticTypeKind::Set: {
            std::string inner = type.elementType ? fltShape(*type.elementType) : "";
            if (inner.empty()) return "";
            return std::string("[\"") + (type.kind == SemanticTypeKind::List ? "l" : "s") + "\", " + inner + "]";
        }
        case SemanticTypeKind::Map: {
            std::string k = type.keyType ? fltShape(*type.keyType) : "";
            std::string v = type.elementType ? fltShape(*type.elementType) : "";
            if (k.empty() && v.empty()) return "";
            return "[\"m\", " + (k.empty() ? std::string("null") : k) + ", " + (v.empty() ? std::string("null") : v) + "]";
        }
        case SemanticTypeKind::Union: {
            bool hasDbl = false;
            for (const auto& m : type.unionMembers) if (m.is(SemanticTypeKind::Dbl)) hasDbl = true;
            std::string members;
            for (const auto& m : type.unionMembers) {
                if (m.is(SemanticTypeKind::Flt) && hasDbl) continue;   // can't tell them apart: show as Dbl
                std::string s = fltShape(m);
                if (!s.empty()) members += ", " + s;
            }
            if (members.empty()) return "";
            return "[\"u\"" + members + "]";
        }
        default: return "";
    }
}

// A value of type `from` used where `to` is expected: Int <-> number, Flt rounding.
std::string JsCodegen::convert(const std::string& code, const SemanticType& from, const SemanticType& to) const {
    SemanticType target = withoutNil(to);
    if (target.kind == SemanticTypeKind::Union || from.isUnknown() || target.isUnknown()) return code;
    if (from.is(SemanticTypeKind::Int) && isNumber(target)) {
        return target.is(SemanticTypeKind::Flt) ? "Math.fround(Number(" + code + "))" : "Number(" + code + ")";
    }
    if (isNumber(from) && target.is(SemanticTypeKind::Int)) return "$.trunc(" + code + ")";
    if (from.is(SemanticTypeKind::Dbl) && target.is(SemanticTypeKind::Flt)) return "Math.fround(" + code + ")";
    return code;
}

// ============================================================
// Program
// ============================================================

void JsCodegen::collectExternClasses() {
    // In a library with a JavaScript native part, a class whose methods are
    // all `{ ... }` describes an object that comes from JavaScript (a DOM
    // Element, an Event, ...): no class is generated and its members are used
    // as they are.
    for (const auto& module : program.modules) {
        if (module->jsNative.empty()) continue;
        for (const auto& s : module->ast) {
            if (!s || s->kind != StmtKind::ClassDecl) continue;
            const auto* decl = static_cast<const ClassDeclStmt*>(s.get());
            bool allStubs = true;
            for (const auto& m : decl->members) {
                if (m && m->kind == StmtKind::FuncDecl && !isStub(static_cast<const FuncDeclStmt*>(m.get())->body.get())) allStubs = false;
            }
            if (allStubs) externClasses.insert(SemanticType::userDefined(decl->name, module->id, UserTypeKind::Class).key());
        }
    }
}

std::string JsCodegen::generate(const std::string& runtime) {
    out.str("");
    collectExternClasses();
    line("// Generated by the Dessa compiler (JavaScript). Do not edit.");
    line("(function ($hooks) {");
    out << runtime << "\n";
    line("if ($hooks) Object.assign($, $hooks);");
    // JavaScript native parts of libraries (webutils.js)
    std::set<std::string> natives;
    for (const auto& module : program.modules) {
        if (module->jsNative.empty() || !natives.insert(module->jsNative).second) continue;
        std::ifstream in(module->jsNative, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        out << text.str() << "\n";
    }

    for (const auto& module : program.modules) {
        line("const " + fileVar(module->id) + " = " + jsString(module->displayPath) + ";");
        emitEnums(*module);
    }
    for (const auto& key : analyser.classOrder()) {
        if (externClasses.count(key)) continue;
        auto colon = key.find(':');
        SemanticType t = SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class);
        if (const ClassInfo* info = analyser.findClass(t)) emitClass(*info);
    }
    for (const auto& module : program.modules) emitGlobals(*module);
    for (const auto& module : program.modules) emitModuleEntry(*module);

    line("if (typeof process !== \"undefined\" && process.argv) $.setArgs(process.argv.slice(2));");
    std::string calls;
    for (const auto& module : program.modules) {
        calls += moduleName(module->id, module->isEntry ? "$run" : "$init") + "(); ";
    }
    line("return $.run(() => { " + calls + "});");
    line("})(typeof globalThis !== \"undefined\" ? globalThis.$dessaHooks : undefined);");
    return out.str();
}

void JsCodegen::emitEnums(const Module& module) {
    moduleId = module.id;
    for (const auto& s : module.ast) {
        if (!s || s->kind != StmtKind::EnumDecl) continue;
        const auto* decl = static_cast<const EnumDeclStmt*>(s.get());
        std::string entries;
        for (const auto& v : decl->values) {
            entries += (entries.empty() ? "" : ", ") + std::string("[") + jsString(v.name) + ", " +
                       (v.value ? exprAs(v.value->get(), SemanticType::simple(SemanticTypeKind::Int)) : "null") + "]";
        }
        line("const " + moduleName(module.id, decl->name) + " = $.enumType(" + jsString(decl->name) + ", [" + entries + "]);");
    }
}

const Symbol* JsCodegen::nearestConstructor(const ClassInfo& info) const {
    for (const ClassInfo* k = &info; k; k = k->parent ? analyser.findClass(*k->parent) : nullptr) {
        if (k->constructor) return &*k->constructor;
    }
    return nullptr;
}

std::string JsCodegen::params(const Symbol& symbol) const {
    std::string s;
    for (size_t i = 0; i < symbol.parameters.size(); ++i) s += (i ? ", " : "") + jsName(symbol.parameters[i].name);
    return s;
}

void JsCodegen::emitClass(const ClassInfo& info) {
    const ClassInfo* previous = currentClass;
    currentClass = &info;
    moduleId = info.moduleId;
    const std::string name = className(info.type());
    const std::string base = info.parent && !isExtern(*info.parent) ? className(*info.parent) : "$.Obj";
    line("class " + name + " extends " + base + " {");
    indentLevel++;

    // Fields: their initial values, set at the start of every constructor
    line("$fields_" + info.name + "() {");
    indentLevel++;
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::VarDecl) continue;
        const auto* field = static_cast<const VarDeclStmt*>(member.get());
        if (!field->name) continue;
        const SemanticType& type = info.fields.at(*field->name).type;
        line("this." + memberName(*field->name) + " = " +
             (field->initializer ? exprAs(field->initializer.get(), type) : defaultValue(type)) + ";");
    }
    indentLevel--;
    line("}");

    // Constructor: parent's init, then the field initialisers, then init's body
    const FuncDeclStmt* ctor = nullptr;
    for (const auto& member : info.decl->members) {
        if (member && member->kind == StmtKind::FuncDecl && static_cast<const FuncDeclStmt*>(member.get())->isConstructor) {
            ctor = static_cast<const FuncDeclStmt*>(member.get());
        }
    }
    const Symbol* ctorSymbol = ctor ? analyser.functionOf(ctor) : nearestConstructor(info);
    returnType = SemanticType::simple(SemanticTypeKind::Emp);
    line("$construct_" + info.name + "(" + (ctorSymbol ? params(*ctorSymbol) : "") + ") {");
    indentLevel++;
    const auto* body = ctor ? static_cast<const BlockStmt*>(ctor->body.get()) : nullptr;
    size_t first = 0;
    if (info.parent && !isExtern(*info.parent)) {
        const std::string parentConstruct = "super.$construct_" + info.parent->name;
        if (body && !body->statements.empty() && isSuperInitCall(body->statements.front().get())) {
            const auto* callExpr = static_cast<const CallExpr*>(static_cast<const ExpressionStmt*>(body->statements.front().get())->expression.get());
            const Symbol* parentCtor = analyser.memberOf(callExpr->callee.get());
            std::vector<FunctionParameterSymbol> parentParams = parentCtor ? parentCtor->parameters : std::vector<FunctionParameterSymbol>{};
            line(parentConstruct + "(" + arguments(callExpr, callExpr->args, parentParams) + ");");
            first = 1;
        } else if (!ctor && ctorSymbol) {
            line(parentConstruct + "(" + params(*ctorSymbol) + ");");
        } else {
            line(parentConstruct + "();");
        }
    }
    line("this.$fields_" + info.name + "();");
    if (body) {
        for (size_t i = first; i < body->statements.size(); ++i) stmt(body->statements[i].get());
    }
    line("return this;");
    indentLevel--;
    line("}");
    returnType.reset();

    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::FuncDecl) continue;
        const auto* method = static_cast<const FuncDeclStmt*>(member.get());
        if (method->isConstructor) continue;
        emitFunction(method, memberName(method->name), true);
    }

    // How print shows an instance: toStr() if the class has one, otherwise ClassName(field: value, ...)
    const ClassInfo* withToStr = nullptr;
    std::vector<const ClassInfo*> chain;
    for (const ClassInfo* k = &info; k; k = k->parent ? analyser.findClass(*k->parent) : nullptr) {
        chain.push_back(k);
        auto it = k->methods.find("toStr");
        if (!withToStr && it != k->methods.end() && it->second.parameters.empty() && it->second.returnType.is(SemanticTypeKind::Str)) {
            withToStr = k;
        }
    }
    if (withToStr) {
        line("$str() { return this." + memberName("toStr") + "(); }");
    } else {
        std::string parts;
        bool firstField = true;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (const auto& f : (*it)->fieldOrder) {
                std::string value = "this." + memberName(f);
                std::string shape = fltShape((*it)->fields.at(f).type);
                if (!shape.empty()) value = "$.fview(" + value + ", " + shape + ")";
                parts += std::string(firstField ? "" : " + \", \"") + " + " + jsString(f + ": ") + " + $.repr(" + value + ")";
                firstField = false;
            }
        }
        line("$str() { return " + jsString(info.name + "(") + parts + " + \")\"; }");
    }
    indentLevel--;
    line("}");
    line(name + ".prototype.$class = " + jsString(info.name) + ";");
    std::string ifaces = base + ".prototype.$ifaces";
    if (info.interface) ifaces = "[..." + ifaces + ", " + jsString(info.interface->key()) + "]";
    line(name + ".prototype.$ifaces = " + ifaces + ";");
    line("");
    currentClass = previous;
}

void JsCodegen::emitGlobals(const Module& module) {
    moduleId = module.id;
    for (const auto& s : module.ast) {
        if (!s) continue;
        if (s->kind == StmtKind::VarDecl) {
            const auto* decl = static_cast<const VarDeclStmt*>(s.get());
            const SemanticType* type = analyser.declaredTypeOf(decl);
            if (decl->name && type) line("let " + moduleName(module.id, *decl->name) + " = " + defaultValue(*type) + ";");
        } else if (s->kind == StmtKind::FuncDecl) {
            const auto* decl = static_cast<const FuncDeclStmt*>(s.get());
            if (!module.jsNative.empty() && isStub(decl->body.get())) {
                // Implemented in the library's .js part
                std::string lib = module.jsNative;
                size_t slash = lib.find_last_of("/\\");
                lib = lib.substr(slash == std::string::npos ? 0 : slash + 1);
                lib = lib.substr(0, lib.size() - 3);
                line("const " + moduleName(module.id, decl->name) + " = (...a) => $." + lib + "." + decl->name + "(...a);");
                continue;
            }
            emitFunction(decl, moduleName(module.id, decl->name), false);
        }
    }
}

void JsCodegen::emitFunction(const FuncDeclStmt* decl, const std::string& name, bool isMethod) {
    const Symbol* symbol = analyser.functionOf(decl);
    if (!symbol || !decl->body) return;
    returnType = symbol->returnType;
    line((isMethod ? "" : "function ") + name + "(" + params(*symbol) + ") {");
    indentLevel++;
    for (const auto& s : static_cast<const BlockStmt*>(decl->body.get())->statements) stmt(s.get());
    indentLevel--;
    line("}");
    returnType.reset();
}

void JsCodegen::emitModuleEntry(const Module& module) {
    moduleId = module.id;
    line("function " + moduleName(module.id, module.isEntry ? "$run" : "$init") + "() {");
    indentLevel++;
    for (const auto& s : module.ast) {
        if (!s) continue;
        switch (s->kind) {
            case StmtKind::FuncDecl: case StmtKind::ClassDecl: case StmtKind::InterfaceDecl:
            case StmtKind::EnumDecl: case StmtKind::Import:
                break;
            case StmtKind::VarDecl:
                if (static_cast<const VarDeclStmt*>(s.get())->initializer && s->line > 0) {
                    line("$.trace(" + fileVar(moduleId) + ", " + std::to_string(s->line) + ");");
                }
                varDecl(static_cast<const VarDeclStmt*>(s.get()), true);
                break;
            default:
                if (module.isEntry) stmt(s.get());   // only the entry file's top-level code runs
                break;
        }
    }
    indentLevel--;
    line("}");
}

// ============================================================
// Statements
// ============================================================

void JsCodegen::block(const Stmt* statement) {
    if (statement && statement->kind == StmtKind::Block) {
        for (const auto& s : static_cast<const BlockStmt*>(statement)->statements) stmt(s.get());
    } else {
        stmt(statement);
    }
}

void JsCodegen::varDecl(const VarDeclStmt* decl, bool asGlobalAssignment) {
    if (!decl->name) return;
    const SemanticType* declared = analyser.declaredTypeOf(decl);
    if (!declared) return;
    if (asGlobalAssignment) {
        if (decl->initializer) line(moduleName(moduleId, *decl->name) + " = " + exprAs(decl->initializer.get(), *declared) + ";");
        return;
    }
    const std::string name = jsName(*decl->name);
    line("let " + name + " = " + (decl->initializer ? exprAs(decl->initializer.get(), *declared) : defaultValue(*declared)) + ";");
}

void JsCodegen::stmt(const Stmt* statement) {
    if (!statement) return;
    switch (statement->kind) {
        case StmtKind::Block: case StmtKind::FuncDecl: case StmtKind::ClassDecl: case StmtKind::InterfaceDecl:
        case StmtKind::EnumDecl: case StmtKind::Import: case StmtKind::Break: case StmtKind::Continue:
            break;
        case StmtKind::VarDecl:
            if (!static_cast<const VarDeclStmt*>(statement)->initializer) break;
            [[fallthrough]];
        default:
            if (statement->line > 0) line("$.trace(" + fileVar(moduleId) + ", " + std::to_string(statement->line) + ");");
    }
    switch (statement->kind) {
        case StmtKind::Expression: {
            const Expr* e = static_cast<const ExpressionStmt*>(statement)->expression.get();
            if (e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS) {
                line("$.fail(\"Not implemented yet (line " + std::to_string(statement->line) + ")\");");
                return;
            }
            if (isSuperInitCall(statement)) return;   // done by the constructor
            line(expr(e) + ";");
            return;
        }
        case StmtKind::VarDecl:
            varDecl(static_cast<const VarDeclStmt*>(statement), false);
            return;
        case StmtKind::If: {
            const auto* s = static_cast<const IfStmt*>(statement);
            line("if (" + expr(s->condition.get()) + ") {");
            indentLevel++;
            block(s->thenBranch.get());
            indentLevel--;
            const Stmt* elseBranch = s->elseBranch.get();
            while (elseBranch && elseBranch->kind == StmtKind::If) {
                const auto* elif = static_cast<const IfStmt*>(elseBranch);
                line("} else if (" + expr(elif->condition.get()) + ") {");
                indentLevel++;
                block(elif->thenBranch.get());
                indentLevel--;
                elseBranch = elif->elseBranch.get();
            }
            if (elseBranch) {
                line("} else {");
                indentLevel++;
                block(elseBranch);
                indentLevel--;
            }
            line("}");
            return;
        }
        case StmtKind::Switch:
            switchStmt(static_cast<const SwitchStmt*>(statement));
            return;
        case StmtKind::For: {
            const auto* s = static_cast<const ForStmt*>(statement);
            std::string init;
            if (s->init) {
                if (s->init->kind == StmtKind::VarDecl) {
                    const auto* d = static_cast<const VarDeclStmt*>(s->init.get());
                    const SemanticType* t = analyser.declaredTypeOf(d);
                    init = "let " + jsName(d->name.value_or("$i")) + " = " +
                           (d->initializer ? exprAs(d->initializer.get(), t ? *t : kUnknown) : defaultValue(t ? *t : kUnknown));
                } else if (s->init->kind == StmtKind::Expression) {
                    init = expr(static_cast<const ExpressionStmt*>(s->init.get())->expression.get());
                }
            }
            std::string cond = s->condition ? expr(s->condition.get()) : "";
            std::string inc = s->increment ? expr(s->increment.get()) : "";
            line("for (" + init + "; " + cond + "; " + inc + ") {");
            indentLevel++;
            block(s->body.get());
            indentLevel--;
            line("}");
            return;
        }
        case StmtKind::ForIn:
            forIn(static_cast<const ForInStmt*>(statement));
            return;
        case StmtKind::While: {
            const auto* s = static_cast<const WhileStmt*>(statement);
            line("while (" + expr(s->condition.get()) + ") {");
            indentLevel++;
            block(s->body.get());
            indentLevel--;
            line("}");
            return;
        }
        case StmtKind::Loop:
            line("while (true) {");
            indentLevel++;
            block(static_cast<const LoopStmt*>(statement)->body.get());
            indentLevel--;
            line("}");
            return;
        case StmtKind::Break: line("break;"); return;
        case StmtKind::Continue: line("continue;"); return;
        case StmtKind::Return: {
            const auto* s = static_cast<const ReturnStmt*>(statement);
            if (!s->value) line("return;");
            else if (returnType && !returnType->isUnknown() && !returnType->isVoidLike()) line("return " + exprAs(s->value.get(), *returnType) + ";");
            else line("return " + expr(s->value.get()) + ";");
            return;
        }
        case StmtKind::Block:
            line("{");
            indentLevel++;
            block(statement);
            indentLevel--;
            line("}");
            return;
        case StmtKind::Try:
            tryStmt(static_cast<const TryStmt*>(statement));
            return;
        case StmtKind::Throw: {
            const Expr* value = static_cast<const ThrowStmt*>(statement)->value.get();
            if (typeOf(value).is(SemanticTypeKind::Str)) line("throw new $.DsError(" + expr(value) + ");");
            else line("throw new $.Thrown(" + expr(value) + ");");
            return;
        }
        default:
            return;
    }
}

void JsCodegen::forIn(const ForInStmt* s) {
    const auto* decl = static_cast<const VarDeclStmt*>(s->declaration.get());
    const SemanticType* t = analyser.declaredTypeOf(decl);
    const std::string name = jsName(decl->name.value_or("$item"));
    const SemanticType& iterable = typeOf(s->iterable.get());
    if (iterable.is(SemanticTypeKind::List)) {
        // Index-based so that adding items inside the loop is safe
        std::string seq = temp("seq"), i = temp("i");
        line("{");
        indentLevel++;
        line("const " + seq + " = " + expr(s->iterable.get()) + ";");
        line("for (let " + i + " = 0; " + i + " < " + seq + ".length; " + i + "++) {");
        indentLevel++;
        std::string item = seq + "[" + i + "]";
        if (t && needsCopy(*t)) item = "$.copy(" + item + ")";
        if (t && iterable.elementType) item = convert(item, *iterable.elementType, *t);
        line("let " + name + " = " + item + ";");
        block(s->body.get());
        indentLevel--;
        line("}");
        indentLevel--;
        line("}");
    } else if (iterable.is(SemanticTypeKind::Map)) {
        line("for (const " + name + " of Array.from(" + expr(s->iterable.get()) + ".keys())) {");
        indentLevel++;
        block(s->body.get());
        indentLevel--;
        line("}");
    } else if (iterable.is(SemanticTypeKind::Str)) {
        line("for (const " + name + " of $.m_chars(" + expr(s->iterable.get()) + ")) {");
        indentLevel++;
        block(s->body.get());
        indentLevel--;
        line("}");
    } else {
        // A set: over a copy
        line("for (const " + name + " of $.copy(" + expr(s->iterable.get()) + ").items) {");
        indentLevel++;
        block(s->body.get());
        indentLevel--;
        line("}");
    }
}

void JsCodegen::switchStmt(const SwitchStmt* s) {
    std::string value = temp("switch");
    const SemanticType& discriminantType = typeOf(s->discriminant.get());
    line("{");
    indentLevel++;
    line("const " + value + " = " + expr(s->discriminant.get()) + ";");
    bool first = true;
    const SwitchCase* defaultCase = nullptr;
    for (const auto& arm : s->cases) {
        if (!arm.value) { defaultCase = &arm; continue; }
        std::string test;
        if (const SemanticType* enumType = analyser.enumCaseOf(arm.value.get())) {
            test = value + " === " + className(*enumType) + "." + static_cast<const IdentifierExpr*>(arm.value.get())->name.lexeme;
        } else if (isPrimitive(discriminantType) && discriminantType == typeOf(arm.value.get())) {
            test = value + " === " + expr(arm.value.get());
        } else {
            test = "$.eq(" + value + ", " + expr(arm.value.get()) + ")";
        }
        line(std::string(first ? "if (" : "} else if (") + test + ") {");
        indentLevel++;
        block(arm.body.get());
        indentLevel--;
        first = false;
    }
    if (defaultCase) {
        line(first ? "{" : "} else {");
        indentLevel++;
        block(defaultCase->body.get());
        indentLevel--;
        line("}");
    } else if (!first) {
        line("}");
    }
    indentLevel--;
    line("}");
}

void JsCodegen::tryStmt(const TryStmt* s) {
    line("try {");
    indentLevel++;
    block(s->tryBlock.get());
    indentLevel--;
    // Like the C++ backend: class arms match thrown objects in order, the Str
    // arm matches runtime errors and thrown text, a catch-all the rest.
    std::vector<const CatchClause*> classArms;
    const CatchClause* strArm = nullptr;
    const CatchClause* catchAll = nullptr;
    for (const auto& clause : s->catches) {
        const SemanticType* type = analyser.catchTypeOf(&clause);
        if (!clause.param || !type) {
            if (!catchAll) catchAll = &clause;
        } else if (type->is(SemanticTypeKind::Str)) {
            if (!strArm && !catchAll) strArm = &clause;
        } else if (!catchAll) {
            classArms.push_back(&clause);
        }
    }
    const std::string error = temp("e");
    line("} catch (" + error + ") {");
    indentLevel++;
    line("if (" + error + " instanceof $.Exit) throw " + error + ";");
    bool first = true;
    auto arm = [&](const std::string& test, const CatchClause* clause, const std::string& binding) {
        line(std::string(first ? "if (" : "} else if (") + test + ") {");
        indentLevel++;
        if (!binding.empty()) line(binding);
        block(clause->body.get());
        indentLevel--;
        first = false;
    };
    for (const CatchClause* c : classArms) {
        const SemanticType* type = analyser.catchTypeOf(c);
        arm(error + " instanceof $.Thrown && " + isCheck(error + ".value", *type), c,
            "const " + jsName(*c->param) + " = " + error + ".value;");
    }
    if (strArm) {
        arm("$.isRuntimeError(" + error + ")", strArm, "const " + jsName(*strArm->param) + " = $.message(" + error + ");");
    }
    if (catchAll) {
        line(first ? "{" : "} else {");
        indentLevel++;
        block(catchAll->body.get());
        indentLevel--;
        line("}");
    } else {
        line(first ? "{" : "} else {");
        line("    throw " + error + ";");
        line("}");
    }
    indentLevel--;
    line("}");
}

// ============================================================
// Expressions
// ============================================================

bool JsCodegen::isStorageRead(const Expr* e) {
    e = unwrapGrouping(e);
    return e && (e->kind == ExprKind::Identifier || e->kind == ExprKind::Member || e->kind == ExprKind::Index);
}

std::string JsCodegen::exprAs(const Expr* e, const SemanticType& target) {
    if (!e) return "";
    const Expr* inner = unwrapGrouping(e);
    if (isNilLiteral(inner)) return "null";
    if (target.isVoidLike() || containsUnknown(target)) return expr(e);

    if (inner->kind == ExprKind::List || inner->kind == ExprKind::Set) {
        bool isSet = inner->kind == ExprKind::Set;
        const SemanticTypeKind want = isSet ? SemanticTypeKind::Set : SemanticTypeKind::List;
        const SemanticType* collectionType = target.kind == want ? &target : nullptr;
        if (!collectionType && target.kind == SemanticTypeKind::Union) {
            for (const auto& m : target.unionMembers) if (m.kind == want) collectionType = &m;
        }
        if (collectionType) {
            const auto& elements = isSet ? static_cast<const SetExpr*>(inner)->elements : static_cast<const ListExpr*>(inner)->elements;
            return collection(elements, isSet, inner, collectionType);
        }
    }
    const SemanticType* targetMap = target.kind == SemanticTypeKind::Map ? &target : nullptr;
    if (!targetMap && target.kind == SemanticTypeKind::Union) {
        for (const auto& m : target.unionMembers) if (m.kind == SemanticTypeKind::Map) targetMap = &m;
    }
    if (targetMap && inner->kind == ExprKind::Set && static_cast<const SetExpr*>(inner)->elements.empty()) return "new Map()";
    if (targetMap && inner->kind == ExprKind::Map) return mapLiteral(static_cast<const MapExpr*>(inner), targetMap);
    if (inner->kind == ExprKind::Ternary) {
        const auto* t = static_cast<const TernaryExpr*>(inner);
        return "(" + expr(t->condition.get()) + " ? " + exprAs(t->thenBranch.get(), target) + " : " +
               exprAs(t->elseBranch.get(), target) + ")";
    }
    // Number literals straight into a Dbl / Flt
    if (inner->kind == ExprKind::Literal && isNumber(withoutNil(target))) {
        const Token& tok = static_cast<const LiteralExpr*>(inner)->token;
        if (tok.type == TokenType::INT_LITERAL || tok.type == TokenType::DOUBLE_LITERAL || tok.type == TokenType::FLOAT_LITERAL) {
            std::string digits = tok.lexeme;
            if (!digits.empty() && (digits.back() == 'f' || digits.back() == 'd')) digits.pop_back();
            return withoutNil(target).is(SemanticTypeKind::Flt) ? "Math.fround(" + digits + ")" : digits;
        }
    }
    std::string code = expr(e);
    const SemanticType& source = typeOf(e);
    if ((needsCopy(target) || needsCopy(source)) && isStorageRead(e)) code = "$.copy(" + code + ")";
    return convert(code, source, target);
}

std::string JsCodegen::printable(const Expr* e) {
    const SemanticType& t = typeOf(e);
    if (t.is(SemanticTypeKind::Int) && t.radix && *t.radix != 10) return "$.radix(" + expr(e) + ", " + std::to_string(*t.radix) + ")";
    std::string shape = fltShape(t);
    if (!shape.empty()) return shape == "\"f\"" ? "$.flt(" + expr(e) + ")" : "$.fview(" + expr(e) + ", " + shape + ")";
    return expr(e);
}

std::string JsCodegen::number(const Expr* e) {
    return typeOf(e).isInteger() ? "Number(" + expr(e) + ")" : expr(e);
}

std::string JsCodegen::expr(const Expr* e) {
    if (!e) return "";
    switch (e->kind) {
        case ExprKind::Literal: return literal(static_cast<const LiteralExpr*>(e));
        case ExprKind::TemplateString: {
            const auto* t = static_cast<const TemplateStringExpr*>(e);
            std::string args;
            for (size_t i = 0; i < t->literalParts.size(); ++i) {
                if (!t->literalParts[i].empty()) args += (args.empty() ? "" : ", ") + jsString(t->literalParts[i]);
                if (i < t->interpolations.size()) args += (args.empty() ? "" : ", ") + printable(t->interpolations[i].get());
            }
            return args.empty() ? "\"\"" : "$.cat(" + args + ")";
        }
        case ExprKind::Identifier: return identifier(static_cast<const IdentifierExpr*>(e));
        case ExprKind::Unary: {
            const auto* u = static_cast<const UnaryExpr*>(e);
            const SemanticType& t = typeOf(u->operand.get());
            if (u->op.type == TokenType::MINUS) {
                if (t.isInteger()) return "$.neg(" + expr(u->operand.get()) + ")";
                return "(-" + expr(u->operand.get()) + ")";
            }
            if (u->op.type == TokenType::BITWISE_NOT) return "(~" + expr(u->operand.get()) + ")";
            return "(!" + expr(u->operand.get()) + ")";
        }
        case ExprKind::Postfix: return postfix(static_cast<const PostfixExpr*>(e));
        case ExprKind::Binary: return binary(static_cast<const BinaryExpr*>(e));
        case ExprKind::Assign: return assign(static_cast<const AssignExpr*>(e));
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            const SemanticType& result = typeOf(e);
            return "(" + expr(t->condition.get()) + " ? " + exprAs(t->thenBranch.get(), result) + " : " +
                   exprAs(t->elseBranch.get(), result) + ")";
        }
        case ExprKind::Call: return call(static_cast<const CallExpr*>(e));
        case ExprKind::Index: {
            const auto* i = static_cast<const IndexExpr*>(e);
            return "$.at(" + expr(i->object.get()) + ", " + expr(i->index.get()) + ")";
        }
        case ExprKind::Member: return member(static_cast<const MemberExpr*>(e));
        case ExprKind::Lambda: return lambda(static_cast<const LambdaExpr*>(e));
        case ExprKind::New: {
            const auto* n = static_cast<const NewExpr*>(e);
            const SemanticType& type = typeOf(e);
            const ClassInfo* info = analyser.findClass(type);
            std::vector<FunctionParameterSymbol> ps;
            if (const Symbol* ctor = info ? nearestConstructor(*info) : nullptr) ps = ctor->parameters;
            if (isExtern(type)) return "new " + type.name + "(" + arguments(e, n->args, ps, true) + ")";
            return "new " + className(type) + "().$construct_" + type.name + "(" + arguments(e, n->args, ps) + ")";
        }
        case ExprKind::List: return collection(static_cast<const ListExpr*>(e)->elements, false, e);
        case ExprKind::Set: return collection(static_cast<const SetExpr*>(e)->elements, true, e);
        case ExprKind::Grouping: return "(" + expr(static_cast<const GroupingExpr*>(e)->inner.get()) + ")";
        case ExprKind::Map: return mapLiteral(static_cast<const MapExpr*>(e), nullptr);
        case ExprKind::Convert: {
            const auto* c = static_cast<const ConvertExpr*>(e);
            const std::string& to = c->type.lexeme;
            if (to == "Str") return "$.toStr(" + printable(c->value.get()) + ")";
            if (to == "Int") return "$.toInt(" + expr(c->value.get()) + ")";
            if (to == "Dbl") return "$.toDbl(" + expr(c->value.get()) + ")";
            if (to == "Flt") return "$.toFlt(" + expr(c->value.get()) + ")";
            if (to == "Bul") return "$.toBul(" + expr(c->value.get()) + ")";
            if (to == "Chr") return "$.toChr(" + expr(c->value.get()) + ")";
            return expr(c->value.get());
        }
        case ExprKind::Is: {
            const auto* is = static_cast<const IsExpr*>(e);
            const SemanticType* target = analyser.isTargetOf(is);
            if (!target) return "false";
            return isCheck(expr(is->value.get()), *target);
        }
    }
    return "";
}

std::string JsCodegen::isCheck(const std::string& value, const SemanticType& target) const {
    switch (target.kind) {
        case SemanticTypeKind::Int: return "$.isInt(" + value + ")";
        case SemanticTypeKind::Dbl: case SemanticTypeKind::Flt: return "$.isNum(" + value + ")";
        case SemanticTypeKind::Str: return "$.isStr(" + value + ")";
        case SemanticTypeKind::Bul: return "$.isBul(" + value + ")";
        case SemanticTypeKind::Chr: return "$.isChr(" + value + ")";
        case SemanticTypeKind::List: return "$.isList(" + value + ")";
        case SemanticTypeKind::Set: return "$.isSet(" + value + ")";
        case SemanticTypeKind::Map: return "$.isMap(" + value + ")";
        case SemanticTypeKind::Function: return "$.isFn(" + value + ")";
        case SemanticTypeKind::Nil: return "(" + value + " == null)";
        case SemanticTypeKind::UserDefined:
            if (target.isEnum()) return "$.isEnumOf(" + value + ", " + className(target) + ")";
            if (target.isInterface()) return "$.implements(" + value + ", " + jsString(target.key()) + ")";
            if (isExtern(target)) return "(" + value + " instanceof " + target.name + ")";
            return "$.isClass(" + value + ", " + className(target) + ")";
        default: return "false";
    }
}

std::string JsCodegen::literal(const LiteralExpr* e) {
    const std::string& text = e->token.lexeme;
    switch (e->token.type) {
        case TokenType::INT_LITERAL: case TokenType::HEX_LITERAL: case TokenType::BINARY_LITERAL: return text + "n";
        case TokenType::OCTAL_LITERAL: return "0o" + text.substr(2) + "n";
        case TokenType::FLOAT_LITERAL: return "Math.fround(" + text.substr(0, text.size() - 1) + ")";
        case TokenType::DOUBLE_LITERAL: return text.back() == 'd' ? text.substr(0, text.size() - 1) : text;
        case TokenType::CHAR_LITERAL: {
            if (text.empty()) return "$.chr(0)";
            unsigned char b = static_cast<unsigned char>(text[0]);
            unsigned long cp = b;
            if (b >= 0x80) {
                int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
                cp = extra == 3 ? (b & 0x07) : extra == 2 ? (b & 0x0F) : (b & 0x1F);
                for (int k = 1; k <= extra && k < static_cast<int>(text.size()); ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
            }
            return "$.chr(" + std::to_string(cp) + ")";
        }
        case TokenType::STRING: return jsString(text);
        case TokenType::TRUE: return "true";
        case TokenType::FALSE: return "false";
        case TokenType::NIL: return "null";
        default: return "undefined";
    }
}

std::string JsCodegen::identifier(const IdentifierExpr* e) {
    if (e->name.type == TokenType::THIS) return "this";
    const Symbol* symbol = analyser.symbolOf(e);
    if (!symbol) return jsName(e->name.lexeme);
    switch (symbol->kind) {
        case SymbolKind::Field:
        case SymbolKind::Method:
            return "this." + memberName(symbol->name);
        case SymbolKind::Builtin:
        case SymbolKind::Function:
        case SymbolKind::Class:
        case SymbolKind::Interface:
        case SymbolKind::Enum:
            return globalName(*symbol);
        default:
            return symbol->isGlobal ? globalName(*symbol) : jsName(symbol->name);
    }
}

std::string JsCodegen::binary(const BinaryExpr* e) {
    const Expr* l = e->left.get();
    const Expr* r = e->right.get();
    const SemanticType& lt = typeOf(l);
    const SemanticType& rt = typeOf(r);
    const SemanticType& result = typeOf(e);
    auto flt = [&](const std::string& code) { return result.is(SemanticTypeKind::Flt) ? "Math.fround(" + code + ")" : code; };
    const bool bothInt = lt.isInteger() && rt.isInteger();

    switch (e->op.type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL: {
            bool negate = e->op.type == TokenType::BANG_EQUAL;
            if (isNilLiteral(l) || isNilLiteral(r)) {
                const Expr* other = isNilLiteral(l) ? r : l;
                return "(" + expr(other) + (negate ? " != null)" : " == null)");
            }
            if (isPrimitive(lt) && lt == rt && !lt.isUnknown()) return "(" + expr(l) + (negate ? " !== " : " === ") + expr(r) + ")";
            if (lt.isNumeric() && rt.isNumeric()) return "(" + number(l) + (negate ? " !== " : " === ") + number(r) + ")";
            return std::string("(") + (negate ? "!" : "") + "$.eq(" + expr(l) + ", " + expr(r) + "))";
        }
        case TokenType::QUESTION_QUESTION: return "(" + expr(l) + " ?? " + exprAs(r, result) + ")";
        case TokenType::AND_AND: return "(" + expr(l) + " && " + expr(r) + ")";
        case TokenType::OR_OR: return "(" + expr(l) + " || " + expr(r) + ")";
        case TokenType::DIV:
            if (bothInt) return "$.div(" + expr(l) + ", " + expr(r) + ")";
            return flt("(" + number(l) + " / " + number(r) + ")");
        case TokenType::MOD:
            if (bothInt) return "$.mod(" + expr(l) + ", " + expr(r) + ")";
            return flt("$.mod(" + number(l) + ", " + number(r) + ")");
        case TokenType::EXPONENT:
            if (bothInt) return "$.pow(" + expr(l) + ", " + expr(r) + ")";
            return flt("$.pow(" + number(l) + ", " + number(r) + ")");
        case TokenType::PLUS:
            if (bothInt) return "$.add(" + expr(l) + ", " + expr(r) + ")";
            if (lt.is(SemanticTypeKind::Str) || rt.is(SemanticTypeKind::Str) || lt.is(SemanticTypeKind::Chr) || rt.is(SemanticTypeKind::Chr)) {
                auto text = [&](const Expr* side, const SemanticType& t) { return t.is(SemanticTypeKind::Chr) ? "$.str(" + expr(side) + ")" : expr(side); };
                return "(" + text(l, lt) + " + " + text(r, rt) + ")";
            }
            return flt("(" + number(l) + " + " + number(r) + ")");
        case TokenType::MINUS:
            if (bothInt) return "$.sub(" + expr(l) + ", " + expr(r) + ")";
            return flt("(" + number(l) + " - " + number(r) + ")");
        case TokenType::MULT:
            if (bothInt) return "$.mul(" + expr(l) + ", " + expr(r) + ")";
            return flt("(" + number(l) + " * " + number(r) + ")");
        case TokenType::L_ANGLE: case TokenType::R_ANGLE: case TokenType::LESS_EQUAL: case TokenType::GREATER_EQUAL: {
            std::string op = e->op.type == TokenType::L_ANGLE ? " < " : e->op.type == TokenType::R_ANGLE ? " > "
                           : e->op.type == TokenType::LESS_EQUAL ? " <= " : " >= ";
            if (lt.is(SemanticTypeKind::Str)) return "($.strCmp(" + expr(l) + ", " + expr(r) + ")" + op + "0)";
            if (lt.is(SemanticTypeKind::Chr)) return "(" + expr(l) + ".code" + op + expr(r) + ".code)";
            return "(" + expr(l) + op + expr(r) + ")";   // BigInt and number compare correctly in JS
        }
        case TokenType::LEFT_SHIFT: return "$.shl(" + expr(l) + ", " + expr(r) + ")";
        case TokenType::RIGHT_SHIFT: return "(" + expr(l) + " >> " + expr(r) + ")";
        case TokenType::BITWISE_AND: return "(" + expr(l) + " & " + expr(r) + ")";
        case TokenType::BITWISE_OR: return "(" + expr(l) + " | " + expr(r) + ")";
        case TokenType::BITWISE_XOR: return "(" + expr(l) + " ^ " + expr(r) + ")";
        default:
            return "(" + expr(l) + " " + e->op.lexeme + " " + expr(r) + ")";
    }
}

std::string JsCodegen::assign(const AssignExpr* e) {
    const Expr* target = e->target.get();
    const SemanticType& targetType = typeOf(target);
    const SemanticType& valueType = typeOf(e->value.get());
    const bool externField = target->kind == ExprKind::Member && isExternMember(analyser.memberOf(target));

    // The value to store, given the current value's code (compound operators)
    auto combine = [&](const std::string& current) -> std::string {
        std::string value = expr(e->value.get());
        const SemanticType base = withoutNil(targetType);
        const bool bothInt = base.isInteger() && valueType.isInteger();
        const bool flt = base.is(SemanticTypeKind::Flt);
        auto num = [&](const std::string& code, const SemanticType& t) { return t.isInteger() ? "Number(" + code + ")" : code; };
        auto f = [&](const std::string& code) { return flt ? "Math.fround(" + code + ")" : code; };
        switch (e->op.type) {
            case TokenType::PLUS_EQUAL:
                if (bothInt) return "$.add(" + current + ", " + value + ")";
                if (base.is(SemanticTypeKind::Str)) return current + " + " + (valueType.is(SemanticTypeKind::Chr) ? "$.str(" + value + ")" : value);
                return f(num(current, base) + " + " + num(value, valueType));
            case TokenType::MINUS_EQUAL:
                if (bothInt) return "$.sub(" + current + ", " + value + ")";
                return f(num(current, base) + " - " + num(value, valueType));
            case TokenType::MULT_EQUAL:
                if (bothInt) return "$.mul(" + current + ", " + value + ")";
                return f(num(current, base) + " * " + num(value, valueType));
            case TokenType::DIV_EQUAL:
                // x /= 2 on an Int keeps it an Int (truncated)
                if (base.isInteger()) return "$.trunc($.div(" + current + ", " + value + "))";
                return f(num(current, base) + " / " + num(value, valueType));
            case TokenType::MOD_EQUAL:
                if (bothInt) return "$.mod(" + current + ", " + value + ")";
                return f("$.mod(" + num(current, base) + ", " + num(value, valueType) + ")");
            case TokenType::EXPONENT_EQUAL:
                if (bothInt) return "$.pow(" + current + ", " + value + ")";
                return f("$.pow(" + num(current, base) + ", " + num(value, valueType) + ")");
            default:
                return value;
        }
    };

    if (target->kind == ExprKind::Index) {
        const auto* index = static_cast<const IndexExpr*>(target);
        const SemanticType& objectType = typeOf(index->object.get());
        const SemanticType keyType = objectType.is(SemanticTypeKind::Map) && objectType.keyType ? *objectType.keyType : SemanticType::simple(SemanticTypeKind::Int);
        if (e->op.type == TokenType::EQUAL) {
            return "$.setAt(" + expr(index->object.get()) + ", " + exprAs(index->index.get(), keyType) + ", " +
                   exprAs(e->value.get(), targetType) + ")";
        }
        std::string o = temp("o"), i = temp("k");
        return "((" + o + ", " + i + ") => $.setAt(" + o + ", " + i + ", " + combine("$.at(" + o + ", " + i + ")") + "))(" +
               expr(index->object.get()) + ", " + expr(index->index.get()) + ")";
    }

    std::string lhs;
    if (target->kind == ExprKind::Identifier) {
        const Symbol* s = analyser.symbolOf(target);
        if (s && s->kind == SymbolKind::Field) lhs = "this." + memberName(s->name);
        else if (s) lhs = s->isGlobal ? globalName(*s) : jsName(s->name);
        else lhs = expr(target);
    } else if (target->kind == ExprKind::Member) {
        lhs = memberTarget(static_cast<const MemberExpr*>(target));
    } else {
        lhs = expr(target);
    }
    std::string value = e->op.type == TokenType::EQUAL ? exprAs(e->value.get(), targetType) : combine(lhs);
    if (externField) value = toExtern(value, targetType);
    return "(" + lhs + " = " + value + ")";
}

std::string JsCodegen::postfix(const PostfixExpr* e) {
    const Expr* target = e->operand.get();
    const SemanticType t = withoutNil(typeOf(target));
    const bool inc = e->op.type == TokenType::PLUS_PLUS;
    if (target->kind == ExprKind::Index) {
        const auto* index = static_cast<const IndexExpr*>(target);
        std::string o = temp("o"), i = temp("k"), v = temp("v");
        std::string next = t.isInteger() ? std::string(inc ? "$.add(" : "$.sub(") + v + ", 1n)" : v + (inc ? " + 1" : " - 1");
        return "((" + o + ", " + i + ") => { const " + v + " = $.at(" + o + ", " + i + "); $.setAt(" + o + ", " + i + ", " + next +
               "); return " + v + "; })(" + expr(index->object.get()) + ", " + expr(index->index.get()) + ")";
    }
    std::string lhs = target->kind == ExprKind::Member ? memberTarget(static_cast<const MemberExpr*>(target)) : expr(target);
    if (target->kind == ExprKind::Identifier) {
        const Symbol* s = analyser.symbolOf(target);
        if (s && s->kind == SymbolKind::Field) lhs = "this." + memberName(s->name);
        else if (s) lhs = s->isGlobal ? globalName(*s) : jsName(s->name);
    }
    if (t.isInteger()) {
        // The old value: the new one minus the step (the step itself was checked)
        return inc ? "((" + lhs + " = $.add(" + lhs + ", 1n)) - 1n)" : "((" + lhs + " = $.sub(" + lhs + ", 1n)) + 1n)";
    }
    return "(" + lhs + (inc ? "++" : "--") + ")";
}

std::string JsCodegen::arguments(const Expr* callExpr, const std::vector<Argument>& args,
                                 const std::vector<FunctionParameterSymbol>& ps, bool externCall) {
    const ArgumentOrder* order = analyser.argumentOrder(callExpr);
    const bool ordered = order && order->size() == ps.size();
    std::vector<std::string> code(args.size());
    for (size_t a = 0; a < args.size(); ++a) {
        const SemanticType* p = &kUnknown;
        if (ordered) {
            for (size_t i = 0; i < ps.size(); ++i) {
                const auto& sources = (*order)[i];
                if (std::find(sources.begin(), sources.end(), static_cast<int>(a)) == sources.end()) continue;
                p = &ps[i].type;
                if (ps[i].variadic && !args[a].isSpread && ps[i].type.elementType) p = ps[i].type.elementType.get();
            }
        } else if (a < ps.size()) {
            p = &ps[a].type;
        }
        code[a] = exprAs(args[a].value.get(), *p);
        if (externCall) code[a] = toExtern(code[a], *p);
    }
    std::string s;
    if (!ordered) {
        for (size_t a = 0; a < args.size(); ++a) s += (a ? ", " : "") + code[a];
        return s;
    }
    // JS evaluates arguments left to right like Dessa, but the order of the
    // parameters may differ from the call (named arguments): evaluate first.
    bool reordered = false;
    {
        int last = -1;
        for (size_t i = 0; i < ps.size(); ++i) {
            for (int a : (*order)[i]) {
                if (a < last) reordered = true;
                last = a;
            }
        }
    }
    std::vector<std::string> names(args.size());
    std::string prelude;
    if (reordered && std::any_of(args.begin(), args.end(), [](const Argument& a) { return a.value->kind == ExprKind::Call || a.value->kind == ExprKind::Assign; })) {
        for (size_t a = 0; a < args.size(); ++a) {
            names[a] = temp("arg");
            prelude += names[a] + " = " + code[a] + ", ";
            code[a] = names[a];
        }
    }
    for (size_t i = 0; i < ps.size(); ++i) {
        const auto& sources = (*order)[i];
        const FunctionParameterSymbol& p = ps[i];
        std::string value;
        if (p.variadic) {
            bool spread = std::any_of(sources.begin(), sources.end(), [&](int a) { return args[static_cast<size_t>(a)].isSpread; });
            if (!spread) {
                value = "[";
                for (size_t k = 0; k < sources.size(); ++k) value += (k ? ", " : "") + code[static_cast<size_t>(sources[k])];
                value += "]";
            } else {
                value = "$.list(";
                for (size_t k = 0; k < sources.size(); ++k) {
                    int a = sources[k];
                    value += (k ? ", " : "") + std::string("[") + (args[static_cast<size_t>(a)].isSpread ? "true" : "false") + ", " + code[static_cast<size_t>(a)] + "]";
                }
                value += ")";
            }
        } else if (!sources.empty()) {
            value = code[static_cast<size_t>(sources.front())];
        } else if (p.defaultValue) {
            value = exprAs(p.defaultValue, p.type);
            if (externCall) value = toExtern(value, p.type);
        } else {
            continue;
        }
        s += (s.empty() ? "" : ", ") + value;
    }
    if (!prelude.empty()) {
        // (a = f(), b = g(), [b, a]) is awkward in a call; use a tiny arrow instead
        std::string decl;
        for (const auto& n : names) decl += (decl.empty() ? "" : ", ") + n;
        return "...((" + decl + ") => (" + prelude + "[" + s + "]))()";
    }
    return s;
}

std::string JsCodegen::toExtern(const std::string& code, const SemanticType& type) const {
    SemanticType t = withoutNil(type);
    if (t.isInteger()) return "$.extOut(" + code + ")";
    if (t.is(SemanticTypeKind::Function)) return "$.callback(" + code + ")";   // event listeners report their errors
    return code;
}

std::string JsCodegen::fromExtern(const std::string& code, const SemanticType& type) const {
    SemanticType t = withoutNil(type);
    if (t.isInteger()) return "$.extInt(" + code + ")";
    if (t.is(SemanticTypeKind::List)) return "$.extList(" + code + ")";
    if (type.containsNil() || t.is(SemanticTypeKind::Str)) return "(" + code + " ?? null)";
    return code;
}

std::string JsCodegen::call(const CallExpr* e) {
    const Expr* callee = e->callee.get();

    if (callee->kind == ExprKind::Identifier) {
        const Symbol* symbol = analyser.symbolOf(callee);
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->variadic) {
            // print(a, b, sep: ", ", end: "")
            std::string args, sep, end;
            for (const auto& arg : e->args) {
                if (arg.name && *arg.name == "sep") sep = expr(arg.value.get());
                else if (arg.name && *arg.name == "end") end = expr(arg.value.get());
                else {
                    std::string part = printable(arg.value.get());
                    if (needsCopy(typeOf(arg.value.get())) && isStorageRead(arg.value.get())) part = "$.copy(" + part + ")";
                    args += (args.empty() ? "" : ", ") + part;
                }
            }
            if (!sep.empty() || !end.empty()) {
                return "$.printWith(" + (sep.empty() ? std::string("\" \"") : sep) + ", " + (end.empty() ? std::string("\"\\n\"") : end) +
                       (args.empty() ? "" : ", " + args) + ")";
            }
            return globalName(*symbol) + "(" + args + ")";
        }
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->cppName == "dessa::type_of" && e->args.size() == 1) {
            const Expr* value = e->args[0].value.get();
            return "$.typeOf(" + expr(value) + ", " + jsString(typeOf(value).toString()) + ")";
        }
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->cppName == "dessa::zip") {
            std::string a = expr(e->args[0].value.get()), b = expr(e->args[1].value.get());
            if (e->args.size() == 3) return "$.zip_with(" + a + ", " + b + ", " + expr(e->args[2].value.get()) + ")";
            return "$.zip(" + a + ", " + b + ")";
        }
        if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Builtin || symbol->kind == SymbolKind::Method)) {
            std::string name = symbol->kind == SymbolKind::Method ? "this." + memberName(symbol->name) : globalName(*symbol);
            return name + "(" + arguments(e, e->args, symbol->parameters) + ")";
        }
    }

    if (callee->kind == ExprKind::Member) {
        const auto* m = static_cast<const MemberExpr*>(callee);
        const Symbol* symbol = analyser.memberOf(callee);
        if (symbol && symbol->kind == SymbolKind::Method) {
            const Expr* object = m->object.get();
            const bool externCall = isExternMember(symbol);
            std::string args = arguments(e, e->args, symbol->parameters, externCall);
            if (isSuper(object)) return "super." + memberName(m->name.lexeme) + "(" + args + ")";
            bool chained = analyser.isOptionalChain(callee);
            std::string objectCode = expr(object);
            if (!symbol->cppName.empty()) {
                // Builtin method: $.m_name(value, args)
                std::string fn = globalName(*symbol);
                if (!chained) return fn + "(" + objectCode + (args.empty() ? "" : ", " + args) + ")";
                std::string o = temp("o");
                return "$.chain(" + objectCode + ", (" + o + ") => " + fn + "(" + o + (args.empty() ? "" : ", " + args) + "))";
            }
            std::string name = externCall ? m->name.lexeme : memberName(m->name.lexeme);
            std::string invoke;
            if (!chained) {
                invoke = (isSelf(object) ? std::string("this") : objectCode) + "." + name + "(" + args + ")";
            } else {
                std::string o = temp("o");
                invoke = "$.chain(" + objectCode + ", (" + o + ") => " + o + "." + name + "(" + args + "))";
            }
            return externCall ? fromExtern(invoke, symbol->returnType) : invoke;
        }
    }

    // A value holding a function (lambda variable, field, ...)
    const SemanticType fnType = withoutNil(typeOf(callee));
    std::vector<FunctionParameterSymbol> ps;
    for (const auto& p : fnType.parameterTypes) ps.push_back({"", p});
    return expr(callee) + "(" + arguments(e, e->args, ps) + ")";
}

std::string JsCodegen::member(const MemberExpr* e) {
    const Symbol* symbol = analyser.memberOf(e);
    const Expr* object = e->object.get();
    const SemanticType& objectType = typeOf(object);
    if (!symbol) return "undefined";

    // Enum value
    if (symbol->kind == SymbolKind::Constant && objectType.isEnum() && analyser.symbolOf(object) &&
        analyser.symbolOf(object)->kind == SymbolKind::Enum) {
        return className(objectType) + "." + e->name.lexeme;
    }
    const bool externField = isExternMember(symbol);
    const std::string access = externField ? e->name.lexeme : memberName(e->name.lexeme);
    bool chained = analyser.isOptionalChain(e);
    std::string code;
    if (!chained) {
        code = ((isSelf(object) || isSuper(object)) ? std::string("this") : expr(object)) + "." + access;
    } else {
        std::string o = temp("o");
        code = "$.chain(" + expr(object) + ", (" + o + ") => " + o + "." + access + ")";
    }
    return externField && symbol->kind == SymbolKind::Field ? fromExtern(code, symbol->type) : code;
}

// obj.field as something to assign to (no conversion of what's read)
std::string JsCodegen::memberTarget(const MemberExpr* e) {
    const Symbol* symbol = analyser.memberOf(e);
    const Expr* object = e->object.get();
    const std::string access = isExternMember(symbol) ? e->name.lexeme : memberName(e->name.lexeme);
    return ((isSelf(object) || isSuper(object)) ? std::string("this") : expr(object)) + "." + access;
}

// ---- Lambdas: captured locals are copied when the lambda is made ----

void JsCodegen::capturedLocals(const Expr* e, const LambdaExpr* lambda, std::vector<std::pair<std::string, SemanticType>>& out,
                               std::set<std::string>& seen) const {
    if (!e) return;
    auto sub = [&](const Expr* x) { capturedLocals(x, lambda, out, seen); };
    switch (e->kind) {
        case ExprKind::Identifier: {
            const Symbol* s = analyser.symbolOf(e);
            if (!s || s->isGlobal || analyser.isRecursiveRef(e)) return;
            if (s->kind != SymbolKind::Variable && s->kind != SymbolKind::Parameter && s->kind != SymbolKind::Constant) return;
            // declared before the lambda starts = from outside
            bool before = s->line < lambda->line || (s->line == lambda->line && s->column < lambda->column);
            if (before && seen.insert(s->name).second) out.emplace_back(s->name, s->type);
            return;
        }
        case ExprKind::TemplateString:
            for (const auto& i : static_cast<const TemplateStringExpr*>(e)->interpolations) sub(i.get());
            return;
        case ExprKind::Unary: sub(static_cast<const UnaryExpr*>(e)->operand.get()); return;
        case ExprKind::Postfix: sub(static_cast<const PostfixExpr*>(e)->operand.get()); return;
        case ExprKind::Binary: sub(static_cast<const BinaryExpr*>(e)->left.get()); sub(static_cast<const BinaryExpr*>(e)->right.get()); return;
        case ExprKind::Assign: sub(static_cast<const AssignExpr*>(e)->target.get()); sub(static_cast<const AssignExpr*>(e)->value.get()); return;
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            sub(t->condition.get()); sub(t->thenBranch.get()); sub(t->elseBranch.get());
            return;
        }
        case ExprKind::Call: {
            const auto* c = static_cast<const CallExpr*>(e);
            sub(c->callee.get());
            for (const auto& a : c->args) sub(a.value.get());
            return;
        }
        case ExprKind::Index: sub(static_cast<const IndexExpr*>(e)->object.get()); sub(static_cast<const IndexExpr*>(e)->index.get()); return;
        case ExprKind::Member: sub(static_cast<const MemberExpr*>(e)->object.get()); return;
        case ExprKind::Lambda: {
            const auto* l = static_cast<const LambdaExpr*>(e);
            capturedLocals(l->body.get(), lambda, out, seen);
            return;
        }
        case ExprKind::New: for (const auto& a : static_cast<const NewExpr*>(e)->args) sub(a.value.get()); return;
        case ExprKind::List: for (const auto& el : static_cast<const ListExpr*>(e)->elements) sub(el.value.get()); return;
        case ExprKind::Set: for (const auto& el : static_cast<const SetExpr*>(e)->elements) sub(el.value.get()); return;
        case ExprKind::Grouping: sub(static_cast<const GroupingExpr*>(e)->inner.get()); return;
        case ExprKind::Map:
            for (const auto& entry : static_cast<const MapExpr*>(e)->entries) { sub(entry.key.get()); sub(entry.value.get()); }
            return;
        case ExprKind::Convert: sub(static_cast<const ConvertExpr*>(e)->value.get()); return;
        case ExprKind::Is: sub(static_cast<const IsExpr*>(e)->value.get()); return;
        case ExprKind::Literal: return;
    }
}

void JsCodegen::capturedLocals(const Stmt* s, const LambdaExpr* lambda, std::vector<std::pair<std::string, SemanticType>>& out,
                               std::set<std::string>& seen) const {
    if (!s) return;
    auto e = [&](const Expr* x) { capturedLocals(x, lambda, out, seen); };
    auto st = [&](const Stmt* x) { capturedLocals(x, lambda, out, seen); };
    switch (s->kind) {
        case StmtKind::Expression: e(static_cast<const ExpressionStmt*>(s)->expression.get()); return;
        case StmtKind::VarDecl: e(static_cast<const VarDeclStmt*>(s)->initializer.get()); return;
        case StmtKind::If: {
            const auto* x = static_cast<const IfStmt*>(s);
            e(x->condition.get()); st(x->thenBranch.get()); st(x->elseBranch.get());
            return;
        }
        case StmtKind::Switch: {
            const auto* x = static_cast<const SwitchStmt*>(s);
            e(x->discriminant.get());
            for (const auto& c : x->cases) { e(c.value.get()); st(c.body.get()); }
            return;
        }
        case StmtKind::For: {
            const auto* x = static_cast<const ForStmt*>(s);
            st(x->init.get()); e(x->condition.get()); e(x->increment.get()); st(x->body.get());
            return;
        }
        case StmtKind::ForIn: {
            const auto* x = static_cast<const ForInStmt*>(s);
            e(x->iterable.get()); st(x->body.get());
            return;
        }
        case StmtKind::While: { const auto* x = static_cast<const WhileStmt*>(s); e(x->condition.get()); st(x->body.get()); return; }
        case StmtKind::Loop: st(static_cast<const LoopStmt*>(s)->body.get()); return;
        case StmtKind::Return: e(static_cast<const ReturnStmt*>(s)->value.get()); return;
        case StmtKind::Block: for (const auto& x : static_cast<const BlockStmt*>(s)->statements) st(x.get()); return;
        case StmtKind::Try: {
            const auto* x = static_cast<const TryStmt*>(s);
            st(x->tryBlock.get());
            for (const auto& c : x->catches) st(c.body.get());
            return;
        }
        case StmtKind::Throw: e(static_cast<const ThrowStmt*>(s)->value.get()); return;
        default: return;
    }
}

std::string JsCodegen::lambda(const LambdaExpr* e) {
    const SemanticType& type = typeOf(e);
    const SemanticType* expected = analyser.expectedTypeOf(e);
    const SemanticType& fn = (expected && expected->kind == SemanticTypeKind::Function) ? *expected : type;
    SemanticType ret = fn.returnType ? *fn.returnType : SemanticType::simple(SemanticTypeKind::Emp);
    if (type.returnType && !type.returnType->isUnknown() && ret.isUnknown()) ret = *type.returnType;

    std::string ps;
    for (size_t i = 0; i < e->params.size(); ++i) ps += (i ? ", " : "") + jsName(e->params[i].name);

    auto savedReturn = returnType;
    returnType = ret;
    std::string body;
    if (e->body && e->body->kind == StmtKind::Expression) {
        const Expr* value = static_cast<const ExpressionStmt*>(e->body.get())->expression.get();
        body = ret.isVoidLike() ? "{ " + expr(value) + "; }" : "(" + exprAs(value, ret) + ")";
    } else if (e->body) {
        std::ostringstream saved;
        saved << out.str();
        out.str("");
        out.clear();
        indentLevel++;
        block(e->body.get());
        indentLevel--;
        std::string inner = out.str();
        out.str("");
        out.clear();
        out << saved.str();
        body = "{\n" + inner + std::string(static_cast<size_t>(indentLevel) * 4, ' ') + "}";
    }
    returnType = savedReturn;
    std::string arrow = "(" + ps + ") => " + body;

    // Locals from outside are captured by value (C++'s [=]): later changes
    // to them don't reach the lambda.
    std::vector<std::pair<std::string, SemanticType>> captured;
    std::set<std::string> seen;
    capturedLocals(e->body.get(), e, captured, seen);
    if (captured.empty()) return arrow;
    std::string names, values;
    for (const auto& [name, t] : captured) {
        names += (names.empty() ? "" : ", ") + jsName(name);
        values += (values.empty() ? "" : ", ") + (needsCopy(t) ? "$.copy(" + jsName(name) + ")" : jsName(name));
    }
    return "((" + names + ") => " + arrow + ")(" + values + ")";
}

std::string JsCodegen::collection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at,
                                  const SemanticType* target) {
    const SemanticType* expected = target ? target : analyser.expectedTypeOf(at);
    SemanticType type = typeOf(at);
    const SemanticTypeKind want = isSet ? SemanticTypeKind::Set : SemanticTypeKind::List;
    if (expected && expected->kind == want) type = *expected;
    else if (expected && expected->kind == SemanticTypeKind::Union) {
        for (const auto& m : expected->unionMembers) if (m.kind == want) type = m;
    }
    SemanticType element = type.elementType ? *type.elementType : kUnknown;
    bool spread = std::any_of(elements.begin(), elements.end(), [](const CollectionElement& c) { return c.isSpread; });
    std::string items;
    if (!spread) {
        items = "[";
        for (size_t i = 0; i < elements.size(); ++i) items += (i ? ", " : "") + exprAs(elements[i].value.get(), element);
        items += "]";
    } else {
        items = "$.list(";
        for (size_t i = 0; i < elements.size(); ++i) {
            const auto& el = elements[i];
            items += (i ? ", " : "") + std::string("[") + (el.isSpread ? "true, " + expr(el.value.get()) : "false, " + exprAs(el.value.get(), element)) + "]";
        }
        items += ")";
    }
    return isSet ? "$.set(" + items + ")" : items;
}

std::string JsCodegen::mapLiteral(const MapExpr* e, const SemanticType* target) {
    SemanticType type = target ? *target : typeOf(e);
    if (!target) {
        if (const SemanticType* expected = analyser.expectedTypeOf(e); expected && expected->kind == SemanticTypeKind::Map) type = *expected;
    }
    const SemanticType key = type.keyType ? *type.keyType : kUnknown;
    const SemanticType value = type.elementType ? *type.elementType : kUnknown;
    bool spread = std::any_of(e->entries.begin(), e->entries.end(), [](const MapEntry& m) { return !m.key; });
    std::string s = spread ? "$.mapSpread(" : "$.map([";
    for (size_t i = 0; i < e->entries.size(); ++i) {
        const auto& entry = e->entries[i];
        s += (i ? ", " : "");
        if (!entry.key) s += "[" + expr(entry.value.get()) + "]";
        else s += "[" + exprAs(entry.key.get(), key) + ", " + exprAs(entry.value.get(), value) + "]";
    }
    return s + (spread ? ")" : "])");
}

} // namespace dessa
