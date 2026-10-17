// src/analyser/analyser.cpp
// Declarations, modules, scopes, types and statements.
// Expressions live in analyser_expr.cpp.
#include "analyser.hpp"
#include "builtins.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace dessa {

Analyser::Analyser() = default;
Analyser::~Analyser() {
    while (currentScope && currentScope->kind() != ScopeKind::Module && currentScope != builtinScope.get()) endScope();
}

// ============================================================
// Entry point
// ============================================================

void Analyser::analyse(const Program& prog) {
    program = &prog;
    diagnostics.clear();
    classes.clear(); interfaces.clear(); enums.clear();
    classKeys.clear(); interfaceKeys.clear(); enumKeys.clear();
    usedHeaders.clear();
    usedLibraries.clear();
    exprTypes.clear(); expectedTypes.clear(); identSymbols.clear(); memberSymbols.clear();
    optionalChains.clear(); argOrders.clear(); declTypes.clear(); functionSymbols.clear(); catchTypes.clear(); isTargets.clear();
    recursiveLambdas.clear(); recursiveRefs.clear(); definingLambda = nullptr; enumCases.clear();

    builtinScope = std::make_unique<Scope>(ScopeKind::Module);
    for (const auto& fn : globalBuiltins()) {
        Symbol symbol;
        symbol.kind = SymbolKind::Builtin;
        symbol.name = fn.name;
        symbol.cppName = fn.cpp;
        symbol.variadic = fn.variadic;
        symbol.isGlobal = true;
        symbol.returnType = parseBuiltinType(fn.returnType);
        for (const auto& p : fn.params) symbol.parameters.push_back({p.name, parseBuiltinType(p.type)});
        std::vector<SemanticType> params;
        for (const auto& p : symbol.parameters) params.push_back(p.type);
        symbol.type = SemanticType::functionOf(params, symbol.returnType);
        builtinScope->declare(symbol);
    }

    modules.clear();
    modules.resize(prog.modules.size());
    for (size_t i = 0; i < prog.modules.size(); ++i) {
        modules[i].scope = std::make_unique<Scope>(ScopeKind::Module, builtinScope.get());
    }

    // Dependencies come first, so by the time a module's imports are
    // processed, everything it imports already has its exports collected.
    for (const auto& module : prog.modules) {
        currentModule = module->id;
        currentScope = modules[currentModule].scope.get();
        declareTypeNames(*module);
        processImports(*module);
        collectTypeMembers(*module);
        collectFunctionSignatures(*module);
        checkClassHierarchy(*module);
        collectExports(*module);
    }

    for (const auto& module : prog.modules) {
        currentModule = module->id;
        currentScope = modules[currentModule].scope.get();
        analyseModuleBody(*module);
    }

    // Order classes so that a parent always precedes its children.
    std::vector<std::string> ordered;
    std::unordered_set<std::string> placed;
    std::function<void(const std::string&, int)> place = [&](const std::string& key, int depth) {
        if (placed.count(key) || depth > 64) return;
        const ClassInfo& info = classes.at(key);
        if (info.parent && classes.count(info.parent->key())) place(info.parent->key(), depth + 1);
        placed.insert(key);
        ordered.push_back(key);
    };
    for (const auto& key : classKeys) place(key, 0);
    classKeys = std::move(ordered);
}

bool Analyser::hasErrors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.severity == DiagnosticSeverity::Error;
    });
}

const std::vector<Diagnostic>& Analyser::getDiagnostics() const {
    return diagnostics;
}

// ============================================================
// Result accessors
// ============================================================

namespace {
template <typename Map, typename Key>
auto lookup(const Map& map, const Key& key) -> const typename Map::mapped_type* {
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}
} // namespace

const SemanticType* Analyser::typeOf(const Expr* e) const { return lookup(exprTypes, e); }
const SemanticType* Analyser::expectedTypeOf(const Expr* e) const { return lookup(expectedTypes, e); }
const Symbol* Analyser::symbolOf(const Expr* e) const { return lookup(identSymbols, e); }
const Symbol* Analyser::memberOf(const Expr* e) const { return lookup(memberSymbols, e); }
bool Analyser::isOptionalChain(const Expr* e) const { return optionalChains.count(e) != 0; }
const ArgumentOrder* Analyser::argumentOrder(const Expr* e) const { return lookup(argOrders, e); }
const SemanticType* Analyser::declaredTypeOf(const Stmt* s) const { return lookup(declTypes, s); }
const Symbol* Analyser::functionOf(const FuncDeclStmt* d) const { return lookup(functionSymbols, d); }
const SemanticType* Analyser::catchTypeOf(const CatchClause* c) const { return lookup(catchTypes, c); }
const SemanticType* Analyser::isTargetOf(const IsExpr* e) const { return lookup(isTargets, e); }

const ClassInfo* Analyser::findClass(const SemanticType& type) const {
    if (!type.isClass()) return nullptr;
    return lookup(classes, type.key());
}

const InterfaceInfo* Analyser::findInterface(const SemanticType& type) const {
    if (!type.isInterface()) return nullptr;
    return lookup(interfaces, type.key());
}

const EnumInfo* Analyser::findEnum(const SemanticType& type) const {
    if (!type.isEnum()) return nullptr;
    return lookup(enums, type.key());
}

const std::unordered_map<std::string, Symbol>& Analyser::exportsOf(int moduleId) const {
    return modules.at(static_cast<size_t>(moduleId)).exports;
}

const Scope* Analyser::moduleScope(int moduleId) const {
    if (moduleId < 0 || static_cast<size_t>(moduleId) >= modules.size()) return nullptr;
    return modules[static_cast<size_t>(moduleId)].scope.get();
}

// ============================================================
// Diagnostics
// ============================================================

void Analyser::report(DiagnosticSeverity severity, int line, int column, const std::string& message) {
    std::string file;
    if (program && currentModule >= 0 && static_cast<size_t>(currentModule) < program->modules.size()) {
        file = program->modules[currentModule]->displayPath;
    }
    diagnostics.push_back({severity, message, file, line, column});
}

void Analyser::error(const Expr* at, const std::string& message) {
    report(DiagnosticSeverity::Error, at ? at->line : 0, at ? at->column : 0, message);
}

void Analyser::error(const Stmt* at, const std::string& message) {
    report(DiagnosticSeverity::Error, at ? at->line : 0, at ? at->column : 0, message);
}

void Analyser::error(int line, int column, const std::string& message) {
    report(DiagnosticSeverity::Error, line, column, message);
}

void Analyser::warning(const Expr* at, const std::string& message) {
    report(DiagnosticSeverity::Warning, at ? at->line : 0, at ? at->column : 0, message);
}

// ============================================================
// Module pipeline
// ============================================================

void Analyser::declareTypeNames(const Module& module) {
    for (const auto& statement : module.ast) {
        if (!statement) continue;
        Symbol symbol;
        symbol.moduleId = module.id;
        symbol.isGlobal = true;
        symbol.line = statement->line;
        symbol.column = statement->column;

        if (statement->kind == StmtKind::ClassDecl) {
            const auto* decl = static_cast<const ClassDeclStmt*>(statement.get());
            ClassInfo info;
            info.name = decl->name;
            info.moduleId = module.id;
            info.decl = decl;
            symbol.kind = SymbolKind::Class;
            symbol.name = decl->name;
            symbol.type = info.type();
            if (declareSymbol(symbol, statement->line, statement->column)) {
                classKeys.push_back(symbol.type.key());
                classes.emplace(symbol.type.key(), std::move(info));
            }
        } else if (statement->kind == StmtKind::InterfaceDecl) {
            const auto* decl = static_cast<const InterfaceDeclStmt*>(statement.get());
            InterfaceInfo info;
            info.name = decl->name;
            info.moduleId = module.id;
            info.decl = decl;
            symbol.kind = SymbolKind::Interface;
            symbol.name = decl->name;
            symbol.type = info.type();
            if (declareSymbol(symbol, statement->line, statement->column)) {
                interfaceKeys.push_back(symbol.type.key());
                interfaces.emplace(symbol.type.key(), std::move(info));
            }
        } else if (statement->kind == StmtKind::EnumDecl) {
            const auto* decl = static_cast<const EnumDeclStmt*>(statement.get());
            EnumInfo info;
            info.name = decl->name;
            info.moduleId = module.id;
            info.decl = decl;
            for (const auto& value : decl->values) {
                if (std::find(info.values.begin(), info.values.end(), value.name) != info.values.end()) {
                    error(statement.get(), "Duplicate enum value '" + value.name + "' in enum '" + decl->name + "'");
                    continue;
                }
                info.values.push_back(value.name);
            }
            symbol.kind = SymbolKind::Enum;
            symbol.name = decl->name;
            symbol.type = info.type();
            if (declareSymbol(symbol, statement->line, statement->column)) {
                enumKeys.push_back(symbol.type.key());
                enums.emplace(symbol.type.key(), std::move(info));
            }
        }
    }
}

void Analyser::importSymbol(const Symbol& exported, const std::string& localName, int line, int column) {
    Symbol symbol = exported;
    if (symbol.originalName.empty()) symbol.originalName = symbol.name;
    symbol.name = localName;
    if (Symbol* existing = currentScope->resolveLocal(localName)) {
        if (existing->moduleId == symbol.moduleId && existing->originalName == symbol.originalName) return; // same thing twice
        error(line, column, "Imported name '" + localName + "' conflicts with an existing declaration");
        return;
    }
    currentScope->declare(symbol);
}

