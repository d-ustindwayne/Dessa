#include "codegen.hpp"

#include <algorithm>
#include <unordered_set>

namespace dessa {

namespace {

const SemanticType kUnknown = SemanticType::unknown();

bool isNilLiteral(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::NIL;
}

bool isSelf(const Expr* e) {
    return e && e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type == TokenType::SELF;
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

bool sameType(const SemanticType& a, const SemanticType& b) {
    return a == b;
}

} // namespace

Codegen::Codegen(const Program& program, const Analyser& analyser) : program(program), analyser(analyser) {}

// ============================================================
// Output helpers
// ============================================================

void Codegen::line(const std::string& text) {
    if (text.empty()) {
        out << "\n";
        return;
    }
    out << std::string(static_cast<size_t>(indentLevel) * 4, ' ') << text << "\n";
}

std::string Codegen::temp(const std::string& base) {
    return "ds_" + base + std::to_string(tempCounter++);
}

const SemanticType& Codegen::typeOf(const Expr* e) const {
    const SemanticType* t = analyser.typeOf(e);
    return t ? *t : kUnknown;
}

// ============================================================
// Names & types
// ============================================================

std::string Codegen::mangle(const std::string& name) {
    static const std::unordered_set<std::string> reserved = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval",
        "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
        "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
        "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
        "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "final", "override",
        // names the generated code or C library relies on
        "std", "dessa", "main", "errno", "stdin", "stdout", "stderr", "EOF", "NULL", "assert",
        "INFINITY", "NAN", "offsetof", "setjmp", "va_arg", "va_start", "va_end", "min", "max",
    };
    if (reserved.count(name) || name.rfind("ds_", 0) == 0 || name.rfind("__", 0) == 0) return name + "_";
    return name;
}

std::string Codegen::ns(int id) {
    return "ds_m" + std::to_string(id);
}

std::string Codegen::typeName(const SemanticType& type) const {
    return ns(type.moduleId) + "::" + mangle(type.name);
}

bool Codegen::isOptionalValue(const SemanticType& type) const {
    if (type.kind != SemanticTypeKind::Union || !type.containsNil()) return false;
    SemanticType base = withoutNil(type);
    return base.kind != SemanticTypeKind::Union && !base.isReference() &&
           base.kind != SemanticTypeKind::Function && !base.isUnknown();
}

bool Codegen::isPlain(const SemanticType& type) const {
    return type.kind != SemanticTypeKind::Union && type.kind != SemanticTypeKind::Nil && !type.isUnknown();
}

std::string Codegen::cppType(const SemanticType& type) const {
    switch (type.kind) {
        case SemanticTypeKind::Unknown: return "dessa::Nil";
        case SemanticTypeKind::Nil: return "dessa::Nil";
        case SemanticTypeKind::Emp: return "void";
        case SemanticTypeKind::Str: return "dessa::Str";
        case SemanticTypeKind::Int: return "dessa::Int";
        case SemanticTypeKind::Bul: return "dessa::Bul";
        case SemanticTypeKind::Flt: return "dessa::Flt";
        case SemanticTypeKind::Dbl: return "dessa::Dbl";
        case SemanticTypeKind::Chr: return "dessa::Chr";
        case SemanticTypeKind::List:
            return "dessa::List<" + cppType(type.elementType ? *type.elementType : kUnknown) + ">";
        case SemanticTypeKind::Set:
            return "dessa::Set<" + cppType(type.elementType ? *type.elementType : kUnknown) + ">";
        case SemanticTypeKind::Map:
            return "dessa::Map<" + cppType(type.keyType ? *type.keyType : kUnknown) + ", " +
                   cppType(type.elementType ? *type.elementType : kUnknown) + ">";
        case SemanticTypeKind::UserDefined:
            if (type.isEnum()) return typeName(type);
            return "dessa::Ref<" + typeName(type) + ">";
        case SemanticTypeKind::Function: {
            std::string s = "dessa::Fn<" + cppType(type.returnType ? *type.returnType : SemanticType::simple(SemanticTypeKind::Emp)) + "(";
            for (size_t i = 0; i < type.parameterTypes.size(); ++i) s += (i ? ", " : "") + cppType(type.parameterTypes[i]);
            return s + ")>";
        }
        case SemanticTypeKind::Union: {
            SemanticType base = withoutNil(type);
            bool nil = type.containsNil();
            if (base.kind != SemanticTypeKind::Union) {
                if (!nil || base.isReference() || base.kind == SemanticTypeKind::Function) return cppType(base);
                return "dessa::Opt<" + cppType(base) + ">";
            }
            std::vector<std::string> members;
            for (const auto& m : base.unionMembers) members.push_back(cppType(m));
            std::sort(members.begin(), members.end());
            members.erase(std::unique(members.begin(), members.end()), members.end());
            std::string s = "std::variant<";
            for (size_t i = 0; i < members.size(); ++i) s += (i ? ", " : "") + members[i];
            return s + (nil ? ", dessa::Nil>" : ">");
        }
    }
    return "dessa::Nil";
}

std::string Codegen::globalName(const Symbol& symbol) const {
    if (!symbol.cppName.empty()) return symbol.cppName;
    const std::string& name = symbol.originalName.empty() ? symbol.name : symbol.originalName;
    return ns(symbol.moduleId) + "::" + mangle(name);
}

std::string Codegen::cppString(const std::string& text) {
    std::string s = "\"";
    bool hasNul = false;
    for (unsigned char c : text) {
        switch (c) {
            case '"': s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            case '?': s += "\\?"; break; // no trigraphs
            case 0: s += "\\0\"\""; hasNul = true; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03o", c);
                    s += buf;
                } else {
                    s += static_cast<char>(c);
                }
        }
    }
    s += "\"";
    if (hasNul) return "dessa::Str(" + s + ", " + std::to_string(text.size()) + ")";
    return "dessa::Str(" + s + ")";
}

std::string Codegen::cppChar(char c) {
    switch (c) {
        case '\'': return "'\\''";
        case '\\': return "'\\\\'";
        case '\n': return "'\\n'";
        case '\r': return "'\\r'";
        case '\t': return "'\\t'";
        case '\0': return "'\\0'";
        default:
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7f) {
                return "static_cast<char>(" + std::to_string(static_cast<int>(c)) + ")";
            }
            return std::string("'") + c + "'";
    }
}

// ============================================================
// Program
// ============================================================

std::string Codegen::generate() {
    out.str("");
    line("// Generated by the Dessa compiler. Do not edit.");
    line("#include \"dessa_runtime.hpp\"");
    for (const auto& header : analyser.builtinHeaders()) line("#include \"" + header + "\"");
    line("");

    emitTypeDeclarations();

    for (const auto& module : program.modules) emitGlobalsAndPrototypes(*module);
    for (const auto& module : program.modules) emitBodies(*module);
    for (const auto& module : program.modules) emitModuleEntry(*module);

    line("int main(int argc, char** argv) {");
    line("    return dessa::run_main(argc, argv, [] {");
    for (const auto& module : program.modules) {
        line("        " + ns(module->id) + (module->isEntry ? "::ds_run();" : "::ds_init();"));
    }
    line("    });");
    line("}");
    return out.str();
}

