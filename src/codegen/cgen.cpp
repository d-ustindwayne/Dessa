// The C backend: program structure, types and statements. Expressions are
// in cgen_expr.cpp. See cgen.hpp for how values are counted.
#include "cgen.hpp"
#include "cglue.hpp"

#include <algorithm>
#include <functional>

namespace dessa {

namespace {

struct Unsupported {
    std::string what;
};

const SemanticType kUnknown = SemanticType::unknown();

std::string cString(const std::string& text) {
    std::string s = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': s += "\\\""; break;
            case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break;
            case '\r': s += "\\r"; break;
            case '\t': s += "\\t"; break;
            case '?': s += "\\?"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03o", c);
                    s += buf;
                } else {
                    s += static_cast<char>(c);
                }
        }
    }
    return s + "\"";
}

size_t utf8Count(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++n) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= s.size() || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) ok = false;
        }
        i += ok ? 1 + extra : 1;
    }
    return n;
}

bool isSuperInitCall(const Stmt* s) {
    if (!s || s->kind != StmtKind::Expression) return false;
    const Expr* e = static_cast<const ExpressionStmt*>(s)->expression.get();
    if (!e || e->kind != ExprKind::Call) return false;
    const Expr* callee = static_cast<const CallExpr*>(e)->callee.get();
    if (!callee || callee->kind != ExprKind::Member) return false;
    const auto* m = static_cast<const MemberExpr*>(callee);
    return m->name.type == TokenType::INIT && m->object->kind == ExprKind::Identifier &&
           static_cast<const IdentifierExpr*>(m->object.get())->name.type == TokenType::SUPER;
}

} // namespace

CCodegen::CCodegen(const Program& program, const Analyser& analyser) : program(program), analyser(analyser) {}

// ============================================================
// Output
// ============================================================

void CCodegen::line(const std::string& text) {
    if (text.empty()) {
        *out << "\n";
        return;
    }
    *out << std::string(static_cast<size_t>(indentLevel) * 4, ' ') << text << "\n";
}

std::string CCodegen::temp() {
    return "ds_t" + std::to_string(tempCounter++);
}

void CCodegen::unsupported(const std::string& what) const {
    throw Unsupported{what};
}

// A text literal: a static ds_str that is never freed
std::string CCodegen::literal(const std::string& text) {
    auto it = literals.find(text);
    if (it != literals.end()) return "(&" + it->second + ")";
    std::string name = "ds_lit" + std::to_string(literals.size());
    literals[text] = name;
    data << "static ds_str " << name << " = DS_STR_LITERAL(" << cString(text) << ", " << text.size() << ", "
         << utf8Count(text) << ");\n";
    return "(&" + name + ")";
}

// ============================================================
// Names and types
// ============================================================

std::string CCodegen::mangle(const std::string& name) const {
    static const std::unordered_set<std::string> reserved = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
        "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
        "_Bool", "bool", "true", "false", "NULL", "main", "errno", "stdin", "stdout", "stderr", "EOF", "assert",
        "INFINITY", "NAN", "offsetof", "setjmp", "longjmp", "jmp_buf", "va_list", "va_arg", "va_start", "va_end",
        "min", "max", "abs", "exit", "pow", "fmod", "fmodf", "sqrt", "cbrt", "floor", "ceil", "round", "llround",
        "sin", "cos", "tan", "atan2", "log", "log10", "exp", "fabs", "fabsf", "isnan", "isinf", "memcpy", "memset",
        "memmove", "strlen", "malloc", "free", "printf", "snprintf", "fwrite", "this_", "self_", "unix", "linux",
        "_WIN32", "complex", "imaginary", "alignas", "alignof", "noreturn", "static_assert", "thread_local",
    };
    if (reserved.count(name) || name.rfind("ds", 0) == 0 || name.rfind("__", 0) == 0) return name + "_";
    return name;
}

std::string CCodegen::functionName(int module, const std::string& name) const {
    return "dsf_m" + std::to_string(module) + "_" + name;
}

std::string CCodegen::globalName(const Symbol& symbol) const {
    const std::string& name = symbol.originalName.empty() ? symbol.name : symbol.originalName;
    if (symbol.kind == SymbolKind::Function) return functionName(symbol.moduleId, name);
    return "dsg_m" + std::to_string(symbol.moduleId) + "_" + name;
}

std::string CCodegen::structName(const SemanticType& t) const {
    return "dsc_m" + std::to_string(t.moduleId) + "_" + t.name;
}

std::string CCodegen::clsName(const SemanticType& t) const {
    return "dsk_m" + std::to_string(t.moduleId) + "_" + t.name;
}

std::string CCodegen::methodName(const ClassInfo& info, const std::string& method) const {
    return "dsm_m" + std::to_string(info.moduleId) + "_" + info.name + "_" + method;
}

bool CCodegen::isCounted(const SemanticType& t) const {
    switch (t.kind) {
        case SemanticTypeKind::Str:
        case SemanticTypeKind::List:
        case SemanticTypeKind::Set:
        case SemanticTypeKind::Map:
        case SemanticTypeKind::Function:
            return true;
        case SemanticTypeKind::UserDefined:
            return !t.isEnum();
        case SemanticTypeKind::Union: {
            SemanticType base = withoutNil(t);
            return base.kind != SemanticTypeKind::Union && isCounted(base);
        }
        default:
            return false;
    }
}

bool CCodegen::isOptValue(const SemanticType& t) const {
    if (t.kind != SemanticTypeKind::Union || !t.containsNil()) return false;
    SemanticType base = withoutNil(t);
    return base.kind != SemanticTypeKind::Union && !isCounted(base);
}

std::string CCodegen::optType(const SemanticType& base) const {
    switch (base.kind) {
        case SemanticTypeKind::Int: return "ds_opt_int";
        case SemanticTypeKind::Dbl: return "ds_opt_dbl";
        case SemanticTypeKind::Flt: return "ds_opt_flt";
        case SemanticTypeKind::Bul: return "ds_opt_bul";
        case SemanticTypeKind::Chr: return "ds_opt_chr";
        case SemanticTypeKind::UserDefined:
            if (base.isEnum()) return "ds_opt_int";
            break;
        default: break;
    }
    unsupported("the type " + base.toString() + " | Nil");
}

std::string CCodegen::cType(const SemanticType& t) const {
    switch (t.kind) {
        case SemanticTypeKind::Int: return "ds_int";
        case SemanticTypeKind::Dbl: return "ds_dbl";
        case SemanticTypeKind::Flt: return "ds_flt";
        case SemanticTypeKind::Bul: return "ds_bul";
        case SemanticTypeKind::Chr: return "ds_chr";
        case SemanticTypeKind::Emp: return "void";
        case SemanticTypeKind::Nil: return "void*";   // always NULL
        case SemanticTypeKind::Str: return "ds_str*";
        case SemanticTypeKind::List: return "ds_list*";
        case SemanticTypeKind::Map: return "ds_map*";
        case SemanticTypeKind::Function: return "ds_fn*";
        case SemanticTypeKind::UserDefined:
            if (t.isEnum()) return "ds_int";
            if (t.isInterface()) return "ds_head*";
            return structName(t) + "*";
        case SemanticTypeKind::Union: {
            SemanticType base = withoutNil(t);
            if (!t.containsNil() || base.kind == SemanticTypeKind::Union) unsupported("union types (" + t.toString() + ")");
            if (isCounted(base)) return cType(base);
            return optType(base);
        }
        case SemanticTypeKind::Obj: unsupported("Obj");
        case SemanticTypeKind::Set: return "ds_list*";
        default: unsupported("the type " + t.toString());
    }
}

std::string CCodegen::elem(const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::Int:
            if (t.radix && *t.radix != 10) {
                std::string name = "ds_elem_r" + std::to_string(*t.radix);
                if (!elemNames.count(name)) {
                    elemNames[name] = name;
                    data << "static const ds_elem " << name << " = {DS_E_RADIX, sizeof(ds_int), NULL, " << *t.radix << ", NULL};\n";
                }
                return "&" + name;
            }
            return "&ds_elem_int";
        case SemanticTypeKind::Dbl: return "&ds_elem_dbl";
        case SemanticTypeKind::Flt: return "&ds_elem_flt";
        case SemanticTypeKind::Bul: return "&ds_elem_bul";
        case SemanticTypeKind::Chr: return "&ds_elem_chr";
        case SemanticTypeKind::Str: return "&ds_elem_str";
        case SemanticTypeKind::UserDefined:
            if (t.isEnum()) {
                std::string name = "ds_elem_m" + std::to_string(t.moduleId) + "_" + t.name;
                if (!elemNames.count(name)) {
                    elemNames[name] = name;
                    data << "static const ds_elem " << name << " = {DS_E_ENUM, sizeof(ds_int), dse_m" << t.moduleId << "_" << t.name
                         << ", 10, NULL};\n";
                }
                return "&" + name;
            }
            return "&ds_elem_ref";
        default:
            if (isCounted(t)) {
                if (withoutNil(t).is(SemanticTypeKind::Str)) return "&ds_elem_str";
                return "&ds_elem_ref";
            }
            unsupported("a list or map of " + t.toString());
    }
}

// A shared empty list (never freed; changing it makes a copy)
std::string CCodegen::emptyList(const SemanticType& element) {
    std::string e = elem(element);
    auto it = emptyLists.find(e);
    if (it != emptyLists.end()) return "(&" + it->second + ")";
    std::string name = "ds_empty" + std::to_string(emptyLists.size());
    emptyLists[e] = name;
    data << "static ds_list " << name << " = DS_LIST_EMPTY(" << e << ");\n";
    return "(&" + name + ")";
}