void Analyser::processImports(const Module& module) {
    for (const auto& statement : module.ast) {
        if (!statement || statement->kind != StmtKind::Import) continue;
        const auto* import = static_cast<const ImportStmt*>(statement.get());

        // name -> symbol, gathered across every module named in $(...)
        std::unordered_map<std::string, std::vector<Symbol>> available;
        bool anyResolved = false;

        for (const auto& spec : import->modules) {
            if (spec.isBuiltin) {
                // A builtin library has a native (C++) part, a Dessa part
                // (include/<lib>/<lib>.dm), or both.
                const BuiltinLibrary* lib = findBuiltinLibrary(spec.path);
                auto dessaPart = module.resolvedImports.find(&spec);
                if (!lib && dessaPart == module.resolvedImports.end()) {
                    error(spec.line, spec.column, "Unknown library '<" + spec.path + ">'; if it's a riv package, install it with: riv add " + spec.path);
                    continue;
                }
                anyResolved = true;
                if (dessaPart != module.resolvedImports.end()) {
                    for (const auto& [name, symbol] : modules[dessaPart->second].exports) {
                        available[name].push_back(symbol);
                    }
                }
                if (!lib) continue;
                usedHeaders.insert(lib->header);
                usedLibraries.insert(lib->name);
                for (const auto& fn : lib->functions) {
                    Symbol s;
                    s.kind = SymbolKind::Builtin;
                    s.name = fn.name;
                    s.cppName = fn.cpp;
                    s.isGlobal = true;
                    s.moduleId = -1;
                    s.returnType = parseBuiltinType(fn.returnType);
                    std::vector<SemanticType> params;
                    for (const auto& p : fn.params) {
                        s.parameters.push_back({p.name, parseBuiltinType(p.type)});
                        params.push_back(s.parameters.back().type);
                    }
                    s.type = SemanticType::functionOf(params, s.returnType);
                    available[fn.name].push_back(s);
                }
                for (const auto& c : lib->constants) {
                    Symbol s;
                    s.kind = SymbolKind::Builtin;
                    s.name = c.name;
                    s.cppName = c.cpp;
                    s.isGlobal = true;
                    s.isConst = true;
                    s.moduleId = -1;
                    s.type = parseBuiltinType(c.type);
                    available[c.name].push_back(s);
                }
            } else {
                auto it = module.resolvedImports.find(&spec);
                if (it == module.resolvedImports.end()) continue; // loader already reported it
                anyResolved = true;
                for (const auto& [name, symbol] : modules[it->second].exports) {
                    available[name].push_back(symbol);
                }
            }
        }

        if (!anyResolved) continue;

        if (import->importAll) {
            for (const auto& [name, candidates] : available) {
                // _helpers of a library are only imported by name
                if (!name.empty() && name[0] == '_') continue;
                if (candidates.size() > 1) {
                    error(statement.get(), "'" + name + "' is exported by more than one imported module; import it by name from one of them");
                    continue;
                }
                importSymbol(candidates.front(), name, statement->line, statement->column);
            }
            continue;
        }

        for (const auto& item : import->names) {
            auto it = available.find(item.name);
            if (it == available.end()) {
                error(item.line, item.column, "Module does not export '" + item.name + "'");
                continue;
            }
            if (it->second.size() > 1) {
                error(item.line, item.column, "'" + item.name + "' is exported by more than one of the listed modules");
                continue;
            }
            importSymbol(it->second.front(), item.alias.value_or(item.name), item.line, item.column);
        }
    }
}

void Analyser::collectTypeMembers(const Module& module) {
    for (const auto& statement : module.ast) {
        if (!statement) continue;
        if (statement->kind == StmtKind::InterfaceDecl) {
            const auto* decl = static_cast<const InterfaceDeclStmt*>(statement.get());
            auto it = interfaces.find(SemanticType::userDefined(decl->name, module.id, UserTypeKind::Interface).key());
            if (it != interfaces.end() && it->second.decl == decl) collectInterfaceMembers(it->second);
        }
    }
    for (const auto& statement : module.ast) {
        if (!statement) continue;
        if (statement->kind == StmtKind::ClassDecl) {
            const auto* decl = static_cast<const ClassDeclStmt*>(statement.get());
            auto it = classes.find(SemanticType::userDefined(decl->name, module.id, UserTypeKind::Class).key());
            if (it != classes.end() && it->second.decl == decl) collectClassMembers(it->second);
        }
    }
}

void Analyser::collectInterfaceMembers(InterfaceInfo& info) {
    for (const auto& member : info.decl->members) {
        if (!member) continue;
        if (member->kind == StmtKind::VarDecl) {
            const auto* field = static_cast<const VarDeclStmt*>(member.get());
            if (!field->name) continue;
            if (info.fields.count(*field->name) || info.methods.count(*field->name)) {
                error(member.get(), "Duplicate interface member '" + *field->name + "' in interface '" + info.name + "'");
                continue;
            }
            info.fieldOrder.push_back(*field->name);
            info.fields.emplace(*field->name, resolveType(field->type, member->line, member->column));
        } else if (member->kind == StmtKind::FuncDecl) {
            const auto* method = static_cast<const FuncDeclStmt*>(member.get());
            if (info.fields.count(method->name) || info.methods.count(method->name)) {
                error(member.get(), "Duplicate interface member '" + method->name + "' in interface '" + info.name + "'");
                continue;
            }
            info.methodOrder.push_back(method->name);
            info.methods.emplace(method->name, makeFunctionSymbol(method, SymbolKind::Method));
        }
    }
}

void Analyser::collectClassMembers(ClassInfo& info) {
    const ClassDeclStmt* decl = info.decl;
    const std::string key = info.type().key();

    if (decl->parentClassName) {
        SemanticType parent = resolveTypeName(*decl->parentClassName, decl->line, decl->column);
        if (parent.isClass()) {
            info.parent = parent;
        } else if (!parent.isUnknown()) {
            error(decl, "'" + *decl->parentClassName + "' is not a class and cannot be inherited from");
        }
    }
    if (decl->interfaceName) {
        SemanticType iface = resolveTypeName(*decl->interfaceName, decl->line, decl->column);
        if (iface.isInterface()) {
            info.interface = iface;
        } else if (!iface.isUnknown()) {
            error(decl, "'" + *decl->interfaceName + "' is not an interface");
        }
    }

    for (const auto& member : decl->members) {
        if (!member) continue;
        if (member->kind == StmtKind::VarDecl) {
            const auto* field = static_cast<const VarDeclStmt*>(member.get());
            if (!field->name) continue;
            if (info.fields.count(*field->name) || info.methods.count(*field->name)) {
                error(member.get(), "Duplicate class member '" + *field->name + "' in class '" + info.name + "'");
                continue;
            }
            Symbol symbol;
            symbol.kind = SymbolKind::Field;
            symbol.name = *field->name;
            symbol.type = resolveType(field->type, member->line, member->column);
            symbol.isConst = field->isConst;
            symbol.ownerClass = key;
            symbol.moduleId = info.moduleId;
            symbol.line = member->line;
            symbol.column = member->column;
            declTypes[member.get()] = symbol.type;
            info.fieldOrder.push_back(symbol.name);
            info.fields.emplace(symbol.name, symbol);
        } else if (member->kind == StmtKind::FuncDecl) {
            const auto* method = static_cast<const FuncDeclStmt*>(member.get());
            if (method->isConstructor) {
                if (info.constructor) {
                    error(member.get(), "Class '" + info.name + "' already has an init constructor");
                    continue;
                }
                Symbol ctor = makeFunctionSymbol(method, SymbolKind::Method);
                ctor.ownerClass = key;
                info.constructor = ctor;
                functionSymbols[method] = ctor;
                continue;
            }
            if (method->name == info.name) {
                error(member.get(), "Constructors are declared with init(...), not as a method named '" + info.name + "'");
            }
            if (info.fields.count(method->name) || info.methods.count(method->name)) {
                error(member.get(), "Duplicate class member '" + method->name + "' in class '" + info.name + "'");
                continue;
            }
            Symbol symbol = makeFunctionSymbol(method, SymbolKind::Method);
            symbol.ownerClass = key;
            functionSymbols[method] = symbol;
            info.methods.emplace(symbol.name, symbol);
        } else {
            error(member.get(), "Only fields, methods and init are allowed inside a class");
        }
    }
}

Symbol Analyser::makeFunctionSymbol(const FuncDeclStmt* declaration, SymbolKind kind) {
    Symbol symbol;
    symbol.kind = kind;
    symbol.name = declaration->name;
    symbol.moduleId = currentModule;
    symbol.line = declaration->line;
    symbol.column = declaration->column;
    symbol.returnType = resolveType(declaration->returnType, declaration->line, declaration->column);
    std::vector<SemanticType> params;
    std::unordered_set<std::string> seen;
    bool seenDefault = false;
    for (size_t i = 0; i < declaration->params.size(); ++i) {
        const Param& parameter = declaration->params[i];
        const int line = parameter.line ? parameter.line : declaration->line;
        const int column = parameter.line ? parameter.column : declaration->column;
        if (!seen.insert(parameter.name).second) {
            error(line, column, "Duplicate parameter '" + parameter.name + "' in '" + declaration->name + "'");
        }
        SemanticType type = resolveType(parameter.type, line, column);
        if (type.isVoidLike()) error(line, column, "Parameter '" + parameter.name + "' cannot have type Emp");

        FunctionParameterSymbol p{parameter.name, type};
        if (parameter.isVariadic) {
            // names[Str*] is received as a Str<>
            if (i + 1 != declaration->params.size()) {
                error(line, column, "Variadic parameter '" + parameter.name + "' must be the last parameter");
            }
            if (parameter.defaultValue) {
                error(line, column, "Variadic parameter '" + parameter.name + "' cannot have a default value (it defaults to an empty list)");
            }
            p.type = SemanticType::listOf(type);
            p.variadic = true;
        } else if (parameter.defaultValue) {
            seenDefault = true;
            p.defaultValue = parameter.defaultValue.get();
            checkDefaultValue(parameter.defaultValue.get(), p.type, parameter.name);
        } else if (seenDefault) {
            error(line, column, "Parameter '" + parameter.name + "' needs a default value because an earlier parameter has one");
        }
        symbol.parameters.push_back(p);
        params.push_back(p.type);
    }
    symbol.type = SemanticType::functionOf(params, symbol.returnType);
    return symbol;
}