void Codegen::emitTypeDeclarations() {
    // Forward declarations and enums
    for (const auto& module : program.modules) {
        bool any = false;
        for (const auto& s : module->ast) {
            if (s && (s->kind == StmtKind::ClassDecl || s->kind == StmtKind::InterfaceDecl || s->kind == StmtKind::EnumDecl)) any = true;
        }
        if (!any) continue;
        line("namespace " + ns(module->id) + " {");
        for (const auto& s : module->ast) {
            if (!s) continue;
            if (s->kind == StmtKind::ClassDecl) line("struct " + mangle(static_cast<const ClassDeclStmt*>(s.get())->name) + ";");
            if (s->kind == StmtKind::InterfaceDecl) line("struct " + mangle(static_cast<const InterfaceDeclStmt*>(s.get())->name) + ";");
        }
        moduleId = module->id;
        for (const auto& s : module->ast) {
            if (!s || s->kind != StmtKind::EnumDecl) continue;
            const auto* decl = static_cast<const EnumDeclStmt*>(s.get());
            std::string name = mangle(decl->name);
            std::string values;
            for (const auto& v : decl->values) {
                values += (values.empty() ? "" : ", ") + mangle(v.name);
                if (v.value) values += " = " + expr(v.value->get());
            }
            line("enum class " + name + " : dessa::Int { " + values + " };");
            line("inline dessa::Str ds_enum_name(" + name + " value) {");
            line("    switch (value) {");
            for (const auto& v : decl->values) {
                line("        case " + name + "::" + mangle(v.name) + ": return \"" + v.name + "\";");
            }
            line("    }");
            line("    return \"" + decl->name + "(\" + std::to_string(static_cast<dessa::Int>(value)) + \")\";");
            line("}");
        }
        line("} // namespace " + ns(module->id));
        line("");
    }

    for (const auto& key : analyser.interfaceOrder()) {
        auto colon = key.find(':');
        SemanticType t = SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Interface);
        if (const InterfaceInfo* info = analyser.findInterface(t)) emitInterface(*info);
    }
    for (const auto& key : analyser.classOrder()) {
        auto colon = key.find(':');
        SemanticType t = SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class);
        if (const ClassInfo* info = analyser.findClass(t)) emitClass(*info);
    }
}

std::string Codegen::signature(const Symbol& symbol, const std::string& name) const {
    std::string s = cppType(symbol.returnType) + " " + name + "(";
    for (size_t i = 0; i < symbol.parameters.size(); ++i) {
        s += (i ? ", " : "") + cppType(symbol.parameters[i].type) + " " + mangle(symbol.parameters[i].name);
    }
    return s + ")";
}

void Codegen::emitInterface(const InterfaceInfo& info) {
    line("namespace " + ns(info.moduleId) + " {");
    line("struct " + mangle(info.name) + " : virtual dessa::Object {");
    for (const auto& field : info.fieldOrder) {
        line("    virtual " + cppType(info.fields.at(field)) + "& ds_get_" + field + "() = 0;");
    }
    for (const auto& method : info.methodOrder) {
        line("    virtual " + signature(info.methods.at(method), mangle(method)) + " = 0;");
    }
    line("};");
    line("} // namespace " + ns(info.moduleId));
    line("");
}

void Codegen::emitClass(const ClassInfo& info) {
    const std::string name = mangle(info.name);
    std::string bases;
    if (info.parent) bases = "public " + typeName(*info.parent);
    else bases = "virtual public dessa::Object";
    if (info.interface) bases += ", virtual public " + typeName(*info.interface);

    line("namespace " + ns(info.moduleId) + " {");
    line("struct " + name + " : " + bases + " {");
    indentLevel++;
    for (const auto& field : info.fieldOrder) {
        line(cppType(info.fields.at(field).type) + " " + mangle(field) + "{};");
    }
    // `init` runs as ds_construct_X after the object already lives in a
    // dessa::Ref (see dessa::create), so `self` can be used inside init.
    if (const Symbol* ctor = nearestConstructor(info)) {
        line(signature(*ctor, "ds_construct_" + name) + ";");
    } else {
        line("void ds_construct_" + name + "();");
    }
    line("void ds_fields_" + name + "();");
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::FuncDecl) continue;
        const auto* method = static_cast<const FuncDeclStmt*>(member.get());
        if (method->isConstructor) continue;
        const Symbol* symbol = analyser.functionOf(method);
        if (symbol) line("virtual " + signature(*symbol, mangle(method->name)) + ";");
    }
    if (info.interface) {
        if (const InterfaceInfo* iface = analyser.findInterface(*info.interface)) {
            for (const auto& field : iface->fieldOrder) {
                line(cppType(iface->fields.at(field)) + "& ds_get_" + field + "() override { return this->" + mangle(field) + "; }");
            }
        }
    }
    line("dessa::Str ds_class() const override { return \"" + info.name + "\"; }");
    line("dessa::Str ds_str() const override;");
    indentLevel--;
    line("};");
    line("} // namespace " + ns(info.moduleId));
    line("");
}

void Codegen::emitGlobalsAndPrototypes(const Module& module) {
    moduleId = module.id;
    std::vector<std::string> lines;
    for (const auto& s : module.ast) {
        if (!s) continue;
        if (s->kind == StmtKind::VarDecl) {
            const auto* decl = static_cast<const VarDeclStmt*>(s.get());
            const SemanticType* type = analyser.declaredTypeOf(decl);
            if (decl->name && type) lines.push_back(cppType(*type) + " " + mangle(*decl->name) + "{};");
        } else if (s->kind == StmtKind::FuncDecl) {
            const auto* decl = static_cast<const FuncDeclStmt*>(s.get());
            if (const Symbol* symbol = analyser.functionOf(decl)) lines.push_back(signature(*symbol, mangle(decl->name)) + ";");
        }
    }
    line("namespace " + ns(module.id) + " {");
    // Shown with runtime errors: "at <file>:<line>"
    std::string file = cppString(module.displayPath);
    file = file.substr(std::string("dessa::Str(").size(), file.size() - std::string("dessa::Str(").size() - 1);
    line("inline const char* const ds_file = " + file + ";");
    for (const auto& l : lines) line(l);
    line("void " + std::string(module.isEntry ? "ds_run" : "ds_init") + "();");
    line("} // namespace " + ns(module.id));
    line("");
}

void Codegen::emitBodies(const Module& module) {
    moduleId = module.id;
    line("namespace " + ns(module.id) + " {");
    line("");
    for (const auto& s : module.ast) {
        if (!s) continue;
        if (s->kind == StmtKind::ClassDecl) {
            const auto* decl = static_cast<const ClassDeclStmt*>(s.get());
            SemanticType t = SemanticType::userDefined(decl->name, module.id, UserTypeKind::Class);
            if (const ClassInfo* info = analyser.findClass(t)) emitClassBodies(*info);
        } else if (s->kind == StmtKind::FuncDecl) {
            const auto* decl = static_cast<const FuncDeclStmt*>(s.get());
            emitFunction(decl, mangle(decl->name));
        }
    }
    line("} // namespace " + ns(module.id));
    line("");
}

void Codegen::emitFunction(const FuncDeclStmt* decl, const std::string& qualifiedName) {
    const Symbol* symbol = analyser.functionOf(decl);
    if (!symbol || !decl->body) return;
    returnType = symbol->returnType;
    line(signature(*symbol, qualifiedName) + " {");
    indentLevel++;
    for (const auto& s : static_cast<const BlockStmt*>(decl->body.get())->statements) stmt(s.get());
    indentLevel--;
    line("}");
    line("");
    returnType.reset();
}

// The init that `new C(...)` runs: C's own, or the nearest ancestor's.
const Symbol* Codegen::nearestConstructor(const ClassInfo& info) const {
    for (const ClassInfo* k = &info; k; k = k->parent ? analyser.findClass(*k->parent) : nullptr) {
        if (k->constructor) return &*k->constructor;
    }
    return nullptr;
}