// What a variable or field holds before anything is assigned (owned)
std::string CCodegen::defaultValue(const SemanticType& t) {
    if (t.containsNil()) return isOptValue(t) ? "(" + cType(t) + "){0}" : "NULL";
    switch (t.kind) {
        case SemanticTypeKind::Str: return "(ds_str*)ds_retain(&ds_str_empty)";
        case SemanticTypeKind::List: return "(ds_list*)ds_retain(" + emptyList(t.elementType ? *t.elementType : kUnknown) + ")";
        case SemanticTypeKind::Map:
            return "ds_map_new(" + elem(t.keyType ? *t.keyType : kUnknown) + ", " + elem(t.elementType ? *t.elementType : kUnknown) + ")";
        case SemanticTypeKind::Set: return "ds_set_new(" + elem(t.elementType ? *t.elementType : kUnknown) + ")";
        case SemanticTypeKind::Int:
        case SemanticTypeKind::Chr: return "0";
        case SemanticTypeKind::Dbl: return "0.0";
        case SemanticTypeKind::Flt: return "0.0f";
        case SemanticTypeKind::Bul: return "false";
        case SemanticTypeKind::UserDefined:
            if (t.isEnum()) {
                // the enum's first value
                if (const EnumInfo* info = analyser.findEnum(t); info && info->decl && !info->decl->values.empty()) {
                    return "dsv_m" + std::to_string(t.moduleId) + "_" + t.name + "_" + info->decl->values.front().name;
                }
                return "0";
            }
            return "NULL";
        default:
            return "NULL";
    }
}

const ClassInfo* CCodegen::classOf(const SemanticType& t) const {
    SemanticType base = withoutNil(t);
    if (!base.isClass()) return nullptr;
    return analyser.findClass(base);
}

const InterfaceInfo* CCodegen::interfaceOf(const SemanticType& t) const {
    SemanticType base = withoutNil(t);
    if (!base.isInterface()) return nullptr;
    return analyser.findInterface(base);
}

std::vector<const ClassInfo*> CCodegen::chain(const ClassInfo& info) const {
    std::vector<const ClassInfo*> out;
    for (const ClassInfo* k = &info; k; k = k->parent ? analyser.findClass(*k->parent) : nullptr) out.push_back(k);
    return out;
}

// Every field of the class, the parents' first (the struct's layout)
std::vector<std::pair<std::string, SemanticType>> CCodegen::allFields(const ClassInfo& info) const {
    std::vector<std::pair<std::string, SemanticType>> fields;
    auto c = chain(info);
    for (auto it = c.rbegin(); it != c.rend(); ++it) {
        for (const auto& f : (*it)->fieldOrder) fields.push_back({f, (*it)->fields.at(f).type});
    }
    return fields;
}

// Every method name, the parents' first (the method table's layout)
std::vector<std::string> CCodegen::allMethods(const ClassInfo& info) const {
    std::vector<std::string> names;
    auto c = chain(info);
    for (auto it = c.rbegin(); it != c.rend(); ++it) {
        for (const auto& member : (*it)->decl->members) {
            if (!member || member->kind != StmtKind::FuncDecl) continue;
            const auto* method = static_cast<const FuncDeclStmt*>(member.get());
            if (method->isConstructor) continue;
            if (std::find(names.begin(), names.end(), method->name) == names.end()) names.push_back(method->name);
        }
    }
    return names;
}

// The class whose code runs for info.method (info itself or the nearest parent)
const ClassInfo* CCodegen::methodOwner(const ClassInfo& info, const std::string& method) const {
    for (const ClassInfo* k : chain(info)) {
        if (k->methods.count(method)) return k;
    }
    return nullptr;
}

const Symbol* CCodegen::nearestConstructor(const ClassInfo& info) const {
    for (const ClassInfo* k : chain(info)) {
        if (k->constructor) return &*k->constructor;
    }
    return nullptr;
}

// ============================================================
// What the program uses
// ============================================================

void CCodegen::markType(const SemanticType& t, std::vector<const Stmt*>& work) {
    if (t.kind == SemanticTypeKind::UserDefined) {
        auto it = declarations.find(std::to_string(t.moduleId) + ":" + t.name);
        if (it != declarations.end() && !reachable.count(it->second)) {
            reachable.insert(it->second);
            work.push_back(it->second);
        }
    }
    if (t.elementType) markType(*t.elementType, work);
    if (t.keyType) markType(*t.keyType, work);
    if (t.returnType) markType(*t.returnType, work);
    for (const auto& m : t.unionMembers) markType(m, work);
    for (const auto& p : t.parameterTypes) markType(p, work);
}

void CCodegen::markSymbol(const Symbol& symbol, std::vector<const Stmt*>& work) {
    if (!symbol.ownerClass.empty()) {
        auto it = declarations.find(symbol.ownerClass);
        if (it != declarations.end() && !reachable.count(it->second)) {
            reachable.insert(it->second);
            work.push_back(it->second);
        }
    }
    if (symbol.kind == SymbolKind::Builtin || symbol.kind == SymbolKind::Module) return;
    if (!symbol.isGlobal && symbol.kind != SymbolKind::Function && symbol.kind != SymbolKind::Class &&
        symbol.kind != SymbolKind::Enum && symbol.kind != SymbolKind::Interface) {
        return;
    }
    const std::string& name = symbol.originalName.empty() ? symbol.name : symbol.originalName;
    auto it = declarations.find(std::to_string(symbol.moduleId) + ":" + name);
    if (it != declarations.end() && !reachable.count(it->second)) {
        reachable.insert(it->second);
        work.push_back(it->second);
    }
}

void CCodegen::visitExpr(const Expr* e, std::vector<const Stmt*>& work) {
    if (!e) return;
    if (const SemanticType* t = analyser.typeOf(e)) markType(*t, work);
    if (const Symbol* s = analyser.symbolOf(e)) markSymbol(*s, work);
    if (const Symbol* s = analyser.memberOf(e)) markSymbol(*s, work);
    if (const auto* op = analyser.operatorCallOf(e)) markSymbol(op->method, work);
    switch (e->kind) {
        case ExprKind::TemplateString:
            for (const auto& i : static_cast<const TemplateStringExpr*>(e)->interpolations) visitExpr(i.get(), work);
            break;
        case ExprKind::Unary: visitExpr(static_cast<const UnaryExpr*>(e)->operand.get(), work); break;
        case ExprKind::Postfix: visitExpr(static_cast<const PostfixExpr*>(e)->operand.get(), work); break;
        case ExprKind::Binary:
            visitExpr(static_cast<const BinaryExpr*>(e)->left.get(), work);
            visitExpr(static_cast<const BinaryExpr*>(e)->right.get(), work);
            break;
        case ExprKind::Assign:
            visitExpr(static_cast<const AssignExpr*>(e)->target.get(), work);
            visitExpr(static_cast<const AssignExpr*>(e)->value.get(), work);
            break;
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            visitExpr(t->condition.get(), work);
            visitExpr(t->thenBranch.get(), work);
            visitExpr(t->elseBranch.get(), work);
            break;
        }
        case ExprKind::Call: {
            const auto* c = static_cast<const CallExpr*>(e);
            visitExpr(c->callee.get(), work);
            for (const auto& a : c->args) visitExpr(a.value.get(), work);
            // default values of the parameters
            const Symbol* s = analyser.symbolOf(c->callee.get());
            if (!s) s = analyser.memberOf(c->callee.get());
            if (s) for (const auto& p : s->parameters) visitExpr(p.defaultValue, work);
            break;
        }
        case ExprKind::Index:
            visitExpr(static_cast<const IndexExpr*>(e)->object.get(), work);
            visitExpr(static_cast<const IndexExpr*>(e)->index.get(), work);
            break;
        case ExprKind::Member: visitExpr(static_cast<const MemberExpr*>(e)->object.get(), work); break;
        case ExprKind::Lambda: visitStmt(static_cast<const LambdaExpr*>(e)->body.get(), work); break;
        case ExprKind::New: {
            const auto* n = static_cast<const NewExpr*>(e);
            for (const auto& a : n->args) visitExpr(a.value.get(), work);
            if (const ClassInfo* info = classOf(*analyser.typeOf(e))) {
                if (const Symbol* ctor = nearestConstructor(*info)) {
                    for (const auto& p : ctor->parameters) visitExpr(p.defaultValue, work);
                }
            }
            break;
        }
        case ExprKind::List:
            for (const auto& el : static_cast<const ListExpr*>(e)->elements) visitExpr(el.value.get(), work);
            break;
        case ExprKind::Set:
            for (const auto& el : static_cast<const SetExpr*>(e)->elements) visitExpr(el.value.get(), work);
            break;
        case ExprKind::Map:
            for (const auto& entry : static_cast<const MapExpr*>(e)->entries) {
                visitExpr(entry.key.get(), work);
                visitExpr(entry.value.get(), work);
            }
            break;
        case ExprKind::Grouping: visitExpr(static_cast<const GroupingExpr*>(e)->inner.get(), work); break;
        case ExprKind::Is:
            visitExpr(static_cast<const IsExpr*>(e)->value.get(), work);
            if (const SemanticType* t = analyser.isTargetOf(static_cast<const IsExpr*>(e))) markType(*t, work);
            break;
        case ExprKind::Convert: visitExpr(static_cast<const ConvertExpr*>(e)->value.get(), work); break;
        default: break;
    }
}