// A top-level `const` of this module, found in the AST (constants declared
// later in the file aren't in scope yet while signatures are collected).
const VarDeclStmt* Analyser::findModuleConstant(const std::string& name) const {
    if (!program || currentModule < 0 || static_cast<size_t>(currentModule) >= program->modules.size()) return nullptr;
    for (const auto& s : program->modules[static_cast<size_t>(currentModule)]->ast) {
        if (!s || s->kind != StmtKind::VarDecl) continue;
        const auto* decl = static_cast<const VarDeclStmt*>(s.get());
        if (decl->isConst && decl->name && *decl->name == name) return decl;
    }
    return nullptr;
}

// Default values are evaluated where the function is called, so they must
// be constants: literals, nil, enum values, empty collections, or a
// top-level const whose own value is one of those.
void Analyser::checkDefaultValue(const Expr* value, const SemanticType& type, const std::string& name) {
    int depth = 0;
    std::function<bool(const Expr*)> isConstant = [&](const Expr* e) -> bool {
        if (!e) return false;
        switch (e->kind) {
            case ExprKind::Identifier: {
                const std::string& id = static_cast<const IdentifierExpr*>(e)->name.lexeme;
                if (const Symbol* s = resolveSymbol(id)) {
                    return (s->kind == SymbolKind::Constant && s->isGlobal) || (s->kind == SymbolKind::Builtin && s->isConst);
                }
                const VarDeclStmt* decl = findModuleConstant(id);
                if (!decl || ++depth > 16) return false;
                return isConstant(decl->initializer.get());
            }
            case ExprKind::Map:
                return static_cast<const MapExpr*>(e)->entries.empty();
            case ExprKind::Literal:
                return static_cast<const LiteralExpr*>(e)->token.type != TokenType::ELLIPSIS;
            case ExprKind::Grouping:
                return isConstant(static_cast<const GroupingExpr*>(e)->inner.get());
            case ExprKind::Unary:
                return isConstant(static_cast<const UnaryExpr*>(e)->operand.get());
            case ExprKind::TemplateString:
                return static_cast<const TemplateStringExpr*>(e)->interpolations.empty();
            case ExprKind::List:
                return static_cast<const ListExpr*>(e)->elements.empty();
            case ExprKind::Set:
                return static_cast<const SetExpr*>(e)->elements.empty();
            case ExprKind::Member: {
                const auto* m = static_cast<const MemberExpr*>(e);
                if (m->object->kind != ExprKind::Identifier) return false;
                const Symbol* s = resolveSymbol(static_cast<const IdentifierExpr*>(m->object.get())->name.lexeme);
                return s && s->kind == SymbolKind::Enum;
            }
            default:
                return false;
        }
    };
    if (!isConstant(value)) {
        error(value, "Default value of '" + name + "' must be a constant (a literal, nil, an enum value, an empty list/set/map, or a top-level const)");
        return;
    }
    // A const declared further down this file: refer to it directly.
    if (value->kind == ExprKind::Identifier && !resolveSymbol(static_cast<const IdentifierExpr*>(value)->name.lexeme)) {
        const VarDeclStmt* decl = findModuleConstant(static_cast<const IdentifierExpr*>(value)->name.lexeme);
        Symbol symbol;
        symbol.kind = SymbolKind::Constant;
        symbol.name = *decl->name;
        symbol.type = resolveType(decl->type, decl->line, decl->column);
        symbol.isConst = true;
        symbol.isGlobal = true;
        symbol.moduleId = currentModule;
        symbol.line = decl->line;
        symbol.column = decl->column;
        identSymbols[value] = symbol;
        exprTypes[value] = symbol.type;
        checkConversion(type, value, symbol.type, "as the default value of '" + name + "'");
        return;
    }
    SemanticType actual = analyseExpression(value);
    checkConversion(type, value, actual, "as the default value of '" + name + "'");
}

void Analyser::collectFunctionSignatures(const Module& module) {
    for (const auto& statement : module.ast) {
        if (!statement || statement->kind != StmtKind::FuncDecl) continue;
        const auto* decl = static_cast<const FuncDeclStmt*>(statement.get());
        Symbol symbol = makeFunctionSymbol(decl, SymbolKind::Function);
        symbol.isGlobal = true;
        if (declareSymbol(symbol, statement->line, statement->column)) {
            functionSymbols[decl] = symbol;
        }
    }
}

void Analyser::checkClassHierarchy(const Module& module) {
    // Report every class that is part of an inheritance cycle, then cut the
    // cycles so the checks below terminate.
    std::vector<ClassInfo*> cyclic;
    for (const auto& statement : module.ast) {
        if (!statement || statement->kind != StmtKind::ClassDecl) continue;
        const auto* decl = static_cast<const ClassDeclStmt*>(statement.get());
        auto it = classes.find(SemanticType::userDefined(decl->name, module.id, UserTypeKind::Class).key());
        if (it == classes.end() || it->second.decl != decl) continue;
        const std::string self = it->second.type().key();
        std::unordered_set<std::string> visited;
        for (auto p = it->second.parent; p && visited.insert(p->key()).second; ) {
            if (p->key() == self) {
                error(decl, "Class '" + decl->name + "' inherits from itself");
                cyclic.push_back(&it->second);
                break;
            }
            const ClassInfo* parentInfo = findClass(*p);
            p = parentInfo ? parentInfo->parent : std::nullopt;
        }
    }
    for (ClassInfo* info : cyclic) info->parent.reset();

    for (const auto& statement : module.ast) {
        if (!statement || statement->kind != StmtKind::ClassDecl) continue;
        const auto* decl = static_cast<const ClassDeclStmt*>(statement.get());
        auto it = classes.find(SemanticType::userDefined(decl->name, module.id, UserTypeKind::Class).key());
        if (it == classes.end() || it->second.decl != decl) continue;
        ClassInfo& info = it->second;

        // Overrides must keep the signature; fields can't be redeclared.
        if (info.parent) {
            const std::string parentKey = info.parent->key();
            for (const auto& name : info.fieldOrder) {
                if (const Symbol* inherited = findClassMember(parentKey, name)) {
                    error(info.fields.at(name).line, info.fields.at(name).column,
                          "Field '" + name + "' is already declared in a parent of '" + info.name + "'" +
                          (inherited->kind == SymbolKind::Method ? " as a method" : ""));
                }
            }
            for (const auto& [name, method] : info.methods) {
                const Symbol* inherited = findClassMember(parentKey, name);
                if (!inherited) continue;
                if (inherited->kind != SymbolKind::Method) {
                    error(method.line, method.column, "Method '" + name + "' clashes with an inherited field");
                } else if (inherited->type != method.type) {
                    error(method.line, method.column, "Method '" + name + "' overrides a parent method with a different signature: expected " +
                          inherited->type.toString() + ", got " + method.type.toString());
                }
            }
            // A class without its own init inherits its parent's init (like Python).
        }

        // Interface conformance
        if (info.interface) {
            const InterfaceInfo* iface = findInterface(*info.interface);
            if (iface) {
                const std::string key = info.type().key();
                for (const auto& name : iface->fieldOrder) {
                    const SemanticType& expected = iface->fields.at(name);
                    const Symbol* member = findClassMember(key, name);
                    if (!member || member->kind != SymbolKind::Field) {
                        error(decl, "Class '" + info.name + "' does not implement interface field '" + name + "[" + expected.toString() + "]' of '" + iface->name + "'");
                    } else if (member->type != expected) {
                        error(member->line, member->column, "Field '" + name + "' has type " + member->type.toString() +
                              " but interface '" + iface->name + "' requires " + expected.toString());
                    }
                }
                for (const auto& name : iface->methodOrder) {
                    const Symbol& expected = iface->methods.at(name);
                    const Symbol* member = findClassMember(key, name);
                    if (!member || member->kind != SymbolKind::Method) {
                        error(decl, "Class '" + info.name + "' does not implement interface method '" + name + "' of '" + iface->name + "'");
                    } else if (member->type != expected.type) {
                        error(member->line, member->column, "Method '" + name + "' has signature " + member->type.toString() +
                              " but interface '" + iface->name + "' requires " + expected.type.toString());
                    }
                }
            }
        }
    }
}

void Analyser::collectExports(const Module& module) {
    auto& exports = modules[module.id].exports;
    for (const auto& statement : module.ast) {
        if (!statement) continue;
        std::string name;
        switch (statement->kind) {
            case StmtKind::ClassDecl: name = static_cast<const ClassDeclStmt*>(statement.get())->name; break;
            case StmtKind::InterfaceDecl: name = static_cast<const InterfaceDeclStmt*>(statement.get())->name; break;
            case StmtKind::EnumDecl: name = static_cast<const EnumDeclStmt*>(statement.get())->name; break;
            case StmtKind::FuncDecl: name = static_cast<const FuncDeclStmt*>(statement.get())->name; break;
            case StmtKind::VarDecl: {
                // Variables are exported by declared type; they're added to
                // this module's own scope only when their statement runs.
                const auto* var = static_cast<const VarDeclStmt*>(statement.get());
                if (!var->name || exports.count(*var->name)) break;
                Symbol symbol;
                symbol.kind = var->isConst ? SymbolKind::Constant : SymbolKind::Variable;
                symbol.name = *var->name;
                symbol.isConst = var->isConst;
                symbol.isGlobal = true;
                symbol.moduleId = module.id;
                symbol.line = statement->line;
                symbol.column = statement->column;
                // Lambda constants export their function type. Errors in the
                // type are reported when the declaration itself is analysed.
                const size_t diagnosticCount = diagnostics.size();
                SemanticType declared = resolveType(var->type, statement->line, statement->column);
                if (var->initializer && var->initializer->kind == ExprKind::Lambda && declared.kind != SemanticTypeKind::Function) {
                    const auto* lambda = static_cast<const LambdaExpr*>(var->initializer.get());
                    std::vector<SemanticType> params;
                    for (const auto& p : lambda->params) params.push_back(resolveType(p.type, statement->line, statement->column));
                    declared = SemanticType::functionOf(params, declared);
                }
                diagnostics.resize(diagnosticCount);
                symbol.type = declared;
                exports.emplace(symbol.name, symbol);
                break;
            }
            default: break;
        }
        if (name.empty()) continue;
        if (const Symbol* symbol = currentScope->resolveLocal(name)) {
            if (symbol->moduleId == module.id) exports.emplace(name, *symbol);
        }
    }
}