// Order: parent's init (explicit super.init(...) or the implicit no-argument
// one), then this class's field initialisers, then the rest of init's body.
// `ctor` is null for a class without an init.
void Codegen::emitConstructor(const ClassInfo& info, const FuncDeclStmt* ctor) {
    const std::string name = mangle(info.name);
    // Without its own init, a class takes its parent's init parameters and
    // passes them straight on.
    const Symbol* symbol = ctor ? analyser.functionOf(ctor) : nearestConstructor(info);
    std::string params;
    if (symbol) {
        for (size_t i = 0; i < symbol->parameters.size(); ++i) {
            params += (i ? ", " : "") + cppType(symbol->parameters[i].type) + " " + mangle(symbol->parameters[i].name);
        }
    }
    const auto* body = ctor ? static_cast<const BlockStmt*>(ctor->body.get()) : nullptr;

    returnType = SemanticType::simple(SemanticTypeKind::Emp);
    line("void " + name + "::ds_construct_" + name + "(" + params + ") {");
    indentLevel++;
    size_t first = 0;
    if (info.parent) {
        const std::string parent = typeName(*info.parent);
        const std::string parentConstruct = parent + "::ds_construct_" + mangle(info.parent->name);
        if (body && !body->statements.empty() && isSuperInitCall(body->statements.front().get())) {
            const auto* callExpr = static_cast<const CallExpr*>(
                static_cast<const ExpressionStmt*>(body->statements.front().get())->expression.get());
            const Symbol* parentCtor = analyser.memberOf(callExpr->callee.get());
            std::vector<FunctionParameterSymbol> parentParams = parentCtor ? parentCtor->parameters : std::vector<FunctionParameterSymbol>{};
            line(parentConstruct + "(" + arguments(callExpr, callExpr->args, parentParams) + ");");
            first = 1;
        } else if (!ctor && symbol) {
            std::string forwarded;
            for (size_t i = 0; i < symbol->parameters.size(); ++i) {
                forwarded += (i ? ", " : "") + mangle(symbol->parameters[i].name);
            }
            line(parentConstruct + "(" + forwarded + ");");
        } else {
            line(parentConstruct + "();");
        }
    }
    line("ds_fields_" + name + "();");
    if (body) {
        for (size_t i = first; i < body->statements.size(); ++i) stmt(body->statements[i].get());
    }
    indentLevel--;
    line("}");
    line("");
    returnType.reset();
}

void Codegen::emitClassBodies(const ClassInfo& info) {
    const ClassInfo* previous = currentClass;
    currentClass = &info;
    const std::string name = mangle(info.name);

    // Field initialisers run at the start of every constructor.
    line("void " + name + "::ds_fields_" + name + "() {");
    indentLevel++;
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::VarDecl) continue;
        const auto* field = static_cast<const VarDeclStmt*>(member.get());
        if (!field->name || !field->initializer) continue;
        line("this->" + mangle(*field->name) + " = " + exprAs(field->initializer.get(), info.fields.at(*field->name).type) + ";");
    }
    indentLevel--;
    line("}");
    line("");

    const FuncDeclStmt* ctor = nullptr;
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::FuncDecl) continue;
        const auto* method = static_cast<const FuncDeclStmt*>(member.get());
        if (method->isConstructor) ctor = method;
        else emitFunction(method, name + "::" + mangle(method->name));
    }
    emitConstructor(info, ctor);

    // How print() shows an instance: toStr() if the class defines one,
    // otherwise ClassName(field: value, ...).
    const ClassInfo* withToStr = nullptr;
    std::vector<const ClassInfo*> chain;
    for (const ClassInfo* k = &info; k; k = k->parent ? analyser.findClass(*k->parent) : nullptr) {
        chain.push_back(k);
        auto it = k->methods.find("toStr");
        if (!withToStr && it != k->methods.end() && it->second.parameters.empty() &&
            it->second.returnType.is(SemanticTypeKind::Str)) {
            withToStr = k;
        }
    }
    line("dessa::Str " + name + "::ds_str() const {");
    if (withToStr) {
        line("    return const_cast<" + name + "*>(this)->" + mangle("toStr") + "();");
    } else {
        std::string parts = "\"" + info.name + "(\"";
        bool firstField = true;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (const auto& f : (*it)->fieldOrder) {
                parts += ", \"" + std::string(firstField ? "" : ", ") + f + ": \", dessa::repr(this->" + mangle(f) + ")";
                firstField = false;
            }
        }
        parts += ", \")\"";
        line("    return dessa::cat(" + parts + ");");
    }
    line("}");
    line("");
    currentClass = previous;
}

void Codegen::emitModuleEntry(const Module& module) {
    moduleId = module.id;
    line("namespace " + ns(module.id) + " {");
    line(std::string("void ") + (module.isEntry ? "ds_run" : "ds_init") + "() {");
    indentLevel++;
    for (const auto& s : module.ast) {
        if (!s) continue;
        switch (s->kind) {
            case StmtKind::FuncDecl:
            case StmtKind::ClassDecl:
            case StmtKind::InterfaceDecl:
            case StmtKind::EnumDecl:
            case StmtKind::Import:
                break;
            case StmtKind::VarDecl:
                if (static_cast<const VarDeclStmt*>(s.get())->initializer && s->line > 0) {
                    line("dessa::trace(ds_file, " + std::to_string(s->line) + ");");
                }
                varDecl(static_cast<const VarDeclStmt*>(s.get()), true);
                break;
            default:
                // Only the entry file's top-level code runs.
                if (module.isEntry) stmt(s.get());
                break;
        }
    }
    indentLevel--;
    line("}");
    line("} // namespace " + ns(module.id));
    line("");
}

// ============================================================
// Statements
// ============================================================

void Codegen::block(const Stmt* statement) {
    if (statement && statement->kind == StmtKind::Block) {
        for (const auto& s : static_cast<const BlockStmt*>(statement)->statements) stmt(s.get());
    } else {
        stmt(statement);
    }
}

void Codegen::varDecl(const VarDeclStmt* decl, bool asGlobalAssignment) {
    if (!decl->name) return;
    const SemanticType* declared = analyser.declaredTypeOf(decl);
    if (!declared) return;
    const std::string name = mangle(*decl->name);
    if (asGlobalAssignment) {
        if (decl->initializer) line(name + " = " + exprAs(decl->initializer.get(), *declared) + ";");
        return;
    }
    if (decl->initializer && analyser.isRecursiveLambda(decl)) {
        // A local lambda that calls itself reaches itself through a shared
        // holder, so it keeps working even after the function returns it.
        const std::string type = cppType(*declared);
        const std::string box = "ds_rec_" + name;
        line("auto " + box + " = std::make_shared<" + type + ">();");
        line("*" + box + " = " + exprAs(decl->initializer.get(), *declared) + ";");
        line(type + " " + name + " = *" + box + ";");
    } else if (decl->initializer) {
        line(cppType(*declared) + " " + name + " = " + exprAs(decl->initializer.get(), *declared) + ";");
    } else {
        line(cppType(*declared) + " " + name + "{};");
    }
}

void Codegen::stmt(const Stmt* statement) {
    if (!statement) return;
    // Remember where we are, for runtime error messages.
    switch (statement->kind) {
        case StmtKind::Block:
        case StmtKind::FuncDecl:
        case StmtKind::ClassDecl:
        case StmtKind::InterfaceDecl:
        case StmtKind::EnumDecl:
        case StmtKind::Import:
        case StmtKind::Break:
        case StmtKind::Continue:
            break;
        case StmtKind::VarDecl:
            if (!static_cast<const VarDeclStmt*>(statement)->initializer) break;
            [[fallthrough]];
        default:
            if (statement->line > 0) line("dessa::trace(ds_file, " + std::to_string(statement->line) + ");");
    }
    switch (statement->kind) {
        case StmtKind::Expression: {
            const Expr* e = static_cast<const ExpressionStmt*>(statement)->expression.get();
            if (e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS) {
                line("throw dessa::Error(\"Not implemented yet (line " + std::to_string(statement->line) + ")\");");
                return;
            }
            if (isSuperInitCall(statement)) return; // handled by the constructor's initialiser list
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
                    init = cppType(t ? *t : kUnknown) + " " + mangle(d->name.value_or("ds_i"));
                    init += d->initializer ? " = " + exprAs(d->initializer.get(), t ? *t : kUnknown) : "{}";
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
            if (!s->value) {
                line("return;");
            } else if (returnType && !returnType->isUnknown() && !returnType->isVoidLike()) {
                line("return " + exprAs(s->value.get(), *returnType) + ";");
            } else {
                line("return " + expr(s->value.get()) + ";");
            }
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
            if (typeOf(value).is(SemanticTypeKind::Str)) line("throw dessa::Error(" + expr(value) + ");");
            else line("throw dessa::Thrown{" + expr(value) + "};");
            return;
        }
        default:
            return; // declarations are emitted elsewhere
    }
}