void CCodegen::visitStmt(const Stmt* s, std::vector<const Stmt*>& work) {
    if (!s) return;
    if (const SemanticType* t = analyser.declaredTypeOf(s)) markType(*t, work);
    switch (s->kind) {
        case StmtKind::Expression: visitExpr(static_cast<const ExpressionStmt*>(s)->expression.get(), work); break;
        case StmtKind::VarDecl: visitExpr(static_cast<const VarDeclStmt*>(s)->initializer.get(), work); break;
        case StmtKind::FuncDecl: {
            const auto* f = static_cast<const FuncDeclStmt*>(s);
            if (const Symbol* sym = analyser.functionOf(f)) {
                markType(sym->returnType, work);
                for (const auto& p : sym->parameters) markType(p.type, work);
            }
            visitStmt(f->body.get(), work);
            break;
        }
        case StmtKind::ClassDecl: {
            const auto* c = static_cast<const ClassDeclStmt*>(s);
            for (const auto& m : c->members) visitStmt(m.get(), work);
            if (const ClassInfo* info = analyser.findClass(SemanticType::userDefined(c->name, moduleId, UserTypeKind::Class))) {
                if (info->parent) markType(*info->parent, work);
                if (info->interface) markType(*info->interface, work);
                for (const auto& [name, f] : info->fields) markType(f.type, work);
            }
            break;
        }
        case StmtKind::InterfaceDecl:
            for (const auto& m : static_cast<const InterfaceDeclStmt*>(s)->members) visitStmt(m.get(), work);
            break;
        case StmtKind::EnumDecl:
            for (const auto& v : static_cast<const EnumDeclStmt*>(s)->values) {
                if (v.value) visitExpr(v.value->get(), work);
            }
            break;
        case StmtKind::If: {
            const auto* i = static_cast<const IfStmt*>(s);
            visitExpr(i->condition.get(), work);
            visitStmt(i->thenBranch.get(), work);
            visitStmt(i->elseBranch.get(), work);
            break;
        }
        case StmtKind::Switch: {
            const auto* sw = static_cast<const SwitchStmt*>(s);
            visitExpr(sw->discriminant.get(), work);
            for (const auto& c : sw->cases) {
                visitExpr(c.value.get(), work);
                visitStmt(c.body.get(), work);
            }
            break;
        }
        case StmtKind::For: {
            const auto* f = static_cast<const ForStmt*>(s);
            visitStmt(f->init.get(), work);
            visitExpr(f->condition.get(), work);
            visitExpr(f->increment.get(), work);
            visitStmt(f->body.get(), work);
            break;
        }
        case StmtKind::ForIn: {
            const auto* f = static_cast<const ForInStmt*>(s);
            visitStmt(f->declaration.get(), work);
            visitExpr(f->iterable.get(), work);
            visitStmt(f->body.get(), work);
            break;
        }
        case StmtKind::While:
            visitExpr(static_cast<const WhileStmt*>(s)->condition.get(), work);
            visitStmt(static_cast<const WhileStmt*>(s)->body.get(), work);
            break;
        case StmtKind::Loop: visitStmt(static_cast<const LoopStmt*>(s)->body.get(), work); break;
        case StmtKind::Return: visitExpr(static_cast<const ReturnStmt*>(s)->value.get(), work); break;
        case StmtKind::Block:
            for (const auto& b : static_cast<const BlockStmt*>(s)->statements) visitStmt(b.get(), work);
            break;
        case StmtKind::Try: {
            const auto* t = static_cast<const TryStmt*>(s);
            visitStmt(t->tryBlock.get(), work);
            for (const auto& c : t->catches) {
                if (const SemanticType* ct = analyser.catchTypeOf(&c)) markType(*ct, work);
                visitStmt(c.body.get(), work);
            }
            break;
        }
        case StmtKind::Throw: visitExpr(static_cast<const ThrowStmt*>(s)->value.get(), work); break;
        default: break;
    }
}

// Only what the entry file's code can reach is translated, so a library's
// unused parts never stop a program from being built as C.
void CCodegen::findReachable() {
    std::unordered_map<const Stmt*, int> moduleOf;
    for (const auto& module : program.modules) {
        for (const auto& s : module->ast) {
            if (!s) continue;
            std::string name;
            if (s->kind == StmtKind::FuncDecl) name = static_cast<const FuncDeclStmt*>(s.get())->name;
            else if (s->kind == StmtKind::ClassDecl) name = static_cast<const ClassDeclStmt*>(s.get())->name;
            else if (s->kind == StmtKind::EnumDecl) name = static_cast<const EnumDeclStmt*>(s.get())->name;
            else if (s->kind == StmtKind::InterfaceDecl) name = static_cast<const InterfaceDeclStmt*>(s.get())->name;
            else if (s->kind == StmtKind::VarDecl && static_cast<const VarDeclStmt*>(s.get())->name) {
                name = *static_cast<const VarDeclStmt*>(s.get())->name;
            }
            if (name.empty()) continue;
            declarations[std::to_string(module->id) + ":" + name] = s.get();
            moduleOf[s.get()] = module->id;
        }
    }
    std::vector<const Stmt*> work;
    const Module& entry = program.entry();
    moduleId = entry.id;
    for (const auto& s : entry.ast) {
        if (!s) continue;
        switch (s->kind) {
            case StmtKind::FuncDecl:
            case StmtKind::ClassDecl:
            case StmtKind::InterfaceDecl:
            case StmtKind::EnumDecl:
            case StmtKind::Import:
                break;
            default:
                if (s->kind == StmtKind::VarDecl) reachable.insert(s.get());
                visitStmt(s.get(), work);
        }
    }
    // The runtime turns its errors into Error objects with the prelude's _error
    for (const auto& module : program.modules) {
        if (!module->isPrelude) continue;
        auto d = declarations.find(std::to_string(module->id) + ":_error");
        if (d != declarations.end() && !reachable.count(d->second)) {
            reachable.insert(d->second);
            work.push_back(d->second);
        }
    }
    while (!work.empty()) {
        const Stmt* s = work.back();
        work.pop_back();
        auto it = moduleOf.find(s);
        moduleId = it == moduleOf.end() ? moduleId : it->second;
        visitStmt(s, work);
        // A class: its subclasses' overrides may run in its place, so
        // nothing more is needed; but its parent's methods are its own too.
    }
    // Classes: every reachable class's parents
    bool more = true;
    while (more) {
        more = false;
        for (const auto& key : analyser.classOrder()) {
            auto d = declarations.find(key);
            if (d == declarations.end() || !reachable.count(d->second)) continue;
            auto colon = key.find(':');
            const ClassInfo* info = analyser.findClass(
                SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class));
            if (info && info->parent) {
                auto p = declarations.find(info->parent->key());
                if (p != declarations.end() && !reachable.count(p->second)) {
                    reachable.insert(p->second);
                    moduleId = info->parent->moduleId;
                    visitStmt(p->second, work);
                    while (!work.empty()) {
                        const Stmt* w = work.back();
                        work.pop_back();
                        auto m = moduleOf.find(w);
                        moduleId = m == moduleOf.end() ? moduleId : m->second;
                        visitStmt(w, work);
                    }
                    more = true;
                }
            }
        }
    }
}

// ============================================================
// Program
// ============================================================

std::string CCodegen::generate(std::string& reason) {
    try {
        for (const auto& module : program.modules) {
            if (module->jsNative.empty()) continue;
            if (!wasm) unsupported("a browser library");
            usesWeb = true;
        }
        if (usesWeb) collectExtern();
        findReachable();

        // Interfaces get numbers (their place in each class's table)
        for (const auto& key : analyser.interfaceOrder()) {
            auto d = declarations.find(key);
            if (d != declarations.end() && reachable.count(d->second)) {
                int id = static_cast<int>(interfaceIds.size());
                interfaceIds[key] = id;
            }
        }
        // Methods overridden somewhere below the class that has them
        for (const auto& key : analyser.classOrder()) {
            auto d = declarations.find(key);
            if (d == declarations.end() || !reachable.count(d->second)) continue;
            auto colon = key.find(':');
            const ClassInfo* info = analyser.findClass(
                SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class));
            if (!info) continue;
            auto c = chain(*info);
            for (size_t i = 1; i < c.size(); ++i) {
                for (const auto& [name, symbol] : info->methods) {
                    for (size_t k = i; k < c.size(); ++k) overridden.insert(c[k]->type().key() + "." + name);
                }
            }
        }

        emitTypes();

        // Globals
        for (const auto& module : program.modules) {
            protos << "static const char ds_file_m" << module->id << "[] = " << cString(module->displayPath) << ";\n";
            for (const auto& s : module->ast) {
                if (!s || s->kind != StmtKind::VarDecl || !isReachable(s.get())) continue;
                const auto* decl = static_cast<const VarDeclStmt*>(s.get());
                const SemanticType* type = analyser.declaredTypeOf(decl);
                if (!decl->name || !type) continue;
                Symbol g;
                g.kind = SymbolKind::Variable;
                g.name = *decl->name;
                g.moduleId = module->id;
                protos << "static " << cType(*type) << " " << globalName(g) << ";\n";
            }
        }

        // Functions and classes
        for (const auto& module : program.modules) {
            moduleId = module->id;
            for (const auto& s : module->ast) {
                if (!s || !isReachable(s.get())) continue;
                if (s->kind == StmtKind::FuncDecl) {
                    const auto* f = static_cast<const FuncDeclStmt*>(s.get());
                    if (isStubFunction(module->id, f->name)) continue;   // the page bridge does it
                    if (const Symbol* sym = analyser.functionOf(f)) {
                        protos << signature(*sym, functionName(module->id, f->name), false) << ";\n";
                    }
                    emitFunction(f, functionName(module->id, f->name), nullptr);
                }
            }
        }
        for (const auto& key : analyser.classOrder()) {
            auto d = declarations.find(key);
            if (d == declarations.end() || !reachable.count(d->second)) continue;
            auto colon = key.find(':');
            const ClassInfo* info = analyser.findClass(
                SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class));
            if (info && !externClasses.count(key)) emitClassCode(*info);
        }

        emitMain();
    } catch (const Unsupported& u) {
        reason = u.what;
        return "";
    }

    std::ostringstream all;
    all << "/* Generated by the Dessa compiler (C). Do not edit. */\n";
    // Libraries with a C++ part this program calls (the toolchain links their bridges)
    all << "/* dessa-glue:";
    for (const auto& lib : glueLibraries) all << " " << lib;
    all << " */\n";
    all << (usesWeb ? "#include \"dessa_web.h\"\n\n" : "#include \"dessa_rt.h\"\n\n");
    all << typedefs.str() << "\n" << structs.str() << "\n" << protos.str() << "\n" << data.str() << "\n" << code.str();
    return all.str();
}

void CCodegen::emitTypes() {
    // Struct names first: classes may point at each other
    for (const auto& key : analyser.classOrder()) {
        auto d = declarations.find(key);
        if (d == declarations.end() || !reachable.count(d->second)) continue;
        auto colon = key.find(':');
        SemanticType t = SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class);
        // a page object (WebAssembly) is the bridge's ds_js
        typedefs << "typedef struct " << (externClasses.count(key) ? std::string("ds_js") : structName(t)) << " " << structName(t) << ";\n";
    }
    // Enums
    for (const auto& module : program.modules) {
        for (const auto& s : module->ast) {
            if (s && s->kind == StmtKind::EnumDecl && isReachable(s.get())) emitEnum(static_cast<const EnumDeclStmt*>(s.get()), module->id);
        }
    }
    // Interfaces' method tables
    for (const auto& [key, id] : interfaceIds) {
        auto colon = key.find(':');
        SemanticType t = SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Interface);
        if (const InterfaceInfo* info = analyser.findInterface(t)) emitInterface(*info);
    }
    // Classes, parents first
    for (const auto& key : analyser.classOrder()) {
        auto d = declarations.find(key);
        if (d == declarations.end() || !reachable.count(d->second)) continue;
        auto colon = key.find(':');
        const ClassInfo* info = analyser.findClass(
            SemanticType::userDefined(key.substr(colon + 1), std::stoi(key.substr(0, colon)), UserTypeKind::Class));
        if (info && externClasses.count(key)) emitExternClass(*info);
        else if (info) emitClassStruct(*info);
    }
}