void Analyser::analyseModuleBody(const Module& module) {
    // A .dm file is a module file (like a C++ header): declarations only.
    const bool declarationsOnly = module.path.size() > 3 && module.path.compare(module.path.size() - 3, 3, ".dm") == 0;

    // Top-level statements run in order; variables are not hoisted.
    std::vector<const Stmt*> deferred;
    for (const auto& statement : module.ast) {
        if (!statement) continue;
        if (declarationsOnly) {
            switch (statement->kind) {
                case StmtKind::FuncDecl: case StmtKind::ClassDecl: case StmtKind::InterfaceDecl:
                case StmtKind::EnumDecl: case StmtKind::Import: case StmtKind::VarDecl:
                    break;
                default:
                    error(statement.get(), "A .dm module file can only contain declarations (func, class, interface, "
                                           "enum, var, const and imports); put code that runs in a .ds file");
                    continue;
            }
        }
        switch (statement->kind) {
            case StmtKind::FuncDecl:
            case StmtKind::ClassDecl:
                deferred.push_back(statement.get());
                break;
            case StmtKind::Import:
            case StmtKind::InterfaceDecl:
                break;
            case StmtKind::EnumDecl: {
                const auto* decl = static_cast<const EnumDeclStmt*>(statement.get());
                for (const auto& value : decl->values) {
                    if (!value.value) continue;
                    SemanticType t = analyseExpression(value.value->get());
                    if (!t.isInteger() && !t.isUnknown()) error(value.value->get(), "Enum value must be an Int, got " + t.toString());
                }
                break;
            }
            default:
                analyseStatement(statement.get());
                break;
        }
    }

    // Function and method bodies see every global of the module.
    for (const Stmt* statement : deferred) {
        if (statement->kind == StmtKind::FuncDecl) {
            analyseFunctionBody(static_cast<const FuncDeclStmt*>(statement));
        } else {
            analyseClassBody(static_cast<const ClassDeclStmt*>(statement));
        }
    }
}

// ============================================================
// Scopes
// ============================================================

void Analyser::beginScope(ScopeKind kind) {
    currentScope = new Scope(kind, currentScope);
}

void Analyser::endScope() {
    Scope* parent = currentScope->parent();
    delete currentScope;
    currentScope = parent;
}

bool Analyser::declareSymbol(Symbol symbol, int line, int column) {
    if (symbol.line == 0) { symbol.line = line; symbol.column = column; }
    if (symbol.moduleId == 0 && currentModule != 0 && symbol.kind != SymbolKind::Builtin) symbol.moduleId = currentModule;
    if (!currentScope->declare(symbol)) {
        const Symbol* existing = currentScope->resolveLocal(symbol.name);
        std::string where = existing && existing->line > 0 ? " (first declared on line " + std::to_string(existing->line) + ")" : "";
        error(line, column, "Duplicate declaration '" + symbol.name + "'" + where);
        return false;
    }
    return true;
}

Symbol* Analyser::resolveSymbol(const std::string& name) {
    return currentScope ? currentScope->resolve(name) : nullptr;
}

void Analyser::narrowAfterAssignment(const std::string& name, const SemanticType& valueType) {
    Scope* owner = currentScope->owner(name);
    if (!owner) return;
    const Symbol* symbol = owner->resolveLocal(name);
    if (!symbol) return;
    if (!symbol->type.containsNil()) {
        // A new value replaces any `is` narrowing
        for (Scope* s = currentScope; s && s != owner->parent(); s = s->parent()) {
            if (s->narrowed(name)) s->unnarrow(name);
        }
        return;
    }

    if (!valueType.containsNil() && !valueType.isUnknown()) {
        currentScope->narrow(name, valueType);
    } else {
        // Forget narrowings all the way up to the declaration so that code
        // after an enclosing block doesn't assume a value is present.
        for (Scope* s = currentScope; s && s != owner->parent(); s = s->parent()) s->unnarrow(name);
    }
}

// ============================================================
// Types & compatibility
// ============================================================

SemanticType Analyser::resolveType(const TypeNode& typeNode, int line, int column) {
    if (!typeNode.group.empty()) {
        // (Int | Str)<>, Int<><>: a grouped or nested base type
        SemanticType base = resolveType(typeNode.group.front(), line, column);
        if (typeNode.isList || typeNode.isSet) {
            if (base.isVoidLike() || base.is(SemanticTypeKind::Nil)) error(line, column, "Cannot make a collection of " + base.toString());
            base = typeNode.isList ? SemanticType::listOf(base) : SemanticType::setOf(base);
        }
        if (typeNode.unionMembers.empty()) return base;
        std::vector<SemanticType> members{base};
        for (const auto& member : typeNode.unionMembers) members.push_back(resolveType(member, line, column));
        return SemanticType::unionOf(std::move(members));
    }

    if (typeNode.isMap && typeNode.mapTypes.size() == 2) {
        // Map[K, V]: keys must be hashable values
        SemanticType key = resolveType(typeNode.mapTypes[0], line, column);
        SemanticType value = resolveType(typeNode.mapTypes[1], line, column);
        bool hashable = key.isUnknown() || key.isNumeric() || key.is(SemanticTypeKind::Str) ||
                        key.is(SemanticTypeKind::Chr) || key.is(SemanticTypeKind::Bul) ||
                        key.isEnum() || key.isReference();
        if (!hashable) {
            error(line, column, "Map keys must be Str, Int, Flt, Dbl, Chr, Bul, an enum or a class, not " + key.toString());
        }
        if (value.isVoidLike()) error(line, column, "Map values cannot be Emp");
        SemanticType map = SemanticType::mapOf(key, value);
        if (typeNode.isList) map = SemanticType::listOf(map);
        if (typeNode.isSet) {
            error(line, column, "Maps can't be stored in a set; use a list");
            map = SemanticType::setOf(map);
        }
        if (typeNode.unionMembers.empty()) return map;
        std::vector<SemanticType> members{map};
        for (const auto& member : typeNode.unionMembers) members.push_back(resolveType(member, line, column));
        return SemanticType::unionOf(std::move(members));
    }

    if (typeNode.isFunction) {
        // Func[Ret, [Params]]
        SemanticType ret = typeNode.funcReturn.empty() ? SemanticType::simple(SemanticTypeKind::Emp)
                                                       : resolveType(typeNode.funcReturn.front(), line, column);
        std::vector<SemanticType> params;
        for (const auto& p : typeNode.funcParams) {
            SemanticType t = resolveType(p, line, column);
            if (t.isVoidLike()) error(line, column, "A Func parameter type cannot be Emp");
            params.push_back(t);
        }
        SemanticType fn = SemanticType::functionOf(std::move(params), ret);
        if (typeNode.isList) fn = SemanticType::listOf(fn);
        if (typeNode.isSet) {
            error(line, column, "Functions can't be stored in a set (they can't be compared); use a list");
            fn = SemanticType::setOf(fn);
        }
        if (typeNode.unionMembers.empty()) return fn;
        std::vector<SemanticType> members{fn};
        for (const auto& member : typeNode.unionMembers) members.push_back(resolveType(member, line, column));
        return SemanticType::unionOf(std::move(members));
    }

    SemanticType base = resolveTypeName(typeNode.name, line, column);

    if (typeNode.radix) {
        int r = *typeNode.radix;
        if (base.kind != SemanticTypeKind::Int) {
            error(line, column, "An integer base can only be applied to Int, not '" + base.toString() + "'");
        } else if (r != 2 && r != 8 && r != 10 && r != 16) {
            error(line, column, "Integer base must be 2, 8, 10 or 16, got " + std::to_string(r));
        } else {
            base.radix = r;
        }
    }

    if (typeNode.isList || typeNode.isSet) {
        if (base.isVoidLike() || base.is(SemanticTypeKind::Nil)) {
            error(line, column, "Cannot make a collection of " + base.toString());
        }
        base = typeNode.isList ? SemanticType::listOf(base) : SemanticType::setOf(base);
    }

    if (!typeNode.unionMembers.empty()) {
        std::vector<SemanticType> members{base};
        for (const auto& member : typeNode.unionMembers) members.push_back(resolveType(member, line, column));
        for (const auto& m : members) {
            if (m.isVoidLike()) error(line, column, "Emp cannot be part of a union type");
        }
        base = SemanticType::unionOf(std::move(members));
    }

    return base;
}

SemanticType Analyser::resolveTypeName(const std::string& name, int line, int column) {
    if (name == "Str") return SemanticType::simple(SemanticTypeKind::Str);
    if (name == "Int") return SemanticType::simple(SemanticTypeKind::Int);
    if (name == "Bul") return SemanticType::simple(SemanticTypeKind::Bul);
    if (name == "Flt") return SemanticType::simple(SemanticTypeKind::Flt);
    if (name == "Dbl") return SemanticType::simple(SemanticTypeKind::Dbl);
    if (name == "Chr") return SemanticType::simple(SemanticTypeKind::Chr);
    if (name == "Emp") return SemanticType::simple(SemanticTypeKind::Emp);
    if (name == "Nil") return SemanticType::simple(SemanticTypeKind::Nil);
    if (name == "Map" && !resolveSymbol(name)) {
        error(line, column, "Map needs its key and value types, e.g. Map[Str, Int]");
        return SemanticType::unknown();
    }
    if (name == "Func" && !resolveSymbol(name)) {
        error(line, column, "Func needs its return and parameter types, e.g. Func[Int, [Int, Int]] or Func[Emp]");
        return SemanticType::unknown();
    }

    if (const Symbol* symbol = resolveSymbol(name)) {
        if (symbol->kind == SymbolKind::Class || symbol->kind == SymbolKind::Interface || symbol->kind == SymbolKind::Enum) {
            return symbol->type;
        }
        error(line, column, "'" + name + "' is not a type");
        return SemanticType::unknown();
    }

    error(line, column, "Unknown type '" + name + "'");
    return SemanticType::unknown();
}