void Codegen::forIn(const ForInStmt* s) {
    const auto* decl = static_cast<const VarDeclStmt*>(s->declaration.get());
    const SemanticType* t = analyser.declaredTypeOf(decl);
    const std::string varType = cppType(t ? *t : kUnknown);
    const std::string name = mangle(decl->name.value_or("ds_item"));
    const SemanticType& iterable = typeOf(s->iterable.get());

    if (iterable.is(SemanticTypeKind::List)) {
        // Index-based so that appending inside the loop is safe.
        std::string seq = temp("seq"), i = temp("i");
        line("{");
        indentLevel++;
        line("auto&& " + seq + " = " + expr(s->iterable.get()) + ";");
        line("for (size_t " + i + " = 0; " + i + " < " + seq + ".size(); ++" + i + ") {");
        indentLevel++;
        line(varType + " " + name + " = " + seq + "[" + i + "];");
        block(s->body.get());
        indentLevel--;
        line("}");
        indentLevel--;
        line("}");
    } else if (iterable.is(SemanticTypeKind::Map)) {
        // Keys, in insertion order (over a copy, so the loop may change the map)
        std::string entry = temp("entry");
        line("for (const auto& " + entry + " : dessa::m_copy(" + expr(s->iterable.get()) + ")) {");
        indentLevel++;
        line(varType + " " + name + " = " + entry + ".first;");
        block(s->body.get());
        indentLevel--;
        line("}");
    } else {
        // Sets are iterated over a copy; Str by character.
        // Sets over a copy; Str character by character (not byte by byte)
        const std::string items = iterable.is(SemanticTypeKind::Str) ? "dessa::m_chars(" + expr(s->iterable.get()) + ")"
                                                                     : "dessa::m_copy(" + expr(s->iterable.get()) + ")";
        line("for (" + varType + " " + name + " : " + items + ") {");
        indentLevel++;
        block(s->body.get());
        indentLevel--;
        line("}");
    }
}