void CCodegen::emitEnum(const EnumDeclStmt* decl, int module) {
    const std::string prefix = "dsv_m" + std::to_string(module) + "_" + decl->name + "_";
    long long next = 0;
    std::vector<std::pair<std::string, long long>> values;
    for (const auto& v : decl->values) {
        if (v.value) {
            const Expr* e = v.value->get();
            bool negative = false;
            if (e->kind == ExprKind::Unary && static_cast<const UnaryExpr*>(e)->op.type == TokenType::MINUS) {
                negative = true;
                e = static_cast<const UnaryExpr*>(e)->operand.get();
            }
            if (e->kind != ExprKind::Literal) unsupported("an enum value that isn't a number");
            const Token& t = static_cast<const LiteralExpr*>(e)->token;
            long long n = 0;
            try {
                if (t.type == TokenType::INT_LITERAL) n = std::stoll(t.lexeme);
                else if (t.type == TokenType::HEX_LITERAL) n = std::stoll(t.lexeme.substr(2), nullptr, 16);
                else if (t.type == TokenType::BINARY_LITERAL) n = std::stoll(t.lexeme.substr(2), nullptr, 2);
                else if (t.type == TokenType::OCTAL_LITERAL) n = std::stoll(t.lexeme.substr(2), nullptr, 8);
                else unsupported("an enum value that isn't a whole number");
            } catch (const std::exception&) {
                unsupported("an enum value");
            }
            next = negative ? -n : n;
        }
        values.push_back({v.name, next});
        typedefs << "#define " << prefix << v.name << " ((ds_int)" << next << "LL)\n";
        ++next;
    }
    const std::string fn = "dse_m" + std::to_string(module) + "_" + decl->name;
    protos << "static ds_str* " << fn << "(ds_int value);\n";
    code << "static ds_str* " << fn << "(ds_int value) {\n";
    for (const auto& [name, value] : values) {
        code << "    if (value == " << value << "LL) return (ds_str*)ds_retain(" << literal(name) << ");\n";
    }
    code << "    ds_sb b;\n    ds_sb_init(&b);\n    ds_sb_cstr(&b, " << cString(decl->name + "(") << ");\n"
         << "    ds_sb_int(&b, value);\n    ds_sb_cstr(&b, \")\");\n    return ds_sb_done(&b);\n}\n\n";
}

std::string CCodegen::signature(const Symbol& symbol, const std::string& name, bool method) const {
    std::string s = "static " + cType(symbol.returnType) + " " + name + "(";
    bool first = true;
    if (method) {
        s += "void* self_";
        first = false;
    }
    for (const auto& p : symbol.parameters) {
        s += (first ? "" : ", ") + cType(p.type) + " " + mangle(p.name);
        first = false;
    }
    if (first) s += "void";
    return s + ")";
}

void CCodegen::emitInterface(const InterfaceInfo& info) {
    const std::string name = "dst_m" + std::to_string(info.moduleId) + "_" + info.name;
    structs << "typedef struct " << name << " {\n";
    for (const auto& method : info.methodOrder) {
        const Symbol& m = info.methods.at(method);
        structs << "    " << cType(m.returnType) << " (*m_" << method << ")(void* self_";
        for (const auto& p : m.parameters) structs << ", " << cType(p.type);
        structs << ");\n";
    }
    for (const auto& field : info.fieldOrder) {
        structs << "    " << cType(info.fields.at(field)) << "* (*get_" << field << ")(void* self_);\n";
    }
    if (info.methodOrder.empty() && info.fieldOrder.empty()) structs << "    int unused;\n";
    structs << "} " << name << ";\n\n";
}

void CCodegen::emitClassStruct(const ClassInfo& info) {
    const SemanticType t = info.type();
    const std::string s = structName(t);
    structs << "struct " << s << " {\n    ds_head h;\n";
    for (const auto& [name, type] : allFields(info)) structs << "    " << cType(type) << " " << mangle(name) << ";\n";
    structs << "};\n";
    // The method table: every method, the parents' first, so a class's
    // table starts like its parent's
    structs << "typedef struct " << s << "_cls {\n    ds_type t;\n";
    for (const auto& method : allMethods(info)) {
        // the signature where the method first appears
        const ClassInfo* top = nullptr;
        for (const ClassInfo* k : chain(info)) if (k->methods.count(method)) top = k;
        const Symbol& m = top->methods.at(method);
        structs << "    " << cType(m.returnType) << " (*m_" << method << ")(void* self_";
        for (const auto& p : m.parameters) structs << ", " << cType(p.type);
        structs << ");\n";
    }
    structs << "} " << s << "_cls;\n";
    protos << "extern const " << s << "_cls " << clsName(t) << ";\n";
    // prototypes of its code
    const std::string base = "m" + std::to_string(info.moduleId) + "_" + info.name;
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::FuncDecl) continue;
        const auto* method = static_cast<const FuncDeclStmt*>(member.get());
        if (method->isConstructor) continue;
        if (const Symbol* sym = analyser.functionOf(method)) protos << signature(*sym, methodName(info, method->name), true) << ";\n";
    }
    std::string params;
    if (const Symbol* ctor = nearestConstructor(info)) {
        for (const auto& p : ctor->parameters) params += ", " + cType(p.type) + " " + mangle(p.name);
    }
    protos << "static " << s << "* dsn_" << base << "(" << (params.empty() ? "void" : params.substr(2)) << ");\n";
    protos << "static void dsi_" << base << "(void* self_" << params << ");\n";
    protos << "static void dsd_" << base << "(ds_head* h);\n";
    protos << "static ds_str* dss_" << base << "(ds_head* h);\n";
}

void CCodegen::emitClassCode(const ClassInfo& info) {
    const ClassInfo* saved = currentClass;
    currentClass = &info;
    moduleId = info.moduleId;
    const SemanticType t = info.type();
    const std::string s = structName(t);
    const std::string base = "m" + std::to_string(info.moduleId) + "_" + info.name;

    // Methods
    const FuncDeclStmt* ctor = nullptr;
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::FuncDecl) continue;
        const auto* method = static_cast<const FuncDeclStmt*>(member.get());
        if (method->isConstructor) ctor = method;
        else emitFunction(method, methodName(info, method->name), &info);
    }
    emitConstructor(info, ctor);

    // Freeing: release every counted field
    code << "static void dsd_" << base << "(ds_head* h) {\n    " << s << "* o = (" << s << "*)h;\n";
    for (const auto& [name, type] : allFields(info)) {
        if (isCounted(type)) code << "    ds_release(o->" << mangle(name) << ");\n";
    }
    code << "    ds_object_free(o, sizeof(" << s << "));\n}\n\n";

    // How print() shows it: toStr() if the class has one, else Name(field: value, ...)
    code << "static ds_str* dss_" << base << "(ds_head* h) {\n";
    const ClassInfo* withToStr = nullptr;
    for (const ClassInfo* k : chain(info)) {
        auto it = k->methods.find("toStr");
        if (it != k->methods.end() && it->second.parameters.empty() && it->second.returnType.is(SemanticTypeKind::Str)) {
            withToStr = k;
            break;
        }
    }
    if (withToStr) {
        code << "    return " << methodName(*withToStr, "toStr") << "(h);\n}\n\n";
    } else {
        code << "    " << s << "* o = (" << s << "*)h;\n    ds_sb b;\n    ds_sb_init(&b);\n";
        code << "    ds_sb_cstr(&b, " << cString(info.type().toString() + "(") << ");\n";
        bool first = true;
        for (const auto& [name, type] : allFields(info)) {
            code << "    ds_sb_cstr(&b, " << cString((first ? "" : ", ") + name + ": ") << ");\n";
            first = false;
            std::ostringstream* savedOut = out;
            std::ostringstream ignore;
            out = &ignore;
            beginTemps();
            CV value{"o->" + mangle(name), false};
            std::string text;
            if (withoutNil(type).is(SemanticTypeKind::Str)) {
                text = isOptValue(type) ? "" : "(o->" + mangle(name) + " ? ds_quote(o->" + mangle(name) + ", '\"') : ds_str_cstr(\"Nil\"))";
            } else if (type.is(SemanticTypeKind::Chr)) {
                text = "ds_chr_repr(o->" + mangle(name) + ")";
            } else if (isOptValue(type) && withoutNil(type).is(SemanticTypeKind::Chr)) {
                text = "(o->" + mangle(name) + ".has ? ds_chr_repr(o->" + mangle(name) + ".value) : ds_str_cstr(\"Nil\"))";
            } else {
                text = toText(value, type);
            }
            if (!endTemps().empty()) unsupported("printing this class");
            out = savedOut;
            code << "    ds_sb_take(&b, " << text << ");\n";
        }
        code << "    ds_sb_cstr(&b, \")\");\n    return ds_sb_done(&b);\n}\n\n";
    }

    // Interfaces: the methods and field getters each one needs
    std::vector<std::pair<int, std::string>> tables;
    std::set<std::string> done;
    for (const ClassInfo* k : chain(info)) {
        if (!k->interface) continue;
        const std::string key = k->interface->key();
        if (done.count(key) || !interfaceIds.count(key)) continue;
        done.insert(key);
        const InterfaceInfo* iface = analyser.findInterface(*k->interface);
        if (!iface) continue;
        const std::string itType = "dst_m" + std::to_string(iface->moduleId) + "_" + iface->name;
        const std::string table = "dsit_" + base + "_" + std::to_string(interfaceIds[key]);
        for (const auto& field : iface->fieldOrder) {
            code << "static " << cType(iface->fields.at(field)) << "* dsgf_" << base << "_" << field << "(void* self_) { return &(("
                 << s << "*)self_)->" << mangle(field) << "; }\n";
        }
        code << "static const " << itType << " " << table << " = {";
        bool first = true;
        for (const auto& method : iface->methodOrder) {
            const ClassInfo* owner = methodOwner(info, method);
            if (!owner) unsupported("an interface method without code");
            const Symbol& m = iface->methods.at(method);
            std::string slot = cType(m.returnType) + " (*)(void*";
            for (const auto& p : m.parameters) slot += ", " + cType(p.type);
            slot += ")";
            code << (first ? "" : ", ") << "(" << slot << ")" << methodName(*owner, method);
            first = false;
        }
        for (const auto& field : iface->fieldOrder) {
            code << (first ? "" : ", ") << "dsgf_" << base << "_" << field;
            first = false;
        }
        if (first) code << "0";
        code << "};\n";
        tables.push_back({interfaceIds[key], table});
    }
    std::string ifaces = "NULL";
    int count = 0;
    if (!tables.empty()) {
        for (const auto& [id, table] : tables) count = std::max(count, id + 1);
        code << "static const void* const dsif_" << base << "[" << count << "] = {";
        for (size_t i = 0; i < tables.size(); ++i) {
            code << (i ? ", " : "") << "[" << tables[i].first << "] = &" << tables[i].second;
        }
        code << "};\n";
        ifaces = "dsif_" + base;
    }

    // The class description: its type, then the method table
    code << "const " << s << "_cls " << clsName(t) << " = {\n";
    code << "    {" << cString(info.type().toString()) << ", DS_K_OBJECT, dsd_" << base << ", dss_" << base << ", "
         << (info.parent ? "&" + clsName(*info.parent) + ".t" : std::string("NULL")) << ", " << ifaces << ", " << count << "}";
    for (const auto& method : allMethods(info)) {
        const ClassInfo* owner = methodOwner(info, method);
        const ClassInfo* top = nullptr;
        for (const ClassInfo* k : chain(info)) if (k->methods.count(method)) top = k;
        const Symbol& m = top->methods.at(method);
        std::string slot = cType(m.returnType) + " (*)(void*";
        for (const auto& p : m.parameters) slot += ", " + cType(p.type);
        slot += ")";
        code << ",\n    (" << slot << ")" << methodName(*owner, method);
    }
    code << "\n};\n\n";
    currentClass = saved;
}