bool Analyser::isSubclassOf(const SemanticType& derived, const SemanticType& base) const {
    const ClassInfo* klass = findClass(derived);
    for (int guard = 0; klass && guard < 64; ++guard) {
        if (klass->type() == base) return true;
        klass = klass->parent ? findClass(*klass->parent) : nullptr;
    }
    return false;
}

bool Analyser::implementsInterface(const SemanticType& klassType, const SemanticType& iface) const {
    const ClassInfo* klass = findClass(klassType);
    for (int guard = 0; klass && guard < 64; ++guard) {
        if (klass->interface && *klass->interface == iface) return true;
        klass = klass->parent ? findClass(*klass->parent) : nullptr;
    }
    return false;
}

bool Analyser::isAssignable(const SemanticType& target, const SemanticType& value) const {
    if (target.isUnknown() || value.isUnknown()) return true;
    if (target == value) return true;

    if (value.kind == SemanticTypeKind::Union) {
        return std::all_of(value.unionMembers.begin(), value.unionMembers.end(), [&](const SemanticType& member) {
            return isAssignable(target, member);
        });
    }

    if (target.kind == SemanticTypeKind::Union) {
        return std::any_of(target.unionMembers.begin(), target.unionMembers.end(), [&](const SemanticType& member) {
            return isAssignable(member, value);
        });
    }

    if ((target.kind == SemanticTypeKind::List || target.kind == SemanticTypeKind::Set) && target.kind == value.kind) {
        // Collections are invariant, except that an empty literal (element
        // Unknown) fits any collection.
        if (!value.elementType || value.elementType->isUnknown()) return true;
        if (!target.elementType) return true;
        return *target.elementType == *value.elementType;
    }

    if (target.kind == SemanticTypeKind::Map && value.kind == SemanticTypeKind::Map) {
        // Invariant like lists; an empty literal ({:}) fits any map.
        auto fits = [](const std::shared_ptr<SemanticType>& t, const std::shared_ptr<SemanticType>& v) {
            return !t || !v || v->isUnknown() || *t == *v;
        };
        return fits(target.keyType, value.keyType) && fits(target.elementType, value.elementType);
    }

    if (target.isNumeric() && value.isNumeric()) {
        if (target.kind == SemanticTypeKind::Int) return value.kind == SemanticTypeKind::Int;
        if (target.kind == SemanticTypeKind::Flt) return value.kind == SemanticTypeKind::Int || value.kind == SemanticTypeKind::Flt;
        if (target.kind == SemanticTypeKind::Dbl) return true;
    }

    if (target.isClass() && value.isClass()) return isSubclassOf(value, target);
    if (target.isInterface() && value.isClass()) return implementsInterface(value, target);

    if (target.kind == SemanticTypeKind::Function && value.kind == SemanticTypeKind::Function) {
        if (target.parameterTypes.size() != value.parameterTypes.size()) return false;
        for (size_t i = 0; i < target.parameterTypes.size(); ++i) {
            if (target.parameterTypes[i] != value.parameterTypes[i]) return false;
        }
        if (!target.returnType || !value.returnType) return true;
        return target.returnType->isVoidLike() || isAssignable(*target.returnType, *value.returnType);
    }

    return false;
}

bool Analyser::isComparable(const SemanticType& left, const SemanticType& right) const {
    if (left.isUnknown() || right.isUnknown()) return true;
    if (left.isNumeric() && right.isNumeric()) return true;
    return isAssignable(left, right) || isAssignable(right, left);
}

SemanticType Analyser::commonType(const SemanticType& a, const SemanticType& b) const {
    if (a.isUnknown()) return b;
    if (b.isUnknown()) return a;
    if (isAssignable(a, b)) return a;
    if (isAssignable(b, a)) return b;
    return SemanticType::unionOf({a, b});
}

namespace {

// 9.23 / -9.23 / (9.23): an unsuffixed decimal literal, which may initialise a Flt.
} // namespace

// `a / b` with two Int operands (possibly in parentheses).
bool Analyser::isIntDivision(const Expr* e) const {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    if (!e || e->kind != ExprKind::Binary) return false;
    const auto* b = static_cast<const BinaryExpr*>(e);
    if (b->op.type != TokenType::DIV) return false;
    const SemanticType* l = typeOf(b->left.get());
    const SemanticType* r = typeOf(b->right.get());
    return l && r && l->isInteger() && r->isInteger();
}

namespace {

bool isPlainDecimalLiteral(const Expr* e) {
    while (e && e->kind == ExprKind::Grouping) e = static_cast<const GroupingExpr*>(e)->inner.get();
    if (e && e->kind == ExprKind::Unary) {
        const auto* u = static_cast<const UnaryExpr*>(e);
        if (u->op.type == TokenType::MINUS) e = u->operand.get();
    }
    if (!e || e->kind != ExprKind::Literal) return false;
    const Token& t = static_cast<const LiteralExpr*>(e)->token;
    return t.type == TokenType::DOUBLE_LITERAL && !t.lexeme.empty() && t.lexeme.back() != 'd';
}

} // namespace

bool Analyser::checkConversion(const SemanticType& target, const Expr* value, const SemanticType& valueType, const std::string& context) {
    if (value) expectedTypes[value] = target;
    if (isAssignable(target, valueType)) return true;

    // var[Int] half = 7 / 2;  -> 3 (an Int / Int division is truncated when
    // stored into an Int, and may be stored into a Flt)
    if (isWholeNumberMath(value) && (isAssignable(target, SemanticType::simple(SemanticTypeKind::Int)) ||
                                 isAssignable(target, SemanticType::simple(SemanticTypeKind::Flt)))) {
        return true;
    }

    // Literal conversions
    if (isPlainDecimalLiteral(value)) {
        SemanticType flt = SemanticType::simple(SemanticTypeKind::Flt);
        if (isAssignable(target, flt)) return true;
    }
    if (value && (value->kind == ExprKind::List || value->kind == ExprKind::Set) &&
        valueType.elementType && (target.kind == valueType.kind || target.kind == SemanticTypeKind::Union)) {
        // [1, 2] into Dbl<>: check each element against the target element.
        const SemanticType* targetCollection = &target;
        if (target.kind == SemanticTypeKind::Union) {
            targetCollection = nullptr;
            for (const auto& m : target.unionMembers) if (m.kind == valueType.kind) { targetCollection = &m; break; }
        }
        if (targetCollection && targetCollection->elementType) {
            const auto& elements = value->kind == ExprKind::List
                ? static_cast<const ListExpr*>(value)->elements
                : static_cast<const SetExpr*>(value)->elements;
            bool ok = true;
            for (const auto& el : elements) {
                const SemanticType* t = typeOf(el.value.get());
                SemanticType elType = t ? *t : SemanticType::unknown();
                if (el.isSpread) {
                    if (!isAssignable(*targetCollection, elType)) ok = false;
                    continue;
                }
                if (!isAssignable(*targetCollection->elementType, elType) &&
                    !(isPlainDecimalLiteral(el.value.get()) && isAssignable(*targetCollection->elementType, SemanticType::simple(SemanticTypeKind::Flt)))) {
                    ok = false;
                }
                expectedTypes[el.value.get()] = *targetCollection->elementType;
            }
            if (ok) return true;
        }
    }

    // Map literals: an empty `{}` set literal, or entries that each convert
    // ({"a": 1} into Map[Str, Dbl]).
    const SemanticType* targetMap = target.kind == SemanticTypeKind::Map ? &target : nullptr;
    if (!targetMap && target.kind == SemanticTypeKind::Union) {
        for (const auto& m : target.unionMembers) if (m.kind == SemanticTypeKind::Map) targetMap = &m;
    }
    if (targetMap && value && value->kind == ExprKind::Set && static_cast<const SetExpr*>(value)->elements.empty()) {
        return true; // {} where a map is expected
    }
    if (targetMap && value && value->kind == ExprKind::Map && targetMap->keyType && targetMap->elementType) {
        bool ok = true;
        for (const auto& entry : static_cast<const MapExpr*>(value)->entries) {
            const SemanticType* v = typeOf(entry.value.get());
            SemanticType vt = v ? *v : SemanticType::unknown();
            if (!entry.key) {
                if (!isAssignable(*targetMap, vt)) ok = false;
                continue;
            }
            const SemanticType* k = typeOf(entry.key.get());
            if (k && !isAssignable(*targetMap->keyType, *k)) ok = false;
            if (!isAssignable(*targetMap->elementType, vt) &&
                !(isPlainDecimalLiteral(entry.value.get()) && isAssignable(*targetMap->elementType, SemanticType::simple(SemanticTypeKind::Flt)))) {
                ok = false;
            }
            expectedTypes[entry.key.get()] = *targetMap->keyType;
            expectedTypes[entry.value.get()] = *targetMap->elementType;
        }
        if (ok) return true;
    }

    error(value, "Cannot convert " + valueType.toString() + " to " + target.toString() + " " + context);
    return false;
}

// ============================================================
// Statements
// ============================================================