void Codegen::switchStmt(const SwitchStmt* s) {
    std::string value = temp("switch");
    const SemanticType& discriminantType = typeOf(s->discriminant.get());
    line("{");
    indentLevel++;
    line("const auto " + value + " = " + expr(s->discriminant.get()) + ";");
    bool first = true;
    const SwitchCase* defaultCase = nullptr;
    for (const auto& arm : s->cases) {
        if (!arm.value) { defaultCase = &arm; continue; }
        if (const SemanticType* enumType = analyser.enumCaseOf(arm.value.get())) {
            const std::string valueName = static_cast<const IdentifierExpr*>(arm.value.get())->name.lexeme;
            line(std::string(first ? "if (" : "} else if (") + value + " == " + typeName(*enumType) + "::" + mangle(valueName) + ") {");
            indentLevel++;
            block(arm.body.get());
            indentLevel--;
            first = false;
            continue;
        }
        std::string test = isPlain(discriminantType) && isPlain(typeOf(arm.value.get()))
            ? value + " == " + exprAs(arm.value.get(), discriminantType)
            : "dessa::eq(" + value + ", " + expr(arm.value.get()) + ")";
        line(std::string(first ? "if (" : "} else if (") + test + ") {");
        indentLevel++;
        block(arm.body.get());
        indentLevel--;
        first = false;
    }
    if (defaultCase) {
        if (first) {
            line("{");
        } else {
            line("} else {");
        }
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

void Codegen::tryStmt(const TryStmt* s) {
    line("try {");
    indentLevel++;
    block(s->tryBlock.get());
    indentLevel--;
    // Sort the arms into the two kinds of C++ exception Dessa throws:
    //   dessa::Thrown   - class instances, matched in order by dynamic cast
    //   std::exception  - Str messages and runtime errors
    // A catch-all arm handles whatever the other arms don't.
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

    auto body = [&](const CatchClause* clause) {
        indentLevel++;
        block(clause->body.get());
        indentLevel--;
    };

    if (!classArms.empty()) {
        line("} catch (const dessa::Thrown& ds_thrown) {");
        indentLevel++;
        for (size_t i = 0; i < classArms.size(); ++i) {
            const CatchClause* arm = classArms[i];
            const std::string name = mangle(*arm->param);
            line(std::string(i == 0 ? "if" : "} else if") + " (auto " + name + " = std::dynamic_pointer_cast<" +
                 typeName(*analyser.catchTypeOf(arm)) + ">(ds_thrown.value)) {");
            body(arm);
        }
        line("} else {");
        if (catchAll) body(catchAll);
        else line("    throw;");
        line("}");
        indentLevel--;
    }
    if (strArm) {
        line("} catch (const std::exception& ds_error) {");
        line("    dessa::Str " + mangle(*strArm->param) + " = ds_error.what();");
        body(strArm);
    }
    if (catchAll) {
        line("} catch (...) {");
        body(catchAll);
    }
    line("}");
}

// ============================================================
// Expressions
// ============================================================

namespace {
bool containsUnknown(const SemanticType& t) {
    if (t.isUnknown()) return true;
    if (t.elementType && containsUnknown(*t.elementType)) return true;
    if (t.returnType && containsUnknown(*t.returnType)) return true;
    for (const auto& m : t.unionMembers) if (containsUnknown(m)) return true;
    for (const auto& p : t.parameterTypes) if (containsUnknown(p)) return true;
    return false;
}
} // namespace

std::string Codegen::exprAs(const Expr* e, const SemanticType& target) {
    if (!e) return "";
    if (target.isVoidLike() || containsUnknown(target)) return expr(e);

    const Expr* inner = e;
    while (inner->kind == ExprKind::Grouping) inner = static_cast<const GroupingExpr*>(inner)->inner.get();

    if (isNilLiteral(inner)) return cppType(target) + "(dessa::nil)";

    if (inner->kind == ExprKind::List || inner->kind == ExprKind::Set) {
        bool isSet = inner->kind == ExprKind::Set;
        const SemanticTypeKind want = isSet ? SemanticTypeKind::Set : SemanticTypeKind::List;
        const SemanticType* collectionType = target.kind == want ? &target : nullptr;
        if (!collectionType && target.kind == SemanticTypeKind::Union) {
            for (const auto& m : target.unionMembers) if (m.kind == want) collectionType = &m;
        }
        if (collectionType) {
            const auto& elements = isSet ? static_cast<const SetExpr*>(inner)->elements : static_cast<const ListExpr*>(inner)->elements;
            std::string built = collection(elements, isSet, inner, collectionType);
            // collection() uses the expected element type recorded by the analyser
            if (!sameType(*collectionType, target)) return cppType(target) + "(" + built + ")";
            return built;
        }
    }

    // {} or {"a": 1} where a map is expected
    const SemanticType* targetMap = target.kind == SemanticTypeKind::Map ? &target : nullptr;
    if (!targetMap && target.kind == SemanticTypeKind::Union) {
        for (const auto& m : target.unionMembers) if (m.kind == SemanticTypeKind::Map) targetMap = &m;
    }
    if (targetMap && inner->kind == ExprKind::Set && static_cast<const SetExpr*>(inner)->elements.empty()) {
        return cppType(target) + "(" + cppType(*targetMap) + "{})";
    }
    if (targetMap && inner->kind == ExprKind::Map) {
        std::string built = mapLiteral(static_cast<const MapExpr*>(inner), targetMap);
        return sameType(*targetMap, target) ? built : cppType(target) + "(" + built + ")";
    }

    if (inner->kind == ExprKind::Ternary) {
        const auto* t = static_cast<const TernaryExpr*>(inner);
        return "(" + expr(t->condition.get()) + " ? " + exprAs(t->thenBranch.get(), target) + " : " +
               exprAs(t->elseBranch.get(), target) + ")";
    }

    // A lambda going into a Func: wrap it, so that e.g. both branches of
    // `c ? (x[Int]) := x : (x[Int]) := -x` have the same C++ type.
    if (inner->kind == ExprKind::Lambda) {
        if (withoutNil(target).kind == SemanticTypeKind::Function) return cppType(target) + "(" + expr(e) + ")";
        return expr(e);
    }
    const SemanticType& source = typeOf(e);
    if (source.isUnknown() || sameType(source, target)) return expr(e);
    return "static_cast<" + cppType(target) + ">(" + expr(e) + ")";
}

std::string Codegen::expr(const Expr* e) {
    if (!e) return "";
    switch (e->kind) {
        case ExprKind::Literal: return literal(static_cast<const LiteralExpr*>(e));
        case ExprKind::TemplateString: return templateString(static_cast<const TemplateStringExpr*>(e));
        case ExprKind::Identifier: return identifier(static_cast<const IdentifierExpr*>(e));
        case ExprKind::Unary: {
            const auto* u = static_cast<const UnaryExpr*>(e);
            // -x on an Int is checked: -(-9223372036854775808) doesn't fit
            if (u->op.type == TokenType::MINUS && typeOf(u->operand.get()).isInteger()) {
                return "dessa::neg(" + expr(u->operand.get()) + ")";
            }
            return "(" + u->op.lexeme + expr(u->operand.get()) + ")";
        }
        case ExprKind::Postfix: {
            const auto* p = static_cast<const PostfixExpr*>(e);
            const SemanticType& t = typeOf(p->operand.get());
            if (t.isInteger()) {
                return std::string(p->op.type == TokenType::PLUS_PLUS ? "dessa::post_inc(" : "dessa::post_dec(") +
                       expr(p->operand.get()) + ")";
            }
            return "(" + expr(p->operand.get()) + p->op.lexeme + ")";
        }
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
            return "dessa::at(" + expr(i->object.get()) + ", " + expr(i->index.get()) + ")";
        }
        case ExprKind::Member: return member(static_cast<const MemberExpr*>(e));
        case ExprKind::Lambda: return lambda(static_cast<const LambdaExpr*>(e));
        case ExprKind::New: {
            const auto* n = static_cast<const NewExpr*>(e);
            const SemanticType& type = typeOf(e);
            const ClassInfo* info = analyser.findClass(type);
            std::vector<FunctionParameterSymbol> params;
            if (const Symbol* ctor = info ? nearestConstructor(*info) : nullptr) params = ctor->parameters;
            std::string hoist;
            std::string args = arguments(e, n->args, params, &hoist);
            const std::string cls = typeName(type);
            return withHoist(hoist, "dessa::create<" + cls + ">([&](" + cls + "& ds_new) { ds_new.ds_construct_" +
                                    mangle(type.name) + "(" + args + "); })");
        }
        case ExprKind::List: return collection(static_cast<const ListExpr*>(e)->elements, false, e);
        case ExprKind::Set: return collection(static_cast<const SetExpr*>(e)->elements, true, e);
        case ExprKind::Grouping: return "(" + expr(static_cast<const GroupingExpr*>(e)->inner.get()) + ")";
        case ExprKind::Map: return mapLiteral(static_cast<const MapExpr*>(e), nullptr);
        case ExprKind::Convert: {
            const auto* c = static_cast<const ConvertExpr*>(e);
            static const std::unordered_map<std::string, std::string> functions = {
                {"Str", "dessa::to_str"}, {"Int", "dessa::to_int"}, {"Dbl", "dessa::to_dbl"},
                {"Flt", "dessa::to_flt"}, {"Bul", "dessa::to_bul"}, {"Chr", "dessa::to_chr"},
            };
            auto it = functions.find(c->type.lexeme);
            if (it == functions.end()) return expr(c->value.get());
            // Str(mask) keeps an Int[16]'s base: "0xff"
            std::string value = c->type.lexeme == "Str" ? printable(c->value.get()) : expr(c->value.get());
            return it->second + "(" + value + ")";
        }
        case ExprKind::Is: {
            const auto* is = static_cast<const IsExpr*>(e);
            // Resolve the checked type the same way the analyser did.
            const SemanticType* target = analyser.isTargetOf(is);
            if (!target) return "false";
            std::string type = target->isReference() ? typeName(*target) : cppType(*target);
            return "dessa::is<" + type + ">(" + expr(is->value.get()) + ")";
        }
    }
    return "";
}

std::string Codegen::literal(const LiteralExpr* e) {
    const std::string& text = e->token.lexeme;
    switch (e->token.type) {
        case TokenType::INT_LITERAL:
            return text + "LL";
        case TokenType::HEX_LITERAL: return text + "LL";
        case TokenType::BINARY_LITERAL: return text + "LL";
        case TokenType::OCTAL_LITERAL: return "0" + text.substr(2) + "LL";
        case TokenType::FLOAT_LITERAL: {
            std::string digits = text.substr(0, text.size() - 1);
            if (digits.find('.') == std::string::npos) digits += ".0";
            return digits + "f";
        }
        case TokenType::DOUBLE_LITERAL: {
            std::string digits = text.back() == 'd' ? text.substr(0, text.size() - 1) : text;
            if (digits.find('.') == std::string::npos) digits += ".0";
            return digits;
        }
        case TokenType::CHAR_LITERAL: {
            // The lexeme holds the character's UTF-8 bytes: 'é' is two bytes, one Chr.
            if (text.empty()) return "dessa::Chr(0)";
            unsigned char b = static_cast<unsigned char>(text[0]);
            if (b < 0x80) return "dessa::Chr(" + cppChar(text[0]) + ")";
            int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
            unsigned long cp = extra == 3 ? (b & 0x07) : extra == 2 ? (b & 0x0F) : (b & 0x1F);
            for (int k = 1; k <= extra && k < static_cast<int>(text.size()); ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(text[k]) & 0x3F);
            }
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "dessa::Chr(0x%lX)", cp);
            return buffer;
        }
        case TokenType::STRING: return cppString(text);
        case TokenType::TRUE: return "true";
        case TokenType::FALSE: return "false";
        case TokenType::NIL: return "dessa::nil";
        default: return "/* ? */";
    }
}