// ============================================================
// Functions
// ============================================================

bool CCodegen::hasTry(const Stmt* s) const {
    if (!s) return false;
    switch (s->kind) {
        case StmtKind::Try: return true;
        case StmtKind::Block:
            for (const auto& b : static_cast<const BlockStmt*>(s)->statements) if (hasTry(b.get())) return true;
            return false;
        case StmtKind::If:
            return hasTry(static_cast<const IfStmt*>(s)->thenBranch.get()) || hasTry(static_cast<const IfStmt*>(s)->elseBranch.get());
        case StmtKind::For: return hasTry(static_cast<const ForStmt*>(s)->body.get());
        case StmtKind::ForIn: return hasTry(static_cast<const ForInStmt*>(s)->body.get());
        case StmtKind::While: return hasTry(static_cast<const WhileStmt*>(s)->body.get());
        case StmtKind::Loop: return hasTry(static_cast<const LoopStmt*>(s)->body.get());
        case StmtKind::Switch:
            for (const auto& c : static_cast<const SwitchStmt*>(s)->cases) if (hasTry(c.body.get())) return true;
            return false;
        default: return false;
    }
}

bool CCodegen::assignsNameExpr(const Expr* e, const std::string& name) const {
    if (!e) return false;
    auto isName = [&](const Expr* t) {
        return t && t->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(t)->name.lexeme == name;
    };
    switch (e->kind) {
        case ExprKind::Assign: {
            const auto* a = static_cast<const AssignExpr*>(e);
            const Expr* target = a->target.get();
            // x = ..., x<i> = ..., x.f = ... (a list or map parameter changed in place)
            while (target && (target->kind == ExprKind::Index || target->kind == ExprKind::Member)) {
                target = target->kind == ExprKind::Index ? static_cast<const IndexExpr*>(target)->object.get()
                                                         : static_cast<const MemberExpr*>(target)->object.get();
            }
            return isName(target) || assignsNameExpr(a->value.get(), name);
        }
        case ExprKind::Postfix: return isName(static_cast<const PostfixExpr*>(e)->operand.get());
        case ExprKind::Unary: return assignsNameExpr(static_cast<const UnaryExpr*>(e)->operand.get(), name);
        case ExprKind::Binary:
            return assignsNameExpr(static_cast<const BinaryExpr*>(e)->left.get(), name) ||
                   assignsNameExpr(static_cast<const BinaryExpr*>(e)->right.get(), name);
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            return assignsNameExpr(t->condition.get(), name) || assignsNameExpr(t->thenBranch.get(), name) ||
                   assignsNameExpr(t->elseBranch.get(), name);
        }
        case ExprKind::Call: {
            const auto* c = static_cast<const CallExpr*>(e);
            // list.push(x) changes the list in place
            if (c->callee->kind == ExprKind::Member) {
                const Expr* object = static_cast<const MemberExpr*>(c->callee.get())->object.get();
                while (object && (object->kind == ExprKind::Index || object->kind == ExprKind::Member)) {
                    object = object->kind == ExprKind::Index ? static_cast<const IndexExpr*>(object)->object.get()
                                                             : static_cast<const MemberExpr*>(object)->object.get();
                }
                if (isName(object)) return true;
            }
            if (assignsNameExpr(c->callee.get(), name)) return true;
            for (const auto& a : c->args) if (assignsNameExpr(a.value.get(), name)) return true;
            return false;
        }
        case ExprKind::Index:
            return assignsNameExpr(static_cast<const IndexExpr*>(e)->object.get(), name) ||
                   assignsNameExpr(static_cast<const IndexExpr*>(e)->index.get(), name);
        case ExprKind::Member: return assignsNameExpr(static_cast<const MemberExpr*>(e)->object.get(), name);
        case ExprKind::Grouping: return assignsNameExpr(static_cast<const GroupingExpr*>(e)->inner.get(), name);
        case ExprKind::Lambda: return false;   // a lambda can't change it
        default: return false;
    }
}

bool CCodegen::assignsName(const Stmt* s, const std::string& name) const {
    if (!s) return false;
    switch (s->kind) {
        case StmtKind::Expression: return assignsNameExpr(static_cast<const ExpressionStmt*>(s)->expression.get(), name);
        case StmtKind::VarDecl: return assignsNameExpr(static_cast<const VarDeclStmt*>(s)->initializer.get(), name);
        case StmtKind::Block:
            for (const auto& b : static_cast<const BlockStmt*>(s)->statements) if (assignsName(b.get(), name)) return true;
            return false;
        case StmtKind::If: {
            const auto* i = static_cast<const IfStmt*>(s);
            return assignsNameExpr(i->condition.get(), name) || assignsName(i->thenBranch.get(), name) ||
                   assignsName(i->elseBranch.get(), name);
        }
        case StmtKind::For: {
            const auto* f = static_cast<const ForStmt*>(s);
            return assignsName(f->init.get(), name) || assignsNameExpr(f->condition.get(), name) ||
                   assignsNameExpr(f->increment.get(), name) || assignsName(f->body.get(), name);
        }
        case StmtKind::ForIn:
            return assignsNameExpr(static_cast<const ForInStmt*>(s)->iterable.get(), name) ||
                   assignsName(static_cast<const ForInStmt*>(s)->body.get(), name);
        case StmtKind::While:
            return assignsNameExpr(static_cast<const WhileStmt*>(s)->condition.get(), name) ||
                   assignsName(static_cast<const WhileStmt*>(s)->body.get(), name);
        case StmtKind::Loop: return assignsName(static_cast<const LoopStmt*>(s)->body.get(), name);
        case StmtKind::Switch:
            for (const auto& c : static_cast<const SwitchStmt*>(s)->cases) if (assignsName(c.body.get(), name)) return true;
            return assignsNameExpr(static_cast<const SwitchStmt*>(s)->discriminant.get(), name);
        case StmtKind::Return: return assignsNameExpr(static_cast<const ReturnStmt*>(s)->value.get(), name);
        case StmtKind::Throw: return assignsNameExpr(static_cast<const ThrowStmt*>(s)->value.get(), name);
        case StmtKind::Try: {
            const auto* t = static_cast<const TryStmt*>(s);
            if (assignsName(t->tryBlock.get(), name)) return true;
            for (const auto& c : t->catches) if (assignsName(c.body.get(), name)) return true;
            return false;
        }
        default: return false;
    }
}

void CCodegen::beginFunction(bool withTry) {
    volatileLocals = withTry;
    scopes.clear();
    Scope f;
    f.function = true;
    scopes.push_back(f);
    indentLevel = 1;
}

void CCodegen::endFunction() {
    scopes.clear();
    volatileLocals = false;
    indentLevel = 0;
}

// Parameters: borrowed from the caller, unless the function changes them
// (then it keeps its own count); in a function with try, copied to volatile
// locals (setjmp would lose them otherwise).
void CCodegen::emitFunction(const FuncDeclStmt* decl, const std::string& name, const ClassInfo* owner) {
    const Symbol* symbol = analyser.functionOf(decl);
    if (!symbol || !decl->body) return;
    std::ostringstream body;
    out = &body;
    returnType = symbol->returnType;
    const bool withTry = hasTry(decl->body.get());
    beginFunction(withTry);

    std::string sig = "static " + cType(symbol->returnType) + " " + name + "(";
    bool first = true;
    if (owner) {
        sig += "void* self_";
        first = false;
    }
    std::vector<std::pair<const FunctionParameterSymbol*, bool>> params;
    for (const auto& p : symbol->parameters) {
        const bool changed = assignsName(decl->body.get(), p.name);
        const bool copy = withTry || changed;
        sig += (first ? "" : ", ") + cType(p.type) + " " + mangle(p.name) + (copy ? "_in" : "");
        first = false;
        params.push_back({&p, copy});
    }
    if (first) sig += "void";
    indentLevel = 0;
    line(sig + ") {");
    indentLevel = 1;
    if (owner) {
        line(structName(owner->type()) + "* this_ = (" + structName(owner->type()) + "*)self_;");
        scopes.back().locals.push_back({"this", "this_", owner->type(), false});
    }
    for (const auto& [p, copy] : params) {
        if (copy) {
            const bool counted = isCounted(p->type);
            std::string cname = declareLocal(p->name, p->type, counted);
            std::string value = mangle(p->name) + "_in";
            if (counted) value = "(" + cType(p->type) + ")ds_retain(" + value + ")";
            line(cType(p->type) + (volatileLocals ? " volatile " : " ") + cname + " = " + value + ";");
        } else {
            scopes.back().locals.push_back({p->name, mangle(p->name), p->type, false});
        }
    }
    if (owner) line("(void)this_;");
    for (const auto& s : static_cast<const BlockStmt*>(decl->body.get())->statements) stmt(s.get());
    popScope(true);
    indentLevel = 0;
    line("}");
    line("");
    endFunction();
    code << body.str();
    out = nullptr;
    returnType.reset();
}