void Analyser::analyseStatement(const Stmt* statement) {
    if (!statement) return;

    switch (statement->kind) {
        case StmtKind::Expression: {
            const auto* expr = static_cast<const ExpressionStmt*>(statement)->expression.get();
            analyseExpression(expr);
            break;
        }
        case StmtKind::VarDecl: analyseVariableDeclaration(static_cast<const VarDeclStmt*>(statement)); break;
        case StmtKind::FuncDecl:
            error(statement, "Functions can only be declared at the top level of a file or inside a class; use a lambda instead");
            break;
        case StmtKind::ClassDecl:
        case StmtKind::InterfaceDecl:
        case StmtKind::EnumDecl:
            error(statement, "Classes, interfaces and enums can only be declared at the top level of a file");
            break;
        case StmtKind::Import:
            error(statement, "Imports are only allowed at the top level of a file");
            break;
        case StmtKind::If: analyseIf(static_cast<const IfStmt*>(statement)); break;
        case StmtKind::Switch: analyseSwitch(static_cast<const SwitchStmt*>(statement)); break;
        case StmtKind::For: analyseFor(static_cast<const ForStmt*>(statement)); break;
        case StmtKind::ForIn: analyseForIn(static_cast<const ForInStmt*>(statement)); break;
        case StmtKind::While: analyseWhile(static_cast<const WhileStmt*>(statement)); break;
        case StmtKind::Loop: analyseLoop(static_cast<const LoopStmt*>(statement)); break;
        case StmtKind::Break:
            if (loopDepth <= 0) error(statement, "'break' can only be used inside a loop");
            break;
        case StmtKind::Continue:
            if (loopDepth <= 0) error(statement, "'continue' can only be used inside a loop");
            break;
        case StmtKind::Return: analyseReturn(static_cast<const ReturnStmt*>(statement)); break;
        case StmtKind::Block: analyseBlock(static_cast<const BlockStmt*>(statement)); break;
        case StmtKind::Try: analyseTry(static_cast<const TryStmt*>(statement)); break;
        case StmtKind::Throw: analyseThrow(static_cast<const ThrowStmt*>(statement)); break;
    }
}

void Analyser::analyseBlock(const BlockStmt* statement, ScopeKind kind) {
    beginScope(kind);
    for (const auto& child : statement->statements) analyseStatement(child.get());
    endScope();
}

void Analyser::analyseBranch(const Stmt* statement, const Narrowings& narrowings) {
    beginScope(ScopeKind::Block);
    for (const auto& [name, type] : narrowings) currentScope->narrow(name, type);
    analyseStatement(statement);
    endScope();
}

void Analyser::analyseVariableDeclaration(const VarDeclStmt* statement) {
    if (!statement->name) return;
    const std::string& name = *statement->name;

    SemanticType declared = resolveType(statement->type, statement->line, statement->column);
    const bool lambdaInit = statement->initializer && statement->initializer->kind == ExprKind::Lambda;
    if (declared.isVoidLike() && !lambdaInit) {
        error(statement, "Variable '" + name + "' cannot have type Emp");
        declared = SemanticType::unknown();
    }

    SemanticType symbolType = declared;
    SemanticType valueType = SemanticType::unknown();

    // A lambda may call itself: declare its name before analysing the body.
    //     const[Int] fact = (n[Int]) := n <= 1 ? 1 : n * fact(n - 1);
    bool predeclared = false;
    const VarDeclStmt* previousDefining = definingLambda;
    if (lambdaInit) {
        const auto* lambda = static_cast<const LambdaExpr*>(statement->initializer.get());
        SemanticType fnType = declared;
        if (withoutNil(declared).kind != SemanticTypeKind::Function) {
            std::vector<SemanticType> params;
            for (const auto& p : lambda->params) params.push_back(resolveType(p.type, statement->line, statement->column));
            fnType = SemanticType::functionOf(params, declared);
        }
        Symbol early;
        early.kind = statement->isConst ? SymbolKind::Constant : SymbolKind::Variable;
        early.name = name;
        early.type = fnType;
        early.isConst = statement->isConst;
        early.isGlobal = currentScope->kind() == ScopeKind::Module;
        early.moduleId = currentModule;
        if (!declareSymbol(early, statement->line, statement->column)) return;
        predeclared = true;
        definingLambda = statement;
    }

    if (statement->initializer) {
        const Expr* init = statement->initializer.get();
        if (init->kind == ExprKind::Lambda && declared.kind != SemanticTypeKind::Function) {
            // const[Int] square = (x[Int]) := x ^ 2;  -> [Int] is the return type
            valueType = analyseLambda(static_cast<const LambdaExpr*>(init), declared);
            symbolType = valueType;
            expectedTypes[init] = valueType;
        } else if (init->kind == ExprKind::Lambda && withoutNil(declared).kind == SemanticTypeKind::Function &&
                   withoutNil(declared).returnType) {
            // var[Func[Int, [Int]]] f = (x[Int]) := x * 2;  -> check the body against Int
            valueType = analyseLambda(static_cast<const LambdaExpr*>(init), *withoutNil(declared).returnType);
            checkConversion(declared, init, valueType, "in declaration of '" + name + "'");
        } else {
            valueType = analyseExpression(init);
            if (valueType.isVoidLike()) {
                error(init, "Expression has no value (Emp) and cannot initialise '" + name + "'");
            } else {
                checkConversion(declared, init, valueType, "in declaration of '" + name + "'");
            }
        }
    } else if (statement->isConst) {
        error(statement, "Constant '" + name + "' must be initialised");
    } else if (!declared.containsNil() && (declared.isReference() || declared.isEnum() || declared.kind == SemanticTypeKind::Function)) {
        error(statement, "Variable '" + name + "' of type " + declared.toString() +
              " must be initialised (or declared as " + declared.toString() + " | Nil)");
    }

    declTypes[statement] = symbolType;
    definingLambda = previousDefining;

    if (!predeclared) {
        Symbol symbol;
        symbol.kind = statement->isConst ? SymbolKind::Constant : SymbolKind::Variable;
        symbol.name = name;
        symbol.type = symbolType;
        symbol.isConst = statement->isConst;
        symbol.isGlobal = currentScope->kind() == ScopeKind::Module;
        symbol.moduleId = currentModule;
        if (!declareSymbol(symbol, statement->line, statement->column)) return;
    } else if (Symbol* existing = currentScope->resolveLocal(name)) {
        existing->type = symbolType;
    }

    if (statement->initializer) narrowAfterAssignment(name, valueType);
}

void Analyser::analyseFunctionBody(const FuncDeclStmt* statement) {
    const Symbol* signature = functionOf(statement);
    if (!signature || !statement->body) return;

    const auto previousFunction = currentFunctionName;
    const auto previousReturn = currentReturnType;
    const bool previousInferring = inferringReturn;
    const int previousLoopDepth = loopDepth;
    const bool previousInConstructor = inConstructor;
    currentFunctionName = statement->isConstructor ? "init" : statement->name;
    currentReturnType = signature->returnType;
    inferringReturn = false;
    loopDepth = 0;
    inConstructor = statement->isConstructor;
    functionDepth++;

    beginScope(ScopeKind::Function);
    for (size_t i = 0; i < statement->params.size(); ++i) {
        Symbol parameter;
        parameter.kind = SymbolKind::Parameter;
        parameter.name = statement->params[i].name;
        parameter.type = signature->parameters[i].type;
        parameter.moduleId = currentModule;
        declareSymbol(parameter, statement->line, statement->column);
    }

    const auto* block = static_cast<const BlockStmt*>(statement->body.get());

    if (statement->isConstructor) {
        // super.init(...) may only appear as the first statement.
        const ClassInfo* klass = currentClass();
        bool parentNeedsArgs = false;
        if (klass && klass->parent) {
            for (const ClassInfo* p = findClass(*klass->parent); p; p = p->parent ? findClass(*p->parent) : nullptr) {
                if (p->constructor) { parentNeedsArgs = !p->constructor->parameters.empty(); break; }
            }
        }
        auto isSuperInit = [](const Stmt* s) {
            if (!s || s->kind != StmtKind::Expression) return false;
            const Expr* e = static_cast<const ExpressionStmt*>(s)->expression.get();
            if (!e || e->kind != ExprKind::Call) return false;
            const Expr* callee = static_cast<const CallExpr*>(e)->callee.get();
            if (!callee || callee->kind != ExprKind::Member) return false;
            const auto* m = static_cast<const MemberExpr*>(callee);
            return m->name.type == TokenType::INIT && m->object->kind == ExprKind::Identifier &&
                   static_cast<const IdentifierExpr*>(m->object.get())->name.type == TokenType::SUPER;
        };
        bool firstIsSuper = !block->statements.empty() && isSuperInit(block->statements.front().get());
        if (parentNeedsArgs && !firstIsSuper) {
            error(statement, "init must start with super.init(...) because the parent's init takes arguments");
        }
        for (size_t i = 1; i < block->statements.size(); ++i) {
            if (isSuperInit(block->statements[i].get())) {
                error(block->statements[i].get(), "super.init(...) must be the first statement of init");
            }
        }
    }

    for (const auto& child : block->statements) analyseStatement(child.get());

    if (!signature->returnType.isVoidLike() && !signature->returnType.isUnknown() &&
        !blockDefinitelyExits(block, false) && !isStubBody(block)) {
        error(statement, "Function '" + statement->name + "' may reach the end without returning " + signature->returnType.toString());
    }

    endScope();
    functionDepth--;
    currentFunctionName = previousFunction;
    currentReturnType = previousReturn;
    inferringReturn = previousInferring;
    loopDepth = previousLoopDepth;
    inConstructor = previousInConstructor;
}

void Analyser::analyseClassBody(const ClassDeclStmt* statement) {
    auto it = classes.find(SemanticType::userDefined(statement->name, currentModule, UserTypeKind::Class).key());
    if (it == classes.end() || it->second.decl != statement) return;
    const ClassInfo& info = it->second;

    const std::string previousClass = currentClassKey;
    currentClassKey = info.type().key();

    // Class scope: own and inherited members, nearest declaration wins.
    beginScope(ScopeKind::Class);
    std::vector<const ClassInfo*> chain;
    for (const ClassInfo* k = &info; k && chain.size() < 64; k = k->parent ? findClass(*k->parent) : nullptr) chain.push_back(k);
    for (const ClassInfo* k : chain) {
        for (const auto& [name, symbol] : k->fields) currentScope->declare(symbol);
        for (const auto& [name, symbol] : k->methods) currentScope->declare(symbol);
    }

    for (const auto& member : statement->members) {
        if (!member) continue;
        if (member->kind == StmtKind::VarDecl) {
            const auto* field = static_cast<const VarDeclStmt*>(member.get());
            if (!field->name) continue;
            const SemanticType& target = info.fields.at(*field->name).type;
            if (field->initializer) {
                SemanticType value = analyseExpression(field->initializer.get());
                checkConversion(target, field->initializer.get(), value, "in initialiser of field '" + *field->name + "'");
            } else if (field->isConst) {
                error(member.get(), "Constant field '" + *field->name + "' must be initialised");
            }
        } else if (member->kind == StmtKind::FuncDecl) {
            analyseFunctionBody(static_cast<const FuncDeclStmt*>(member.get()));
        }
    }

    endScope();
    currentClassKey = previousClass;
}