std::string Codegen::identifier(const IdentifierExpr* e) {
    if (e->name.type == TokenType::SELF) {
        return currentClass ? "dessa::self_ref(this)" : "nullptr";
    }
    const Symbol* symbol = analyser.symbolOf(e);
    if (!symbol) return mangle(e->name.lexeme);
    if (analyser.isRecursiveRef(e) && !symbol->isGlobal) return "(*ds_rec_" + mangle(symbol->name) + ")";

    std::string name;
    switch (symbol->kind) {
        case SymbolKind::Field:
        case SymbolKind::Method:
            name = "this->" + mangle(symbol->name);
            break;
        case SymbolKind::Builtin:
            name = symbol->cppName;
            break;
        case SymbolKind::Function:
        case SymbolKind::Class:
        case SymbolKind::Interface:
        case SymbolKind::Enum:
            name = globalName(*symbol);
            break;
        default:
            name = symbol->isGlobal ? globalName(*symbol) : mangle(symbol->name);
            break;
    }

    // Narrowed: by `!= nil` (Int | Nil -> Int), by `is` (Account -> SavingsAccount,
    // Int | Str -> Int) or by an assignment.
    const SemanticType& now = typeOf(e);
    const SemanticType& declared = symbol->type;
    if (now.isUnknown() || now == declared) return name;
    if (isOptionalValue(declared) && now == withoutNil(declared)) return "dessa::unwrap(" + name + ")";
    if (now.isReference()) {
        if (withoutNil(declared) == now) return name; // Ref<T> already; nil was ruled out
        return "dessa::as<" + typeName(now) + ">(" + name + ")";
    }
    if (withoutNil(declared).kind == SemanticTypeKind::Union) {
        return "dessa::unwrap_to<" + cppType(now) + ">(" + name + ")";
    }
    return name;
}

std::string Codegen::binary(const BinaryExpr* e) {
    const Expr* l = e->left.get();
    const Expr* r = e->right.get();
    const SemanticType& lt = typeOf(l);
    const SemanticType& rt = typeOf(r);

    switch (e->op.type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL: {
            bool negate = e->op.type == TokenType::BANG_EQUAL;
            if (isNilLiteral(l) || isNilLiteral(r)) {
                const Expr* other = isNilLiteral(l) ? r : l;
                // Use the raw variable: a narrowed name would be unwrapped.
                std::string value = expr(other);
                if (other->kind == ExprKind::Identifier) {
                    if (const Symbol* s = analyser.symbolOf(other); s && s->type.containsNil()) {
                        value = (s->kind == SymbolKind::Field) ? "this->" + mangle(s->name)
                              : s->isGlobal ? globalName(*s) : mangle(s->name);
                    }
                }
                return std::string("(") + (negate ? "" : "!") + "dessa::has(" + value + "))";
            }
            if (isPlain(lt) && isPlain(rt)) return "(" + expr(l) + (negate ? " != " : " == ") + expr(r) + ")";
            return std::string("(") + (negate ? "!" : "") + "dessa::eq(" + expr(l) + ", " + expr(r) + "))";
        }
        case TokenType::QUESTION_QUESTION: {
            const SemanticType& result = typeOf(e);
            std::string type = cppType(result);
            return "dessa::coalesce<" + type + ">(" + expr(l) + ", [&]() -> " + type + " { return " + exprAs(r, result) + "; })";
        }
        case TokenType::DIV: return "dessa::div(" + expr(l) + ", " + expr(r) + ")";
        case TokenType::MOD: return "dessa::mod(" + expr(l) + ", " + expr(r) + ")";
        case TokenType::EXPONENT: return "dessa::pow(" + expr(l) + ", " + expr(r) + ")";
        case TokenType::BITWISE_XOR: return "(" + expr(l) + " ^ " + expr(r) + ")";
        case TokenType::MINUS:
        case TokenType::MULT:
            // Int arithmetic is checked: going past the Int range is an error, not a wrap-around
            if (lt.isInteger() && rt.isInteger()) {
                return std::string(e->op.type == TokenType::MINUS ? "dessa::sub(" : "dessa::mul(") + expr(l) + ", " + expr(r) + ")";
            }
            return "(" + expr(l) + " " + e->op.lexeme + " " + expr(r) + ")";
        case TokenType::L_ANGLE: return "(" + expr(l) + " < " + expr(r) + ")";
        case TokenType::R_ANGLE: return "(" + expr(l) + " > " + expr(r) + ")";
        case TokenType::PLUS:
            if (lt.isInteger() && rt.isInteger()) return "dessa::add(" + expr(l) + ", " + expr(r) + ")";
            // "a" + 'é': a Chr is appended as its UTF-8 text
            if (lt.is(SemanticTypeKind::Chr) || rt.is(SemanticTypeKind::Chr)) {
                auto text = [&](const Expr* side, const SemanticType& t) {
                    return t.is(SemanticTypeKind::Chr) ? "dessa::to_str(" + expr(side) + ")" : expr(side);
                };
                return "(" + text(l, lt) + " + " + text(r, rt) + ")";
            }
            return "(" + expr(l) + " + " + expr(r) + ")";
        default:
            return "(" + expr(l) + " " + e->op.lexeme + " " + expr(r) + ")";
    }
}

std::string Codegen::assign(const AssignExpr* e) {
    const Expr* target = e->target.get();
    const SemanticType& targetType = typeOf(target);

    // Write to the variable itself, never to an unwrapped copy.
    std::string lhs = expr(target);
    std::string current = lhs;
    if (target->kind == ExprKind::Identifier) {
        if (const Symbol* s = analyser.symbolOf(target)) {
            std::string raw = (s->kind == SymbolKind::Field) ? "this->" + mangle(s->name)
                            : s->isGlobal ? globalName(*s) : mangle(s->name);
            lhs = raw;
            current = isOptionalValue(s->type) ? "dessa::unwrap(" + raw + ")" : raw;
        }
    }

    if (e->op.type == TokenType::EQUAL) {
        if (target->kind == ExprKind::Index) {
            const auto* index = static_cast<const IndexExpr*>(target);
            const SemanticType& mapType = typeOf(index->object.get());
            if (mapType.is(SemanticTypeKind::Map)) {
                // ages<"Cy"> = 41 -- adds or replaces the entry
                return "dessa::put(" + expr(index->object.get()) + ", " +
                       exprAs(index->index.get(), mapType.keyType ? *mapType.keyType : kUnknown) + ", " +
                       exprAs(e->value.get(), targetType) + ")";
            }
        }
        return "(" + lhs + " = " + exprAs(e->value.get(), targetType) + ")";
    }

    std::string value = expr(e->value.get());
    switch (e->op.type) {
        case TokenType::DIV_EQUAL:
            // x /= 2 on an Int keeps it an Int (the decimal result is truncated)
            return "(" + lhs + " = static_cast<" + cppType(withoutNil(targetType)) + ">(dessa::div(" + current + ", " + value + ")))";
        case TokenType::MOD_EQUAL: return "(" + lhs + " = dessa::mod(" + current + ", " + value + "))";
        case TokenType::EXPONENT_EQUAL: return "(" + lhs + " = dessa::pow(" + current + ", " + value + "))";
        default:
            // text += 'é' appends the character's UTF-8 text
            if (e->op.type == TokenType::PLUS_EQUAL && typeOf(e->value.get()).is(SemanticTypeKind::Chr)) {
                return "(" + current + " += dessa::to_str(" + value + "))";
            }
            if (targetType.isInteger() && typeOf(e->value.get()).isInteger() &&
                (e->op.type == TokenType::PLUS_EQUAL || e->op.type == TokenType::MINUS_EQUAL || e->op.type == TokenType::MULT_EQUAL)) {
                const char* fn = e->op.type == TokenType::PLUS_EQUAL ? "dessa::add(" : e->op.type == TokenType::MINUS_EQUAL ? "dessa::sub(" : "dessa::mul(";
                return "(" + lhs + " = " + fn + current + ", " + value + "))";
            }
            return "(" + current + " " + e->op.lexeme + " " + value + ")";
    }
}