// new C(args): allocate, then dsi_ runs the parents' init, the field
// initialisers and init's body
void CCodegen::emitConstructor(const ClassInfo& info, const FuncDeclStmt* ctor) {
    const SemanticType t = info.type();
    const std::string s = structName(t);
    const std::string base = "m" + std::to_string(info.moduleId) + "_" + info.name;
    const Symbol* symbol = ctor ? analyser.functionOf(ctor) : nearestConstructor(info);
    std::string params, forward;
    if (symbol) {
        for (const auto& p : symbol->parameters) {
            params += ", " + cType(p.type) + " " + mangle(p.name);
            forward += ", " + mangle(p.name);
        }
    }
    code << "static " << s << "* dsn_" << base << "(" << (params.empty() ? "void" : params.substr(2)) << ") {\n";
    code << "    " << s << "* o = (" << s << "*)ds_new_object(&" << clsName(t) << ".t, sizeof(" << s << "));\n";
    code << "    dsi_" << base << "(o" << forward << ");\n    return o;\n}\n\n";

    std::ostringstream body;
    out = &body;
    returnType = SemanticType::simple(SemanticTypeKind::Emp);
    const auto* block = ctor ? static_cast<const BlockStmt*>(ctor->body.get()) : nullptr;
    const bool withTry = block && hasTry(block);
    beginFunction(withTry);
    std::string sig = "static void dsi_" + base + "(void* self_";
    std::vector<std::pair<const FunctionParameterSymbol*, bool>> paramList;
    if (symbol) {
        for (const auto& p : symbol->parameters) {
            const bool copy = block && (withTry || assignsName(block, p.name));
            sig += ", " + cType(p.type) + " " + mangle(p.name) + (copy ? "_in" : "");
            paramList.push_back({&p, copy});
        }
    }
    indentLevel = 0;
    line(sig + ") {");
    indentLevel = 1;
    line(s + "* this_ = (" + s + "*)self_;");
    scopes.back().locals.push_back({"this", "this_", t, false});
    for (const auto& [p, copy] : paramList) {
        if (copy) {
            const bool counted = isCounted(p->type);
            std::string cname = declareLocal(p->name, p->type, counted);
            std::string value = mangle(p->name) + "_in";
            if (counted) value = "(" + cType(p->type) + ")ds_retain(" + value + ")";
            line(cType(p->type) + (volatileLocals ? " volatile " : " ") + cname + " = " + value + ";");
        } else {
            scopes.back().locals.push_back({p->name, mangle(p->name), p->type, false});
        }
    }

    size_t firstStatement = 0;
    if (info.parent) {
        const ClassInfo* parent = analyser.findClass(*info.parent);
        const std::string parentInit = "dsi_m" + std::to_string(info.parent->moduleId) + "_" + info.parent->name;
        if (block && !block->statements.empty() && isSuperInitCall(block->statements.front().get())) {
            const auto* call = static_cast<const CallExpr*>(static_cast<const ExpressionStmt*>(block->statements.front().get())->expression.get());
            const Symbol* parentCtor = analyser.memberOf(call->callee.get());
            std::vector<FunctionParameterSymbol> pp = parentCtor ? parentCtor->parameters : std::vector<FunctionParameterSymbol>{};
            beginTemps();
            std::string callCode = callWith(parentInit, arguments(call, call->args, pp), "this_");
            auto ts = endTemps();
            declareTemps(ts);
            line(callCode + ";");
            releaseTemps(ts);
            firstStatement = 1;
        } else if (!ctor && symbol) {
            line(parentInit + "(this_" + forward + ");");
        } else {
            const Symbol* pc = parent ? nearestConstructor(*parent) : nullptr;
            if (pc && !pc->parameters.empty()) {
                // the parent's init with its defaults
                beginTemps();
                std::string callCode = callWith(parentInit, arguments(nullptr, {}, pc->parameters), "this_");
                auto ts = endTemps();
                declareTemps(ts);
                line(callCode + ";");
                releaseTemps(ts);
            } else {
                line(parentInit + "(this_);");
            }
        }
    }
    // Field initialisers
    for (const auto& member : info.decl->members) {
        if (!member || member->kind != StmtKind::VarDecl) continue;
        const auto* field = static_cast<const VarDeclStmt*>(member.get());
        if (!field->name) continue;
        const SemanticType& type = info.fields.at(*field->name).type;
        if (field->initializer) {
            beginTemps();
            std::string value = own(exprAs(field->initializer.get(), type), type);
            auto ts = endTemps();
            declareTemps(ts);
            if (isCounted(type)) line("ds_store(&this_->" + mangle(*field->name) + ", " + value + ");");
            else line("this_->" + mangle(*field->name) + " = " + value + ";");
            releaseTemps(ts);
        } else if (isCounted(type) && !type.containsNil()) {
            line("this_->" + mangle(*field->name) + " = " + defaultValue(type) + ";");
        } else if (type.isEnum()) {
            line("this_->" + mangle(*field->name) + " = " + defaultValue(type) + ";");
        }
    }
    if (block) {
        for (size_t i = firstStatement; i < block->statements.size(); ++i) stmt(block->statements[i].get());
    }
    popScope(true);
    indentLevel = 0;
    line("}");
    line("");
    endFunction();
    code << body.str();
    out = nullptr;
    returnType.reset();
}

void CCodegen::emitMain() {
    // Globals start as their type's empty value
    std::ostringstream body;
    out = &body;
    indentLevel = 0;
    // Runtime errors and thrown text become Error objects (include/prelude/errors.ds)
    std::string errorMaker;
    for (const auto& module : program.modules) {
        if (module->isPrelude) errorMaker = functionName(module->id, "_error");
    }
    if (!errorMaker.empty()) {
        line("static void* ds_make_error(ds_str* message, int runtime) {");
        line("    return " + errorMaker + "(message, runtime != 0);");
        line("}");
        line("");
    }
    line("static void ds_program(void) {");
    if (!errorMaker.empty()) line("    ds_error_hook = ds_make_error;");
    if (const auto error = preludeError()) line("    ds_error_type = &" + clsName(*error) + ".t;");
    for (const auto& module : program.modules) {
        for (const auto& s : module->ast) {
            if (!s || s->kind != StmtKind::VarDecl || !isReachable(s.get())) continue;
            const auto* decl = static_cast<const VarDeclStmt*>(s.get());
            const SemanticType* type = analyser.declaredTypeOf(decl);
            if (!decl->name || !type) continue;
            Symbol g;
            g.name = *decl->name;
            g.moduleId = module->id;
            line(globalName(g) + " = " + defaultValue(*type) + ";");
        }
    }
    // Each module's globals in order, then the entry file's code
    for (const auto& module : program.modules) {
        moduleId = module->id;
        currentClass = nullptr;
        beginFunction(module->isEntry && [&] {
            for (const auto& s : module->ast) if (hasTry(s.get())) return true;
            return false;
        }());
        indentLevel = 1;
        returnType.reset();
        for (const auto& s : module->ast) {
            if (!s) continue;
            if (s->kind == StmtKind::VarDecl) {
                if (!isReachable(s.get())) continue;
                const auto* decl = static_cast<const VarDeclStmt*>(s.get());
                const SemanticType* type = analyser.declaredTypeOf(decl);
                if (!decl->initializer || !decl->name || !type) continue;
                if (s->line > 0) line("ds_trace(ds_file_m" + std::to_string(module->id) + ", " + std::to_string(s->line) + ");");
                Symbol g;
                g.name = *decl->name;
                g.moduleId = module->id;
                beginTemps();
                std::string value = own(exprAs(decl->initializer.get(), *type), *type);
                auto ts = endTemps();
                declareTemps(ts);
                if (isCounted(*type)) line("ds_store(&" + globalName(g) + ", " + value + ");");
                else line(globalName(g) + " = " + value + ";");
                releaseTemps(ts);
                continue;
            }
            switch (s->kind) {
                case StmtKind::FuncDecl:
                case StmtKind::ClassDecl:
                case StmtKind::InterfaceDecl:
                case StmtKind::EnumDecl:
                case StmtKind::Import:
                    break;
                default:
                    if (module->isEntry) stmt(s.get());
            }
        }
        popScope(true);
        endFunction();
        indentLevel = 1;
    }
    indentLevel = 0;
    line("}");
    line("");
    if (wasmPage) {
        // A page's script: the bridge starts it, and its listeners run afterwards
        line("__attribute__((used, export_name(\"dsw_start\"))) void dsw_start(void) {");
        line("    ds_main(0, NULL, ds_program);");
        line("}");
    } else {
        line("int main(int argc, char** argv) {");
        line("    return ds_main(argc, argv, ds_program);");
        line("}");
    }
    code << body.str();
    out = nullptr;
}

// ============================================================
// Scopes
// ============================================================

void CCodegen::pushScope(Scope scope) {
    scopes.push_back(std::move(scope));
}

void CCodegen::popScope(bool emitReleases) {
    if (scopes.empty()) return;
    if (emitReleases) {
        const auto& locals = scopes.back().locals;
        for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
            if (it->release) line("ds_release(" + it->cName + ");");
        }
    }
    scopes.pop_back();
}