SemanticType Analyser::analyseCondition(const Expr* condition, const std::string& what) {
    SemanticType type = analyseExpression(condition);
    if (!type.isBoolean() && !type.isUnknown()) {
        error(condition, what + " must be Bul, got " + type.toString());
    }
    return type;
}

void Analyser::analyseIf(const IfStmt* statement) {
    analyseCondition(statement->condition.get(), "If condition");
    Narrowings whenTrue, whenFalse;
    conditionNarrowings(statement->condition.get(), whenTrue, whenFalse);

    analyseBranch(statement->thenBranch.get(), whenTrue);
    if (statement->elseBranch) analyseBranch(statement->elseBranch.get(), whenFalse);

    // if (x == nil) { return; }  -> x is non-nil afterwards
    bool thenExits = statementDefinitelyExits(statement->thenBranch.get());
    bool elseExits = statement->elseBranch && statementDefinitelyExits(statement->elseBranch.get());
    if (thenExits && !elseExits) {
        for (const auto& [name, type] : whenFalse) currentScope->narrow(name, type);
    } else if (elseExits && !thenExits) {
        for (const auto& [name, type] : whenTrue) currentScope->narrow(name, type);
    }
}

void Analyser::analyseSwitch(const SwitchStmt* statement) {
    SemanticType discriminant = analyseExpression(statement->discriminant.get());
    if (!discriminant.isUnknown() && !(discriminant.isNumeric() || discriminant.is(SemanticTypeKind::Str) ||
          discriminant.is(SemanticTypeKind::Chr) || discriminant.is(SemanticTypeKind::Bul) || discriminant.isEnum())) {
        error(statement->discriminant.get(), "Cannot switch on a value of type " + discriminant.toString());
    }

    bool seenDefault = false;
    for (const auto& arm : statement->cases) {
        if (!arm.value) {
            if (seenDefault) error(arm.body.get(), "Switch has more than one default");
            seenDefault = true;
        } else {
            if (seenDefault) error(arm.value.get(), "'case' after 'default' is never reached");
            // Like Java: switching on an enum, `case RED` means `case Color.RED`.
            if (discriminant.isEnum() && arm.value->kind == ExprKind::Identifier) {
                const auto* id = static_cast<const IdentifierExpr*>(arm.value.get());
                const EnumInfo* info = findEnum(discriminant);
                if (info && std::find(info->values.begin(), info->values.end(), id->name.lexeme) != info->values.end()) {
                    enumCases[arm.value.get()] = discriminant;
                    exprTypes[arm.value.get()] = discriminant;
                    if (arm.body) analyseStatement(arm.body.get());
                    continue;
                }
            }
            SemanticType caseType = analyseExpression(arm.value.get());
            if (!isComparable(discriminant, caseType)) {
                error(arm.value.get(), "Case value of type " + caseType.toString() + " doesn't match switch type " + discriminant.toString());
            }
        }
        if (arm.body) analyseStatement(arm.body.get());
    }
}

void Analyser::analyseLoopBody(const Stmt* body, const Narrowings& narrowings) {
    ++loopDepth;
    analyseBranch(body, narrowings);
    --loopDepth;
}

namespace {

// Names given a new value with `=` anywhere inside a statement/expression.
void assignedNames(const Expr* e, std::unordered_set<std::string>& names);

void assignedNames(const Stmt* s, std::unordered_set<std::string>& names) {
    if (!s) return;
    switch (s->kind) {
        case StmtKind::Expression: assignedNames(static_cast<const ExpressionStmt*>(s)->expression.get(), names); break;
        case StmtKind::VarDecl: assignedNames(static_cast<const VarDeclStmt*>(s)->initializer.get(), names); break;
        case StmtKind::Block:
            for (const auto& c : static_cast<const BlockStmt*>(s)->statements) assignedNames(c.get(), names);
            break;
        case StmtKind::If: {
            const auto* i = static_cast<const IfStmt*>(s);
            assignedNames(i->condition.get(), names);
            assignedNames(i->thenBranch.get(), names);
            assignedNames(i->elseBranch.get(), names);
            break;
        }
        case StmtKind::Switch: {
            const auto* sw = static_cast<const SwitchStmt*>(s);
            assignedNames(sw->discriminant.get(), names);
            for (const auto& arm : sw->cases) assignedNames(arm.body.get(), names);
            break;
        }
        case StmtKind::For: {
            const auto* f = static_cast<const ForStmt*>(s);
            assignedNames(f->init.get(), names);
            assignedNames(f->condition.get(), names);
            assignedNames(f->increment.get(), names);
            assignedNames(f->body.get(), names);
            break;
        }
        case StmtKind::ForIn: {
            const auto* f = static_cast<const ForInStmt*>(s);
            assignedNames(f->iterable.get(), names);
            assignedNames(f->body.get(), names);
            break;
        }
        case StmtKind::While: {
            const auto* w = static_cast<const WhileStmt*>(s);
            assignedNames(w->condition.get(), names);
            assignedNames(w->body.get(), names);
            break;
        }
        case StmtKind::Loop: assignedNames(static_cast<const LoopStmt*>(s)->body.get(), names); break;
        case StmtKind::Return: assignedNames(static_cast<const ReturnStmt*>(s)->value.get(), names); break;
        case StmtKind::Throw: assignedNames(static_cast<const ThrowStmt*>(s)->value.get(), names); break;
        case StmtKind::Try: {
            const auto* t = static_cast<const TryStmt*>(s);
            assignedNames(t->tryBlock.get(), names);
            for (const auto& c : t->catches) assignedNames(c.body.get(), names);
            break;
        }
        default: break;
    }
}

void assignedNames(const Expr* e, std::unordered_set<std::string>& names) {
    if (!e) return;
    switch (e->kind) {
        case ExprKind::Assign: {
            const auto* a = static_cast<const AssignExpr*>(e);
            if (a->op.type == TokenType::EQUAL && a->target->kind == ExprKind::Identifier) {
                names.insert(static_cast<const IdentifierExpr*>(a->target.get())->name.lexeme);
            }
            assignedNames(a->target.get(), names);
            assignedNames(a->value.get(), names);
            break;
        }
        case ExprKind::Unary: assignedNames(static_cast<const UnaryExpr*>(e)->operand.get(), names); break;
        case ExprKind::Postfix: assignedNames(static_cast<const PostfixExpr*>(e)->operand.get(), names); break;
        case ExprKind::Binary: {
            const auto* b = static_cast<const BinaryExpr*>(e);
            assignedNames(b->left.get(), names);
            assignedNames(b->right.get(), names);
            break;
        }
        case ExprKind::Ternary: {
            const auto* t = static_cast<const TernaryExpr*>(e);
            assignedNames(t->condition.get(), names);
            assignedNames(t->thenBranch.get(), names);
            assignedNames(t->elseBranch.get(), names);
            break;
        }
        case ExprKind::Call: {
            const auto* c = static_cast<const CallExpr*>(e);
            assignedNames(c->callee.get(), names);
            for (const auto& a : c->args) assignedNames(a.value.get(), names);
            break;
        }
        case ExprKind::New:
            for (const auto& a : static_cast<const NewExpr*>(e)->args) assignedNames(a.value.get(), names);
            break;
        case ExprKind::Index: {
            const auto* i = static_cast<const IndexExpr*>(e);
            assignedNames(i->object.get(), names);
            assignedNames(i->index.get(), names);
            break;
        }
        case ExprKind::Member: assignedNames(static_cast<const MemberExpr*>(e)->object.get(), names); break;
        case ExprKind::Lambda: assignedNames(static_cast<const LambdaExpr*>(e)->body.get(), names); break;
        case ExprKind::Grouping: assignedNames(static_cast<const GroupingExpr*>(e)->inner.get(), names); break;
        case ExprKind::Is: assignedNames(static_cast<const IsExpr*>(e)->value.get(), names); break;
        case ExprKind::Convert: assignedNames(static_cast<const ConvertExpr*>(e)->value.get(), names); break;
        case ExprKind::Map:
            for (const auto& entry : static_cast<const MapExpr*>(e)->entries) {
                assignedNames(entry.key.get(), names);
                assignedNames(entry.value.get(), names);
            }
            break;
        case ExprKind::TemplateString:
            for (const auto& i : static_cast<const TemplateStringExpr*>(e)->interpolations) assignedNames(i.get(), names);
            break;
        case ExprKind::List:
            for (const auto& el : static_cast<const ListExpr*>(e)->elements) assignedNames(el.value.get(), names);
            break;
        case ExprKind::Set:
            for (const auto& el : static_cast<const SetExpr*>(e)->elements) assignedNames(el.value.get(), names);
            break;
        default: break;
    }
}

} // namespace

// A loop's body runs again after its last statement, so a nullable variable
// assigned anywhere in the loop can't be assumed non-nil at the top of it:
//     var[Int | Nil] x = 1;
//     while (true) { print(x + 1); x = nil; }   // error on the second pass
void Analyser::forgetNarrowings(const std::unordered_set<std::string>& names) {
    for (const auto& name : names) {
        if (currentScope->narrowed(name)) currentScope->unnarrow(name);
    }
}