bool Codegen::hasSideEffects(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case ExprKind::Call:
        case ExprKind::Assign:
        case ExprKind::Postfix:
        case ExprKind::New:
            return true;
        case ExprKind::Unary: return hasSideEffects(static_cast<const UnaryExpr*>(e)->operand.get());
        case ExprKind::Binary: {
            const auto* b = static_cast<const BinaryExpr*>(e);
            return hasSideEffects(b->left.get()) || hasSideEffects(b->right.get());
        }
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            return hasSideEffects(t->condition.get()) || hasSideEffects(t->thenBranch.get()) || hasSideEffects(t->elseBranch.get());
        }
        case ExprKind::Index: {
            const auto* i = static_cast<const IndexExpr*>(e);
            return hasSideEffects(i->object.get()) || hasSideEffects(i->index.get());
        }
        case ExprKind::Member: return hasSideEffects(static_cast<const MemberExpr*>(e)->object.get());
        case ExprKind::Grouping: return hasSideEffects(static_cast<const GroupingExpr*>(e)->inner.get());
        case ExprKind::Is: return hasSideEffects(static_cast<const IsExpr*>(e)->value.get());
        case ExprKind::Convert: return hasSideEffects(static_cast<const ConvertExpr*>(e)->value.get());
        case ExprKind::Map:
            for (const auto& entry : static_cast<const MapExpr*>(e)->entries) {
                if (hasSideEffects(entry.key.get()) || hasSideEffects(entry.value.get())) return true;
            }
            return false;
        case ExprKind::TemplateString:
            for (const auto& i : static_cast<const TemplateStringExpr*>(e)->interpolations) if (hasSideEffects(i.get())) return true;
            return false;
        case ExprKind::List:
            for (const auto& el : static_cast<const ListExpr*>(e)->elements) if (hasSideEffects(el.value.get())) return true;
            return false;
        case ExprKind::Set:
            for (const auto& el : static_cast<const SetExpr*>(e)->elements) if (hasSideEffects(el.value.get())) return true;
            return false;
        default:
            return false;
    }
}

std::string Codegen::withHoist(const std::string& hoist, const std::string& callCode) {
    if (hoist.empty()) return callCode;
    return "[&] { " + hoist + "return " + callCode + "; }()";
}

std::string Codegen::arguments(const Expr* callExpr, const std::vector<Argument>& args,
                               const std::vector<FunctionParameterSymbol>& params, std::string* hoist) {
    const ArgumentOrder* order = analyser.argumentOrder(callExpr);
    const bool ordered = order && order->size() == params.size();

    // Code for each argument, by source position, converted to the type of
    // the parameter it feeds (a variadic parameter's element type, unless
    // the argument is a ...spread list).
    std::vector<std::string> code(args.size());
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
        code[a] = exprAs(args[a].value.get(), *p);
    }

    bool needsHoist = hoist && args.size() >= 2 &&
        std::any_of(args.begin(), args.end(), [](const Argument& a) { return hasSideEffects(a.value.get()); });
    if (needsHoist) {
        for (size_t a = 0; a < args.size(); ++a) {
            std::string name = temp("arg");
            *hoist += "auto " + name + " = " + code[a] + "; ";
            code[a] = "std::move(" + name + ")";
        }
    }

    std::string s;
    if (!ordered) {
        for (size_t a = 0; a < args.size(); ++a) s += (a ? ", " : "") + code[a];
        return s;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& sources = (*order)[i];
        const FunctionParameterSymbol& p = params[i];
        std::string value;
        if (p.variadic) {
            // greet("a", "b", ...more) -> one list
            const SemanticType element = p.type.elementType ? *p.type.elementType : kUnknown;
            bool spread = std::any_of(sources.begin(), sources.end(), [&](int a) { return args[static_cast<size_t>(a)].isSpread; });
            if (!spread) {
                value = "dessa::List<" + cppType(element) + ">{";
                for (size_t k = 0; k < sources.size(); ++k) value += (k ? ", " : "") + code[static_cast<size_t>(sources[k])];
                value += "}";
            } else {
                value = "dessa::ListBuilder<" + cppType(element) + ">()";
                for (int a : sources) {
                    value += std::string(args[static_cast<size_t>(a)].isSpread ? ".spread(" : ".add(") + code[static_cast<size_t>(a)] + ")";
                }
                value += ".build()";
            }
        } else if (!sources.empty()) {
            value = code[static_cast<size_t>(sources.front())];
        } else if (p.defaultValue) {
            // Defaults are constants, so evaluating them here is safe.
            value = exprAs(p.defaultValue, p.type);
        } else {
            continue;
        }
        s += (s.empty() ? "" : ", ") + value;
    }
    return s;
}

std::string Codegen::printable(const Expr* e) {
    const SemanticType& t = typeOf(e);
    if (t.is(SemanticTypeKind::Int) && t.radix && *t.radix != 10) {
        return "dessa::Radix{" + expr(e) + ", " + std::to_string(*t.radix) + "}";
    }
    return expr(e);
}

std::string Codegen::call(const CallExpr* e) {
    const Expr* callee = e->callee.get();

    if (callee->kind == ExprKind::Identifier) {
        const Symbol* symbol = analyser.symbolOf(callee);
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->variadic) {
            bool order = e->args.size() >= 2 &&
                std::any_of(e->args.begin(), e->args.end(), [](const Argument& a) { return hasSideEffects(a.value.get()); });
            std::string hoist, args;
            std::string sep, end; // print's named options
            for (const auto& arg : e->args) {
                std::string part = arg.name ? exprAs(arg.value.get(), SemanticType::simple(SemanticTypeKind::Str))
                                            : printable(arg.value.get());
                if (order) {
                    std::string name = temp("arg");
                    hoist += "auto " + name + " = " + part + "; ";
                    part = name;
                }
                if (arg.name && *arg.name == "sep") sep = part;
                else if (arg.name && *arg.name == "end") end = part;
                else args += (args.empty() ? "" : ", ") + part;
            }
            if (!sep.empty() || !end.empty()) {
                if (sep.empty()) sep = "dessa::Str(\" \")";
                if (end.empty()) end = "dessa::Str(\"\\n\")";
                return withHoist(hoist, "dessa::print_with(" + sep + ", " + end + (args.empty() ? "" : ", " + args) + ")");
            }
            return withHoist(hoist, symbol->cppName + "(" + args + ")");
        }
        // typeof(x): the static type's name, checked at run time where it can vary
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->cppName == "dessa::type_of" && e->args.size() == 1) {
            const Expr* value = e->args[0].value.get();
            std::string known = typeOf(value).toString();
            return "dessa::type_of(" + expr(value) + ", " + cppString(known) + ")";
        }
        // zip(a, b) / zip(a, b, combine)
        if (symbol && symbol->kind == SymbolKind::Builtin && symbol->cppName == "dessa::zip") {
            std::string a = expr(e->args[0].value.get());
            std::string b = expr(e->args[1].value.get());
            if (e->args.size() == 3) return "dessa::zip_with(" + a + ", " + b + ", " + expr(e->args[2].value.get()) + ")";
            SemanticType result = typeOf(e);
            const SemanticType& pair = *result.elementType;
            return "dessa::zip<" + cppType(*pair.elementType) + ">(" + a + ", " + b + ")";
        }
        if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Builtin || symbol->kind == SymbolKind::Method)) {
            std::string name = symbol->kind == SymbolKind::Method ? "this->" + mangle(symbol->name) : globalName(*symbol);
            std::string hoist;
            std::string args = arguments(e, e->args, symbol->parameters, &hoist);
            return withHoist(hoist, name + "(" + args + ")");
        }
    }

    if (callee->kind == ExprKind::Member) {
        const auto* m = static_cast<const MemberExpr*>(callee);
        const Symbol* symbol = analyser.memberOf(callee);
        if (symbol && symbol->kind == SymbolKind::Method) {
            std::string hoist;
            std::string args = arguments(e, e->args, symbol->parameters, &hoist);
            const Expr* object = m->object.get();

            if (isSuper(object)) {
                SemanticType parent = typeOf(object);
                return withHoist(hoist, typeName(parent) + "::" + mangle(m->name.lexeme) + "(" + args + ")");
            }

            bool chained = analyser.isOptionalChain(callee);
            std::string objectCode = expr(object);
            std::string invoke;
            if (!symbol->cppName.empty()) {
                // Builtin method: dessa::m_len(value, ...)
                auto build = [&](const std::string& receiver) {
                    return symbol->cppName + "(" + receiver + (args.empty() ? "" : ", " + args) + ")";
                };
                if (!chained) return withHoist(hoist, build(objectCode));
                invoke = build("ds_o");
            } else {
                if (!chained) {
                    if (isSelf(object)) return withHoist(hoist, "this->" + mangle(m->name.lexeme) + "(" + args + ")");
                    return withHoist(hoist, "dessa::ref(" + objectCode + ")->" + mangle(m->name.lexeme) + "(" + args + ")");
                }
                invoke = "ds_o->" + mangle(m->name.lexeme) + "(" + args + ")";
            }
            const SemanticType& result = typeOf(e);
            if (result.isVoidLike() || symbol->returnType.isVoidLike()) {
                return withHoist(hoist, "dessa::chain_void(" + objectCode + ", [&](auto&& ds_o) { " + invoke + "; })");
            }
            return withHoist(hoist, "dessa::chain<" + cppType(result) + ">(" + objectCode + ", [&](auto&& ds_o) { return " + invoke + "; })");
        }
    }

    // A value holding a function (lambda variable, field, ...)
    const SemanticType fnType = withoutNil(typeOf(callee));
    std::vector<FunctionParameterSymbol> params;
    for (const auto& p : fnType.parameterTypes) params.push_back({"", p});
    std::string hoist;
    std::string args = arguments(e, e->args, params, &hoist);
    return withHoist(hoist, expr(callee) + "(" + args + ")");
}