// Leaving scopes[downTo..] (return, break, continue): release their locals
// and step out of their try blocks
void CCodegen::releaseScopes(size_t downTo, bool popTries) {
    for (size_t i = scopes.size(); i-- > downTo;) {
        const auto& locals = scopes[i].locals;
        for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
            if (it->release) line("ds_release(" + it->cName + ");");
        }
        if (popTries && !scopes[i].tryFrame.empty()) line("ds_handler = " + scopes[i].tryFrame + ".prev;");
    }
}

std::string CCodegen::declareLocal(const std::string& name, const SemanticType& type, bool release) {
    // A name used again in an inner block gets its own C name
    std::string cname = mangle(name);
    for (const auto& scope : scopes) {
        for (const auto& l : scope.locals) {
            if (l.cName == cname) cname = mangle(name) + "_" + std::to_string(tempCounter++);
        }
    }
    scopes.back().locals.push_back({name, cname, type, release});
    return cname;
}

void CCodegen::beginTemps() {
    temps.emplace_back();
}

std::vector<std::pair<std::string, std::string>> CCodegen::endTemps() {
    auto list = std::move(temps.back());
    temps.pop_back();
    return list;
}

void CCodegen::declareTemps(const std::vector<std::pair<std::string, std::string>>& list) {
    for (const auto& [name, type] : list) line(type + " " + name + " = NULL;");
}

void CCodegen::releaseTemps(const std::vector<std::pair<std::string, std::string>>& list) {
    for (const auto& t : list) line("ds_release(" + t.first + ");");
}

// ============================================================
// Statements
// ============================================================

void CCodegen::block(const Stmt* s, Scope scope) {
    pushScope(std::move(scope));
    blockBody(s);
    const std::string tryFrame = scopes.back().tryFrame;
    popScope(true);
    if (!tryFrame.empty()) line("ds_handler = " + tryFrame + ".prev;");
}

void CCodegen::blockBody(const Stmt* s) {
    if (s && s->kind == StmtKind::Block) {
        for (const auto& b : static_cast<const BlockStmt*>(s)->statements) stmt(b.get());
    } else {
        stmt(s);
    }
}

void CCodegen::stmt(const Stmt* s) {
    if (!s) return;
    switch (s->kind) {
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
            if (!static_cast<const VarDeclStmt*>(s)->initializer) break;
            [[fallthrough]];
        default:
            if (s->line > 0) line("ds_trace(ds_file_m" + std::to_string(moduleId) + ", " + std::to_string(s->line) + ");");
    }
    switch (s->kind) {
        case StmtKind::Expression: {
            const Expr* e = static_cast<const ExpressionStmt*>(s)->expression.get();
            if (e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS) {
                line("ds_fail(\"Not implemented yet (line " + std::to_string(s->line) + ")\");");
                return;
            }
            if (isSuperInitCall(s)) return;
            exprStmt(e);
            return;
        }
        case StmtKind::VarDecl: varDecl(static_cast<const VarDeclStmt*>(s)); return;
        case StmtKind::If: ifStmt(static_cast<const IfStmt*>(s)); return;
        case StmtKind::Switch: switchStmt(static_cast<const SwitchStmt*>(s)); return;
        case StmtKind::For: forStmt(static_cast<const ForStmt*>(s)); return;
        case StmtKind::ForIn: forIn(static_cast<const ForInStmt*>(s)); return;
        case StmtKind::While: whileStmt(static_cast<const WhileStmt*>(s)->condition.get(), static_cast<const WhileStmt*>(s)->body.get()); return;
        case StmtKind::Loop: whileStmt(nullptr, static_cast<const LoopStmt*>(s)->body.get()); return;
        case StmtKind::Break:
        case StmtKind::Continue: {
            size_t loop = scopes.size();
            while (loop > 0 && !scopes[loop - 1].isLoop) {
                if (scopes[loop - 1].lambda || scopes[loop - 1].function) break;
                --loop;
            }
            if (loop == 0 || !scopes[loop - 1].isLoop) unsupported("break outside a loop");
            releaseScopes(loop - 1, true);
            if (s->kind == StmtKind::Break) line("break;");
            else if (!scopes[loop - 1].continueLabel.empty()) line("goto " + scopes[loop - 1].continueLabel + ";");
            else line("continue;");
            return;
        }
        case StmtKind::Return: returnStmt(static_cast<const ReturnStmt*>(s)); return;
        case StmtKind::Block:
            line("{");
            indentLevel++;
            block(s);
            indentLevel--;
            line("}");
            return;
        case StmtKind::Try: tryStmt(static_cast<const TryStmt*>(s)); return;
        case StmtKind::Throw: throwStmt(static_cast<const ThrowStmt*>(s)); return;
        default: return;
    }
}

void CCodegen::exprStmt(const Expr* e) {
    beginTemps();
    CV v = expr(e);
    std::string c = discard(v, analyser.typeOf(e) ? *analyser.typeOf(e) : kUnknown);
    auto ts = endTemps();
    declareTemps(ts);
    line(c + ";");
    releaseTemps(ts);
}

void CCodegen::varDecl(const VarDeclStmt* decl) {
    if (!decl->name) return;
    const SemanticType* declared = analyser.declaredTypeOf(decl);
    if (!declared) return;
    // a lambda that calls itself reaches itself through its own closure
    if (decl->initializer && analyser.isRecursiveLambda(decl)) recursiveName = *decl->name;
    beginTemps();
    std::string value = decl->initializer ? own(exprAs(decl->initializer.get(), *declared), *declared) : defaultValue(*declared);
    auto ts = endTemps();
    declareTemps(ts);
    const bool counted = isCounted(*declared);
    std::string cname = declareLocal(*decl->name, *declared, counted);
    line(cType(*declared) + (volatileLocals ? " volatile " : " ") + cname + " = " + value + ";");
    releaseTemps(ts);
}

void CCodegen::ifStmt(const IfStmt* s) {
    beginTemps();
    std::string condition = use(exprAs(s->condition.get(), SemanticType::simple(SemanticTypeKind::Bul)), SemanticType::simple(SemanticTypeKind::Bul));
    auto ts = endTemps();
    if (!ts.empty()) {
        line("{");
        indentLevel++;
        declareTemps(ts);
        std::string c = temp();
        line("ds_bul " + c + " = " + condition + ";");
        releaseTemps(ts);
        condition = c;
    }
    line("if (" + condition + ") {");
    indentLevel++;
    block(s->thenBranch.get());
    indentLevel--;
    if (s->elseBranch) {
        line("} else {");
        indentLevel++;
        if (s->elseBranch->kind == StmtKind::If) {
            ifStmt(static_cast<const IfStmt*>(s->elseBranch.get()));
        } else {
            block(s->elseBranch.get());
        }
        indentLevel--;
    }
    line("}");
    if (!ts.empty()) {
        indentLevel--;
        line("}");
    }
}

void CCodegen::whileStmt(const Expr* condition, const Stmt* body) {
    const SemanticType bul = SemanticType::simple(SemanticTypeKind::Bul);
    std::string c = "1";
    std::vector<std::pair<std::string, std::string>> ts;
    if (condition) {
        beginTemps();
        c = use(exprAs(condition, bul), bul);
        ts = endTemps();
    }
    Scope loop;
    loop.isLoop = true;
    if (ts.empty()) {
        line("while (" + c + ") {");
        indentLevel++;
        block(body, loop);
        indentLevel--;
        line("}");
        return;
    }
    line("while (1) {");
    indentLevel++;
    declareTemps(ts);
    std::string flag = temp();
    line("ds_bul " + flag + " = " + c + ";");
    releaseTemps(ts);
    line("if (!" + flag + ") break;");
    block(body, loop);
    indentLevel--;
    line("}");
}

void CCodegen::forStmt(const ForStmt* s) {
    const SemanticType bul = SemanticType::simple(SemanticTypeKind::Bul);
    line("{");
    indentLevel++;
    pushScope();
    // the init has no line of its own (like C++: it's in the for's header)
    if (s->init && s->init->kind == StmtKind::VarDecl) varDecl(static_cast<const VarDeclStmt*>(s->init.get()));
    else if (s->init && s->init->kind == StmtKind::Expression) exprStmt(static_cast<const ExpressionStmt*>(s->init.get())->expression.get());
    else if (s->init) stmt(s->init.get());
    std::string c = "1", inc;
    std::vector<std::pair<std::string, std::string>> condTemps, incTemps;
    if (s->condition) {
        beginTemps();
        c = use(exprAs(s->condition.get(), bul), bul);
        condTemps = endTemps();
    }
    if (s->increment) {
        beginTemps();
        CV v = expr(s->increment.get());
        inc = discard(v, analyser.typeOf(s->increment.get()) ? *analyser.typeOf(s->increment.get()) : kUnknown);
        incTemps = endTemps();
    }
    Scope loop;
    loop.isLoop = true;
    if (condTemps.empty() && incTemps.empty()) {
        line("for (; " + c + "; " + inc + ") {");
        indentLevel++;
        block(s->body.get(), loop);
        indentLevel--;
        line("}");
    } else {
        loop.continueLabel = "ds_next" + std::to_string(tempCounter++);
        const std::string label = loop.continueLabel;
        line("while (1) {");
        indentLevel++;
        if (!condTemps.empty()) {
            declareTemps(condTemps);
            std::string flag = temp();
            line("ds_bul " + flag + " = " + c + ";");
            releaseTemps(condTemps);
            line("if (!" + flag + ") break;");
        } else {
            line("if (!(" + c + ")) break;");
        }
        line("{");
        indentLevel++;
        block(s->body.get(), loop);
        indentLevel--;
        line("}");
        line(label + ":;");
        if (!inc.empty()) {
            line("{");
            indentLevel++;
            declareTemps(incTemps);
            line(inc + ";");
            releaseTemps(incTemps);
            indentLevel--;
            line("}");
        }
        indentLevel--;
        line("}");
    }
    popScope(true);
    indentLevel--;
    line("}");
}