void Analyser::analyseFor(const ForStmt* statement) {
    beginScope(ScopeKind::Block);
    if (statement->init) analyseStatement(statement->init.get());
    {
        std::unordered_set<std::string> names;
        assignedNames(statement->condition.get(), names);
        assignedNames(statement->increment.get(), names);
        assignedNames(statement->body.get(), names);
        forgetNarrowings(names);
    }
    if (statement->condition) analyseCondition(statement->condition.get(), "For-loop condition");
    if (statement->increment) analyseExpression(statement->increment.get());
    analyseLoopBody(statement->body.get());
    endScope();
}

void Analyser::analyseForIn(const ForInStmt* statement) {
    SemanticType iterableType = analyseExpression(statement->iterable.get());
    SemanticType elementType = SemanticType::unknown();

    if (iterableType.kind == SemanticTypeKind::List || iterableType.kind == SemanticTypeKind::Set) {
        if (iterableType.elementType) elementType = *iterableType.elementType;
    } else if (iterableType.kind == SemanticTypeKind::Map) {
        if (iterableType.keyType) elementType = *iterableType.keyType;   // loops over the keys
    } else if (iterableType.is(SemanticTypeKind::Str)) {
        elementType = SemanticType::simple(SemanticTypeKind::Chr);
    } else if (!iterableType.isUnknown()) {
        error(statement->iterable.get(), "Value of type " + iterableType.toString() + " is not iterable");
    }

    beginScope(ScopeKind::Block);
    const auto* declaration = static_cast<const VarDeclStmt*>(statement->declaration.get());
    if (declaration && declaration->name) {
        SemanticType declared = resolveType(declaration->type, declaration->line, declaration->column);
        if (!isAssignable(declared, elementType)) {
            error(statement->declaration.get(), "Loop variable '" + *declaration->name + "' of type " + declared.toString() +
                  " cannot hold elements of type " + elementType.toString());
        }
        declTypes[declaration] = declared;
        Symbol symbol;
        symbol.kind = SymbolKind::Variable;
        symbol.name = *declaration->name;
        symbol.type = declared;
        symbol.moduleId = currentModule;
        declareSymbol(symbol, declaration->line, declaration->column);
    }
    {
        std::unordered_set<std::string> names;
        assignedNames(statement->body.get(), names);
        forgetNarrowings(names);
    }
    analyseLoopBody(statement->body.get());
    endScope();
}

void Analyser::analyseWhile(const WhileStmt* statement) {
    beginScope(ScopeKind::Block);
    std::unordered_set<std::string> names;
    assignedNames(statement->condition.get(), names);
    assignedNames(statement->body.get(), names);
    forgetNarrowings(names);

    analyseCondition(statement->condition.get(), "While-loop condition");
    Narrowings whenTrue, whenFalse;
    conditionNarrowings(statement->condition.get(), whenTrue, whenFalse);
    analyseLoopBody(statement->body.get(), whenTrue);
    endScope();
}

void Analyser::analyseLoop(const LoopStmt* statement) {
    beginScope(ScopeKind::Block);
    std::unordered_set<std::string> names;
    assignedNames(statement->body.get(), names);
    forgetNarrowings(names);
    analyseLoopBody(statement->body.get());
    endScope();
}

void Analyser::analyseReturn(const ReturnStmt* statement) {
    if (functionDepth <= 0 || !currentReturnType) {
        error(statement, "'return' can only be used inside a function");
        if (statement->value) analyseExpression(statement->value.get());
        return;
    }

    if (inferringReturn) {
        SemanticType actual = statement->value ? analyseExpression(statement->value.get()) : SemanticType::simple(SemanticTypeKind::Emp);
        inferredReturn = inferredReturn ? commonType(*inferredReturn, actual) : actual;
        return;
    }

    if (!statement->value) {
        if (!currentReturnType->isVoidLike()) {
            error(statement, "'" + currentFunctionName + "' must return a value of type " + currentReturnType->toString());
        }
        return;
    }

    SemanticType actual = analyseExpression(statement->value.get());
    if (currentReturnType->isVoidLike()) {
        error(statement->value.get(), "'" + currentFunctionName + "' has return type Emp and cannot return a value");
        return;
    }
    checkConversion(*currentReturnType, statement->value.get(), actual, "in return from '" + currentFunctionName + "'");
}

void Analyser::analyseTry(const TryStmt* statement) {
    analyseStatement(statement->tryBlock.get());

    std::vector<SemanticType> earlier; // types caught by previous arms
    bool seenCatchAll = false;
    bool seenStr = false;
    for (const auto& clause : statement->catches) {
        if (seenCatchAll) {
            error(clause.line, clause.column, "This catch is never reached: an earlier 'catch { }' already catches everything");
        }
        beginScope(ScopeKind::Block);
        if (clause.param && clause.type) {
            SemanticType type = resolveType(*clause.type, clause.line, clause.column);
            if (!type.isUnknown() && !type.is(SemanticTypeKind::Str) && !type.isReference()) {
                error(clause.line, clause.column, "catch can only catch Str errors or class instances, not " + type.toString());
            } else if (type.is(SemanticTypeKind::Str)) {
                if (seenStr) error(clause.line, clause.column, "Str errors are already caught by an earlier catch");
                seenStr = true;
            } else if (type.isReference() && !seenCatchAll) {
                for (const auto& previous : earlier) {
                    if (isAssignable(previous, type)) {
                        error(clause.line, clause.column, "This catch is never reached: " + type.toString() +
                              " is already caught by the earlier catch for " + previous.toString());
                        break;
                    }
                }
                earlier.push_back(type);
            }
            catchTypes[&clause] = type;
            Symbol symbol;
            symbol.kind = SymbolKind::Variable;
            symbol.name = *clause.param;
            symbol.type = type;
            symbol.moduleId = currentModule;
            declareSymbol(symbol, clause.line, clause.column);
        } else {
            seenCatchAll = true;
        }
        analyseStatement(clause.body.get());
        endScope();
    }
}

void Analyser::analyseThrow(const ThrowStmt* statement) {
    if (!statement->value) return;
    SemanticType thrown = analyseExpression(statement->value.get());
    if (!thrown.isUnknown() && !thrown.is(SemanticTypeKind::Str) && !thrown.isReference()) {
        error(statement->value.get(), "Only Str messages and class instances can be thrown, not " + thrown.toString());
    }
}

// ============================================================
// Control flow helpers
// ============================================================

namespace {

bool containsBreak(const Stmt* s) {
    if (!s) return false;
    switch (s->kind) {
        case StmtKind::Break: return true;
        case StmtKind::Block:
            for (const auto& c : static_cast<const BlockStmt*>(s)->statements) if (containsBreak(c.get())) return true;
            return false;
        case StmtKind::If: {
            const auto* i = static_cast<const IfStmt*>(s);
            return containsBreak(i->thenBranch.get()) || containsBreak(i->elseBranch.get());
        }
        case StmtKind::Switch:
            for (const auto& arm : static_cast<const SwitchStmt*>(s)->cases) if (containsBreak(arm.body.get())) return true;
            return false;
        case StmtKind::Try: {
            const auto* t = static_cast<const TryStmt*>(s);
            if (containsBreak(t->tryBlock.get())) return true;
            for (const auto& c : t->catches) if (containsBreak(c.body.get())) return true;
            return false;
        }
        default: return false; // nested loops own their breaks
    }
}

} // namespace

bool Analyser::statementDefinitelyExits(const Stmt* statement, bool countLoopJumps) const {
    if (!statement) return false;
    switch (statement->kind) {
        case StmtKind::Return:
        case StmtKind::Throw:
            return true;
        case StmtKind::Break:
        case StmtKind::Continue:
            return countLoopJumps;
        case StmtKind::Block:
            return blockDefinitelyExits(static_cast<const BlockStmt*>(statement), countLoopJumps);
        case StmtKind::If: {
            const auto* branch = static_cast<const IfStmt*>(statement);
            return branch->elseBranch &&
                   statementDefinitelyExits(branch->thenBranch.get(), countLoopJumps) &&
                   statementDefinitelyExits(branch->elseBranch.get(), countLoopJumps);
        }
        case StmtKind::Switch: {
            const auto* sw = static_cast<const SwitchStmt*>(statement);
            bool hasDefault = false;
            for (const auto& arm : sw->cases) {
                if (!arm.value) hasDefault = true;
                if (!statementDefinitelyExits(arm.body.get(), countLoopJumps)) return false;
            }
            return hasDefault;
        }
        case StmtKind::Try: {
            const auto* t = static_cast<const TryStmt*>(statement);
            if (!statementDefinitelyExits(t->tryBlock.get(), countLoopJumps)) return false;
            for (const auto& c : t->catches) {
                if (!statementDefinitelyExits(c.body.get(), countLoopJumps)) return false;
            }
            return true;
        }
        case StmtKind::Loop:
            // loop { } without a break never falls through
            return !containsBreak(static_cast<const LoopStmt*>(statement)->body.get());
        default:
            return false;
    }
}

bool Analyser::blockDefinitelyExits(const BlockStmt* block, bool countLoopJumps) const {
    if (!block) return false;
    for (const auto& statement : block->statements) {
        if (statementDefinitelyExits(statement.get(), countLoopJumps)) return true;
    }
    return false;
}

// A body consisting of just `...` is a stub and exempt from return checks.
bool Analyser::isStubBody(const Stmt* body) const {
    if (!body || body->kind != StmtKind::Block) return false;
    const auto* block = static_cast<const BlockStmt*>(body);
    if (block->statements.size() != 1) return false;
    const Stmt* only = block->statements.front().get();
    if (!only || only->kind != StmtKind::Expression) return false;
    const Expr* e = static_cast<const ExpressionStmt*>(only)->expression.get();
    return e && e->kind == ExprKind::Literal && static_cast<const LiteralExpr*>(e)->token.type == TokenType::ELLIPSIS;
}

bool Analyser::isAssignableExpression(const Expr* expression) const {
    if (!expression) return false;
    return expression->kind == ExprKind::Identifier || expression->kind == ExprKind::Member || expression->kind == ExprKind::Index;
}

const ClassInfo* Analyser::currentClass() const {
    if (currentClassKey.empty()) return nullptr;
    return lookup(classes, currentClassKey);
}

} // namespace dessa