std::string Codegen::member(const MemberExpr* e) {
    const Symbol* symbol = analyser.memberOf(e);
    const Expr* object = e->object.get();
    const SemanticType& objectType = typeOf(object);

    if (!symbol) return "/* unknown member */";

    // Enum value
    if (symbol->kind == SymbolKind::Constant && objectType.isEnum() && analyser.symbolOf(object) &&
        analyser.symbolOf(object)->kind == SymbolKind::Enum) {
        return typeName(objectType) + "::" + mangle(e->name.lexeme);
    }

    std::string access;
    SemanticType base = withoutNil(objectType);
    if (symbol->kind == SymbolKind::Field) {
        access = base.isInterface() ? "ds_get_" + e->name.lexeme + "()" : mangle(e->name.lexeme);
    } else {
        access = mangle(e->name.lexeme); // method reference (only valid when called)
    }

    bool chained = analyser.isOptionalChain(e);
    if (!chained) {
        if (isSelf(object) || isSuper(object)) return "this->" + access;
        return "dessa::ref(" + expr(object) + ")->" + access;
    }
    const SemanticType& result = typeOf(e);
    return "dessa::chain<" + cppType(result) + ">(" + expr(object) + ", [&](auto&& ds_o) { return ds_o->" + access + "; })";
}

std::string Codegen::lambda(const LambdaExpr* e) {
    const SemanticType& type = typeOf(e);
    const SemanticType* expected = analyser.expectedTypeOf(e);
    const SemanticType& fn = (expected && expected->kind == SemanticTypeKind::Function) ? *expected : type;
    SemanticType ret = fn.returnType ? *fn.returnType : SemanticType::simple(SemanticTypeKind::Emp);
    if (type.returnType && !type.returnType->isUnknown() && (ret.isUnknown())) ret = *type.returnType;

    std::string params;
    for (size_t i = 0; i < e->params.size(); ++i) {
        const SemanticType& p = i < type.parameterTypes.size() ? type.parameterTypes[i] : kUnknown;
        params += (i ? ", " : "") + cppType(p) + " " + mangle(e->params[i].name);
    }
    std::string capture = currentClass ? "[=, this]" : "[=]";
    std::string header = capture + "(" + params + ") -> " + cppType(ret) + " ";

    auto savedReturn = returnType;
    returnType = ret;
    std::string body;
    if (e->body && e->body->kind == StmtKind::Expression) {
        const Expr* value = static_cast<const ExpressionStmt*>(e->body.get())->expression.get();
        body = ret.isVoidLike() ? "{ " + expr(value) + "; }" : "{ return " + exprAs(value, ret) + "; }";
    } else if (e->body) {
        // Render the block into a separate buffer at the current indentation.
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
    return header + body;
}

std::string Codegen::collection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at,
                                const SemanticType* target) {
    const SemanticType* expected = target ? target : analyser.expectedTypeOf(at);
    SemanticType type = typeOf(at);
    const SemanticTypeKind want = isSet ? SemanticTypeKind::Set : SemanticTypeKind::List;
    if (expected && expected->kind == want) {
        type = *expected;
    } else if (expected && expected->kind == SemanticTypeKind::Union) {
        for (const auto& m : expected->unionMembers) if (m.kind == want) type = m;
    }
    SemanticType element = type.elementType ? *type.elementType : kUnknown;
    std::string elementType = cppType(element);

    bool spread = std::any_of(elements.begin(), elements.end(), [](const CollectionElement& c) { return c.isSpread; });
    if (!spread) {
        std::string s = std::string(isSet ? "dessa::Set<" : "dessa::List<") + elementType + ">{";
        for (size_t i = 0; i < elements.size(); ++i) s += (i ? ", " : "") + exprAs(elements[i].value.get(), element);
        return s + "}";
    }
    std::string s = std::string(isSet ? "dessa::SetBuilder<" : "dessa::ListBuilder<") + elementType + ">()";
    for (const auto& el : elements) {
        if (el.isSpread) s += ".spread(" + expr(el.value.get()) + ")";
        else s += ".add(" + exprAs(el.value.get(), element) + ")";
    }
    return s + ".build()";
}

// {"a": 1, ...other}. `target` is the map type wanted (or null: the literal's own type).
std::string Codegen::mapLiteral(const MapExpr* e, const SemanticType* target) {
    SemanticType type = target ? *target : typeOf(e);
    if (!target) {
        if (const SemanticType* expected = analyser.expectedTypeOf(e); expected && expected->kind == SemanticTypeKind::Map) type = *expected;
    }
    const SemanticType key = type.keyType ? *type.keyType : kUnknown;
    const SemanticType value = type.elementType ? *type.elementType : kUnknown;
    const std::string args = cppType(key) + ", " + cppType(value);

    bool spread = std::any_of(e->entries.begin(), e->entries.end(), [](const MapEntry& m) { return !m.key; });
    if (!spread) {
        std::string s = "dessa::Map<" + args + ">{";
        for (size_t i = 0; i < e->entries.size(); ++i) {
            s += std::string(i ? ", " : "") + "{" + exprAs(e->entries[i].key.get(), key) + ", " +
                 exprAs(e->entries[i].value.get(), value) + "}";
        }
        return s + "}";
    }
    std::string s = "dessa::MapBuilder<" + args + ">()";
    for (const auto& entry : e->entries) {
        if (!entry.key) s += ".spread(" + expr(entry.value.get()) + ")";
        else s += ".put(" + exprAs(entry.key.get(), key) + ", " + exprAs(entry.value.get(), value) + ")";
    }
    return s + ".build()";
}

std::string Codegen::templateString(const TemplateStringExpr* e) {
    std::string args;
    for (size_t i = 0; i < e->literalParts.size(); ++i) {
        if (!e->literalParts[i].empty()) args += (args.empty() ? "" : ", ") + cppString(e->literalParts[i]);
        if (i < e->interpolations.size()) args += (args.empty() ? "" : ", ") + printable(e->interpolations[i].get());
    }
    if (args.empty()) return "dessa::Str()";
    return "dessa::cat(" + args + ")";
}

} // namespace dessa