void CCodegen::forIn(const ForInStmt* s) {
    const auto* decl = static_cast<const VarDeclStmt*>(s->declaration.get());
    const SemanticType* declared = analyser.declaredTypeOf(decl);
    if (!declared || !decl->name) unsupported("this for loop");
    const SemanticType& iterable = analyser.typeOf(s->iterable.get()) ? *analyser.typeOf(s->iterable.get()) : kUnknown;
    const SemanticType element = iterable.is(SemanticTypeKind::Str) ? SemanticType::simple(SemanticTypeKind::Chr)
                               : iterable.is(SemanticTypeKind::Map) ? (iterable.keyType ? *iterable.keyType : kUnknown)
                               : (iterable.elementType ? *iterable.elementType : kUnknown);
    if (!iterable.is(SemanticTypeKind::List) && !iterable.is(SemanticTypeKind::Set) && !iterable.is(SemanticTypeKind::Map) &&
        !iterable.is(SemanticTypeKind::Str)) {
        unsupported("a for loop over " + iterable.toString());
    }

    line("{");
    indentLevel++;
    pushScope();
    beginTemps();
    CV it = expr(s->iterable.get());
    std::string seq;
    bool reread = false;
    if (iterable.is(SemanticTypeKind::List)) {
        // A variable or field is read again on each step (like C++: the loop
        // sees items pushed inside it); anything else is held while it runs
        const Expr* inner = s->iterable.get();
        while (inner->kind == ExprKind::Grouping) inner = static_cast<const GroupingExpr*>(inner)->inner.get();
        if (!it.owned && (inner->kind == ExprKind::Identifier || inner->kind == ExprKind::Member)) {
            seq = it.code;
            reread = true;
        } else {
            seq = own(it, iterable);
        }
    } else if (iterable.is(SemanticTypeKind::Set)) {
        seq = own(it, iterable);
    } else if (iterable.is(SemanticTypeKind::Map)) {
        seq = "ds_map_keys(" + use(it, iterable) + ")";
    } else {
        seq = "ds_str_chars(" + use(it, iterable) + ")";
    }
    auto ts = endTemps();
    declareTemps(ts);
    if (!reread) {
        std::string held = temp();
        line("ds_list* " + held + " = " + seq + ";");
        scopes.back().locals.push_back({"", held, iterable, true});
        seq = held;
    }
    releaseTemps(ts);
    const std::string i = temp();
    const std::string et = cType(element);
    line("for (int64_t " + i + " = 0; " + i + " < (" + seq + ")->len; " + i + "++) {");
    indentLevel++;
    Scope loop;
    loop.isLoop = true;
    pushScope(loop);
    const bool counted = isCounted(*declared);
    std::string cname = declareLocal(*decl->name, *declared, counted);
    std::string item = "((" + et + "*)(" + seq + ")->data)[" + i + "]";
    if (counted) item = "(" + et + ")ds_retain(" + item + ")";
    item = convert({item, counted}, element, *declared).code;
    line(cType(*declared) + (volatileLocals ? " volatile " : " ") + cname + " = " + item + ";");
    blockBody(s->body.get());
    popScope(true);
    indentLevel--;
    line("}");
    popScope(true);
    indentLevel--;
    line("}");
}

void CCodegen::switchStmt(const SwitchStmt* s) {
    const SemanticType& type = analyser.typeOf(s->discriminant.get()) ? *analyser.typeOf(s->discriminant.get()) : kUnknown;
    line("{");
    indentLevel++;
    pushScope();
    beginTemps();
    std::string value = own(expr(s->discriminant.get()), type);
    auto ts = endTemps();
    declareTemps(ts);
    std::string v = temp();
    scopes.back().locals.push_back({"", v, type, isCounted(type)});
    line(cType(type) + " " + v + " = " + value + ";");
    releaseTemps(ts);
    bool first = true;
    const SwitchCase* defaultCase = nullptr;
    for (const auto& arm : s->cases) {
        if (!arm.value) {
            defaultCase = &arm;
            continue;
        }
        std::string test;
        if (const SemanticType* enumType = analyser.enumCaseOf(arm.value.get())) {
            const std::string name = static_cast<const IdentifierExpr*>(arm.value.get())->name.lexeme;
            test = v + " == dsv_m" + std::to_string(enumType->moduleId) + "_" + enumType->name + "_" + name;
        } else {
            beginTemps();
            CV armValue = exprAs(arm.value.get(), type);
            auto armTemps = endTemps();
            if (!armTemps.empty()) unsupported("a switch case that needs temporaries");
            const SemanticType base = withoutNil(type);
            if (base.is(SemanticTypeKind::Str)) {
                test = (type.containsNil() ? v + " && " : std::string()) + "ds_str_eq(" + v + ", " + armValue.code + ")";
            } else if (isOptValue(type)) {
                test = v + ".has && " + v + ".value == " + armValue.code;
            } else if (isCounted(type)) {
                unsupported("a switch on " + type.toString());
            } else {
                test = v + " == " + armValue.code;
            }
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
    popScope(true);
    indentLevel--;
    line("}");
}

void CCodegen::tryStmt(const TryStmt* s) {
    const std::string frame = "ds_try" + std::to_string(tempCounter++);
    line("{");
    indentLevel++;
    line("ds_catch " + frame + ";");
    line(frame + ".prev = ds_handler;");
    line("ds_handler = &" + frame + ";");
    line("if (DS_TRY(" + frame + ")) {");
    indentLevel++;
    Scope body;
    body.tryFrame = frame;
    block(s->tryBlock.get(), body);
    indentLevel--;
    line("} else {");
    indentLevel++;
    line("ds_handler = " + frame + ".prev;");

    // The arms, in order: class instances by class, Str messages (runtime
    // errors too), then a catch-all
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
    bool first = true;
    for (const CatchClause* arm : classArms) {
        const SemanticType& type = *analyser.catchTypeOf(arm);
        std::string test;
        if (type.isInterface()) {
            auto it = interfaceIds.find(type.key());
            test = it == interfaceIds.end() ? "0" : "ds_interface(ds_thrown, " + std::to_string(it->second) + ")";
        } else {
            test = "ds_is_class(ds_thrown, &" + clsName(type) + ".t)";
        }
        line(std::string(first ? "if (" : "} else if (") + "ds_thrown && " + test + ") {");
        first = false;
        indentLevel++;
        pushScope();
        std::string cname = declareLocal(*arm->param, type, true);
        line(cType(type) + " " + cname + " = (" + cType(type) + ")ds_take(&ds_thrown);");
        line("ds_clear_error();");
        blockBody(arm->body.get());
        popScope(true);
        indentLevel--;
    }
    if (strArm) {
        // Text, and any Error (runtime errors are IndexError, ... objects): its message
        const std::optional<SemanticType> error = preludeError();
        const std::string isError = error ? " || (ds_thrown && ds_is_class(ds_thrown, &" + clsName(*error) + ".t))" : "";
        line(std::string(first ? "if (" : "} else if (") + "ds_error" + isError + ") {");
        first = false;
        indentLevel++;
        pushScope();
        std::string cname = declareLocal(*strArm->param, SemanticType::simple(SemanticTypeKind::Str), true);
        if (error) {
            line("ds_str* " + cname + " = ds_error ? (ds_str*)ds_take(&ds_error) : (ds_str*)ds_retain(((" + structName(*error) +
                 "*)ds_thrown)->" + mangle("message") + ");");
        } else {
            line("ds_str* " + cname + " = (ds_str*)ds_take(&ds_error);");
        }
        line("ds_clear_error();");
        blockBody(strArm->body.get());
        popScope(true);
        indentLevel--;
    }
    if (catchAll) {
        line(first ? "{" : "} else {");
        indentLevel++;
        line("ds_clear_error();");
        block(catchAll->body.get());
        indentLevel--;
        line("}");
    } else {
        line(first ? "{" : "} else {");
        line("    ds_rethrow();");
        line("}");
    }
    indentLevel--;
    line("}");
    indentLevel--;
    line("}");
}

void CCodegen::returnStmt(const ReturnStmt* s) {
    // the function (or lambda) body's scope
    size_t base = scopes.size();
    while (base > 0 && !scopes[base - 1].function && !scopes[base - 1].lambda) --base;
    if (base > 0) --base;
    if (!s->value || !returnType || returnType->isVoidLike()) {
        if (s->value) {
            beginTemps();
            CV v = expr(s->value.get());
            std::string c = discard(v, analyser.typeOf(s->value.get()) ? *analyser.typeOf(s->value.get()) : kUnknown);
            auto ts = endTemps();
            declareTemps(ts);
            line(c + ";");
            releaseTemps(ts);
        }
        releaseScopes(base, true);
        line("return;");
        return;
    }
    beginTemps();
    std::string value = own(exprAs(s->value.get(), *returnType), *returnType);
    auto ts = endTemps();
    line("{");
    indentLevel++;
    declareTemps(ts);
    std::string r = temp();
    line(cType(*returnType) + " " + r + " = " + value + ";");
    releaseTemps(ts);
    releaseScopes(base, true);
    line("return " + r + ";");
    indentLevel--;
    line("}");
}

// The prelude's Error class (include/prelude/errors.ds), when it is loaded
std::optional<SemanticType> CCodegen::preludeError() const {
    for (const auto& module : program.modules) {
        if (module->isPrelude) return SemanticType::userDefined("Error", module->id, UserTypeKind::Class);
    }
    return std::nullopt;
}

// The value may call constructors that move the trace line; point it back at the throw.
void CCodegen::traceThrow(const ThrowStmt* s) {
    if (s->line > 0) line("ds_trace(ds_file_m" + std::to_string(moduleId) + ", " + std::to_string(s->line) + ");");
}

void CCodegen::throwStmt(const ThrowStmt* s) {
    const SemanticType& type = analyser.typeOf(s->value.get()) ? *analyser.typeOf(s->value.get()) : kUnknown;
    beginTemps();
    std::string value = own(expr(s->value.get()), type);
    auto ts = endTemps();
    line("{");
    indentLevel++;
    declareTemps(ts);
    std::string v = temp();
    if (type.is(SemanticTypeKind::Str)) {
        line("ds_str* " + v + " = " + value + ";");
        releaseTemps(ts);
        traceThrow(s);
        line("ds_throw_str(" + v + ");");
    } else {
        line("void* " + v + " = " + value + ";");
        releaseTemps(ts);
        traceThrow(s);
        line("ds_throw_obj(" + v + ");");
    }
    indentLevel--;
    line("}");
}

} // namespace dessa
