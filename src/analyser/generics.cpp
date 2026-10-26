// Generics: func[T] first:T(items[T<>]) and class Box:T { ... }
//
// A generic function or class is kept as its tokens (Module::templates). Each
// use with other types (Box:Int, Box:Str, first(<1, 2>)) parses it again with
// T filled in, under a name of its own (Box$1, Box$2, first$1), and analyses it
// like any class or function. The code generators only ever see those filled-in
// copies; their names print as Box:Int.

#include "analyser.hpp"
#include "parser.hpp"

#include <algorithm>

namespace dessa {

namespace {
// The same types give the same key, whichever module asks: Box:Int is one class
std::string canonical(const SemanticType& t) {
    switch (t.kind) {
        case SemanticTypeKind::UserDefined: return t.key();
        case SemanticTypeKind::List: return (t.elementType ? canonical(*t.elementType) : "?") + "<>";
        case SemanticTypeKind::Set: return (t.elementType ? canonical(*t.elementType) : "?") + "{}";
        case SemanticTypeKind::Map:
            return "Map[" + (t.keyType ? canonical(*t.keyType) : "?") + "," + (t.elementType ? canonical(*t.elementType) : "?") + "]";
        case SemanticTypeKind::Union: {
            std::string s = "(";
            for (const auto& m : t.unionMembers) s += canonical(m) + "|";
            return s + ")";
        }
        case SemanticTypeKind::Function: {
            std::string s = "Func[" + (t.returnType ? canonical(*t.returnType) : "?") + ",[";
            for (const auto& p : t.parameterTypes) s += canonical(p) + ",";
            return s + "]]";
        }
        default: return t.toString();
    }
}

// Box:Int, Pair:[Str, Int]
std::string shownWith(const std::string& name, const std::vector<SemanticType>& args) {
    if (args.size() == 1) {
        const std::string one = args[0].toString();
        const bool plain = args[0].kind != SemanticTypeKind::Union && args[0].kind != SemanticTypeKind::List &&
                           args[0].kind != SemanticTypeKind::Set;
        return name + ":" + (plain ? one : "[" + one + "]");
    }
    std::string s = name + ":[";
    for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : "") + args[i].toString();
    return s + "]";
}
} // namespace

// Generic functions and classes are names of the module like any other, so
// they can be imported; using one fills it in.
void Analyser::declareTemplates(const Module& module) {
    for (const auto& statement : module.templates) {
        if (!statement) continue;
        GenericTemplate t;
        t.decl = statement.get();
        t.moduleId = module.id;
        Symbol symbol;
        symbol.moduleId = module.id;
        symbol.isGlobal = true;
        symbol.line = statement->line;
        symbol.column = statement->column;
        symbol.templateId = static_cast<int>(templates.size());
        if (statement->kind == StmtKind::ClassDecl) {
            const auto* decl = static_cast<const ClassDeclStmt*>(statement.get());
            t.name = decl->name;
            t.params = decl->typeParams;
            t.tokens = &decl->templateTokens;
            t.isClass = true;
            symbol.kind = SymbolKind::Class;
        } else {
            const auto* decl = static_cast<const FuncDeclStmt*>(statement.get());
            t.name = decl->name;
            t.params = decl->typeParams;
            t.tokens = &decl->templateTokens;
            symbol.kind = SymbolKind::Function;
        }
        symbol.name = t.name;
        if (declareSymbol(symbol, statement->line, statement->column)) templates.push_back(std::move(t));
    }
}

std::vector<SemanticType> Analyser::resolveTypeArgs(const std::vector<TypeNode>& nodes, int line, int column) {
    std::vector<SemanticType> args;
    for (const auto& n : nodes) {
        SemanticType t = resolveType(n, line, column);
        if (t.isVoidLike()) error(line, column, "Emp can't be a type argument");
        args.push_back(t);
    }
    return args;
}

// Parses the template again with its types filled in; nullptr if they don't fit
StmtPtr Analyser::fillIn(const GenericTemplate& t, const std::vector<SemanticType>& args, int line, int column) {
    if (args.size() != t.params.size()) {
        std::string expected = t.params.size() == 1 ? t.params[0] : "[";
        if (t.params.size() != 1) {
            for (size_t i = 0; i < t.params.size(); ++i) expected += (i ? ", " : "") + t.params[i];
            expected += "]";
        }
        error(line, column, "'" + t.name + "' takes " + std::to_string(t.params.size()) + " type" +
                                (t.params.size() == 1 ? "" : "s") + " (" + t.name + ":" + expected + "), got " + std::to_string(args.size()));
        return nullptr;
    }
    for (const auto& a : args) {
        if (a.isUnknown() || a.isVoidLike()) return nullptr;
    }
    if (instanceDepth > 40) {
        error(line, column, "'" + t.name + "' fills itself in without end (a generic that uses itself with ever bigger types)");
        return nullptr;
    }
    std::vector<std::pair<std::string, TypeNode>> types;
    if (t.classTypes) types = *t.classTypes;   // a generic method of a filled-in class: the class's types too
    for (size_t i = 0; i < args.size(); ++i) {
        TypeNode node;
        node.resolved = std::make_shared<SemanticType>(args[i]);
        types.emplace_back(t.params[i], std::move(node));
    }
    return Parser::instantiate(*t.tokens, types);
}

// The analyser's place while it fills something in (and back afterwards)
Analyser::FillInContext Analyser::enterTemplateModule(int moduleId) {
    FillInContext saved{currentModule, currentScope, currentClassKey, inConstructor, loopDepth, functionDepth,
                        currentReturnType, inferringReturn, inferredReturn, currentFunctionName, currentThrows, tryDepth,
                        definingLambda};
    currentModule = moduleId;
    currentScope = modules[static_cast<size_t>(moduleId)].scope.get();
    currentClassKey.clear();
    inConstructor = false;
    loopDepth = functionDepth = tryDepth = 0;
    currentReturnType.reset();
    inferringReturn = false;
    inferredReturn.reset();
    currentFunctionName.clear();
    currentThrows.reset();
    definingLambda = nullptr;
    ++instanceDepth;
    return saved;
}

void Analyser::leaveTemplateModule(const FillInContext& saved) {
    --instanceDepth;
    currentModule = saved.module;
    currentScope = saved.scope;
    currentClassKey = saved.classKey;
    inConstructor = saved.inConstructor;
    loopDepth = saved.loopDepth;
    functionDepth = saved.functionDepth;
    currentReturnType = saved.returnType;
    inferringReturn = saved.inferringReturn;
    inferredReturn = saved.inferredReturn;
    currentFunctionName = saved.functionName;
    currentThrows = saved.throws;
    tryDepth = saved.tryDepth;
    definingLambda = saved.definingLambda;
}

SemanticType Analyser::instantiateClass(int templateId, const std::vector<SemanticType>& args, int line, int column) {
    const GenericTemplate t = templates[static_cast<size_t>(templateId)];
    std::string key = std::to_string(templateId) + "|";
    for (const auto& a : args) key += canonical(a) + ";";
    if (auto found = classInstanceByKey.find(key); found != classInstanceByKey.end()) return found->second;

    const std::string usedAt = usedAtHere(line);
    StmtPtr decl = fillIn(t, args, line, column);
    if (!decl || decl->kind != StmtKind::ClassDecl) return SemanticType::unknown();
    auto* klass = static_cast<ClassDeclStmt*>(decl.get());
    klass->name = t.name + "$" + std::to_string(++instanceCounts[templateId]);
    const SemanticType type = SemanticType::userDefined(klass->name, t.moduleId, UserTypeKind::Class);
    SemanticType::setShownName(type.key(), shownWith(t.name, args));
    // Known before its members are, so Box:T can use Box:T inside
    classInstanceByKey[key] = type;
    classInstances[type.key()] = {templateId, args};

    const FillInContext saved = enterTemplateModule(t.moduleId);
    Symbol symbol;
    symbol.kind = SymbolKind::Class;
    symbol.name = klass->name;
    symbol.moduleId = t.moduleId;
    symbol.isGlobal = true;
    symbol.line = klass->line;
    symbol.column = klass->column;
    symbol.type = type;
    if (declareSymbol(symbol, klass->line, klass->column)) {
        ClassInfo info;
        info.name = klass->name;
        info.moduleId = t.moduleId;
        info.decl = klass;
        classKeys.push_back(type.key());
        classes.emplace(type.key(), std::move(info));
        collectClassMembers(classes.at(type.key()));
        checkClassMembers(classes.at(type.key()));
        pendingBodies.push_back({t.moduleId, klass, "", usedAt + " as " + type.toString()});
    }
    leaveTemplateModule(saved);
    instanceDecls.emplace_back(t.moduleId, std::move(decl));
    return type;
}

const Symbol* Analyser::instantiateFunction(int templateId, const std::vector<SemanticType>& args, int line, int column) {
    const GenericTemplate t = templates[static_cast<size_t>(templateId)];
    std::string key = std::to_string(templateId) + "|";
    for (const auto& a : args) key += canonical(a) + ";";
    if (auto found = functionInstances.find(key); found != functionInstances.end()) return &found->second;

    const std::string usedAt = usedAtHere(line);
    StmtPtr decl = fillIn(t, args, line, column);
    if (!decl || decl->kind != StmtKind::FuncDecl) return nullptr;
    auto* fn = static_cast<FuncDeclStmt*>(decl.get());
    fn->name = t.name + "$" + std::to_string(++instanceCounts[templateId]);

    const FillInContext saved = enterTemplateModule(t.moduleId);
    Symbol symbol = makeFunctionSymbol(fn, SymbolKind::Function);
    symbol.isGlobal = true;
    const Symbol* result = nullptr;
    if (declareSymbol(symbol, fn->line, fn->column)) {
        functionSymbols[fn] = symbol;
        result = &functionInstances.emplace(key, symbol).first->second;
        functionShownNames[fn->name] = shownWith(t.name, args);
        pendingBodies.push_back({t.moduleId, fn, "", usedAt + " as " + shownWith(t.name, args)});
    }
    leaveTemplateModule(saved);
    instanceDecls.emplace_back(t.moduleId, std::move(decl));
    return result;
}

// Bodies of filled-in functions and classes, once every module has been read
// (filling in more on the way)
void Analyser::analysePendingBodies() {
    for (size_t i = 0; i < pendingBodies.size(); ++i) {
        const PendingBody body = pendingBodies[i];
        const FillInContext saved = enterTemplateModule(body.moduleId);
        --instanceDepth;   // not filling in now
        fillNote = " (in " + body.usedAt + ")";
        if (!body.ownerClass.empty()) {
            if (const ClassInfo* owner = findClass(SemanticType::userDefined(body.ownerClass.substr(body.ownerClass.find(':') + 1),
                                                                             body.moduleId, UserTypeKind::Class))) {
                analyseMethodBody(*owner, static_cast<const FuncDeclStmt*>(body.statement));
            }
        } else if (body.statement->kind == StmtKind::FuncDecl) {
            analyseFunctionBody(static_cast<const FuncDeclStmt*>(body.statement));
        } else {
            analyseClassBody(static_cast<const ClassDeclStmt*>(body.statement));
        }
        fillNote.clear();
        ++instanceDepth;
        leaveTemplateModule(saved);
    }
    pendingBodies.clear();
}

// app.ds:12 -- where a generic is being used
std::string Analyser::usedAtHere(int line) const {
    std::string file;
    if (program && currentModule >= 0 && static_cast<size_t>(currentModule) < program->modules.size()) {
        file = program->modules[static_cast<size_t>(currentModule)]->displayPath;
    }
    return file + ":" + std::to_string(line);
}

// db.all:Person(...): the generic method all, filled in with Person, becomes
// a method all$1 of its class
const Symbol* Analyser::instantiateMethod(int templateId, const std::vector<SemanticType>& args, int line, int column) {
    const GenericTemplate t = templates[static_cast<size_t>(templateId)];
    std::string key = std::to_string(templateId) + "|";
    for (const auto& a : args) key += canonical(a) + ";";
    if (auto found = methodInstances.find(key); found != methodInstances.end()) return &found->second;
    auto owner = classes.find(t.ownerClass);
    if (owner == classes.end()) return nullptr;

    const std::string usedAt = usedAtHere(line);
    StmtPtr decl = fillIn(t, args, line, column);
    if (!decl || decl->kind != StmtKind::FuncDecl) return nullptr;
    auto* method = static_cast<FuncDeclStmt*>(decl.get());
    method->name = t.name + "$" + std::to_string(++instanceCounts[templateId]);

    const FillInContext saved = enterTemplateModule(t.moduleId);
    currentClassKey = t.ownerClass;
    Symbol symbol = makeFunctionSymbol(method, SymbolKind::Method);
    symbol.ownerClass = t.ownerClass;
    functionSymbols[method] = symbol;
    owner->second.methods.emplace(symbol.name, symbol);
    const Symbol* result = &methodInstances.emplace(key, symbol).first->second;
    functionShownNames[method->name] = shownWith(t.name, args);
    pendingBodies.push_back({t.moduleId, method, t.ownerClass, usedAt + " as " + shownWith(t.name, args)});
    leaveTemplateModule(saved);
    methodInstanceDecls.emplace_back(owner->second.decl, std::move(decl));
    return result;
}

std::vector<std::pair<int, StmtPtr>> Analyser::takeInstances() {
    // Filled-in generic methods join their classes (whose declarations are
    // otherwise read-only to the analyser)
    for (auto& [owner, method] : methodInstanceDecls) const_cast<ClassDeclStmt*>(owner)->members.push_back(std::move(method));
    methodInstanceDecls.clear();
    return std::move(instanceDecls);
}

// What T must be for `node` (written with T) to be `actual`. False when two
// uses disagree.
bool Analyser::unify(const TypeNode& node, const SemanticType& actual, const std::vector<std::string>& params,
                     std::vector<std::optional<SemanticType>>& found) {
    if (actual.isUnknown() || node.resolved || !node.unionMembers.empty()) return true;
    if (node.isList || node.isSet) {
        if (actual.kind != (node.isList ? SemanticTypeKind::List : SemanticTypeKind::Set) || !actual.elementType) return true;
        TypeNode inner = node;
        inner.isList = inner.isSet = false;
        if (!inner.group.empty() && inner.name.empty()) return unify(inner.group.front(), *actual.elementType, params, found);
        return unify(inner, *actual.elementType, params, found);
    }
    if (!node.group.empty()) return unify(node.group.front(), actual, params, found);
    if (node.isMap) {
        if (actual.kind != SemanticTypeKind::Map || node.mapTypes.size() != 2) return true;
        return unify(node.mapTypes[0], *actual.keyType, params, found) && unify(node.mapTypes[1], *actual.elementType, params, found);
    }
    if (node.isFunction) {
        if (actual.kind != SemanticTypeKind::Function) return true;
        bool ok = node.funcReturn.empty() || !actual.returnType || unify(node.funcReturn.front(), *actual.returnType, params, found);
        for (size_t i = 0; i < node.funcParams.size() && i < actual.parameterTypes.size(); ++i) {
            ok = unify(node.funcParams[i], actual.parameterTypes[i], params, found) && ok;
        }
        return ok;
    }
    if (!node.typeArgs.empty()) {
        // Box:T against a Box:Int
        auto instance = classInstances.find(actual.key());
        if (actual.kind != SemanticTypeKind::UserDefined || instance == classInstances.end()) return true;
        const auto& [templateId, args] = instance->second;
        const std::string& base = node.name.substr(node.name.rfind('.') == std::string::npos ? 0 : node.name.rfind('.') + 1);
        if (templates[static_cast<size_t>(templateId)].name != base) return true;
        bool ok = true;
        for (size_t i = 0; i < node.typeArgs.size() && i < args.size(); ++i) ok = unify(node.typeArgs[i], args[i], params, found) && ok;
        return ok;
    }
    auto at = std::find(params.begin(), params.end(), node.name);
    if (at == params.end()) return true;
    auto& slot = found[static_cast<size_t>(at - params.begin())];
    SemanticType value = actual;
    value.radix.reset();
    if (!slot) {
        slot = value;
    } else if (*slot != value) {
        if (isAssignable(*slot, value)) return true;      // T is Dbl, then an Int comes
        if (isAssignable(value, *slot)) slot = value;      // T is Int, then a Dbl comes
        else return false;
    }
    return true;
}

// The types of a generic call or `new` that doesn't say them: from the
// arguments (analysed quietly; they are analysed again for real), then from
// what the value goes into
std::optional<std::vector<SemanticType>> Analyser::inferTypeArgs(const GenericTemplate& t, const std::vector<Param>& params,
                                                                 const std::vector<Argument>& args, const TypeNode* result,
                                                                 const Expr* at, const std::string& written) {
    std::vector<std::optional<SemanticType>> found(t.params.size());
    const size_t diagnosticCount = diagnostics.size();
    bool agree = true;
    for (size_t i = 0; i < args.size(); ++i) {
        const Argument& arg = args[i];
        const Param* param = nullptr;
        if (arg.name) {
            for (const auto& p : params) if (p.name == *arg.name) param = &p;
        } else if (i < params.size()) {
            param = &params[i];
        } else if (!params.empty() && params.back().isVariadic) {
            param = &params.back();
        }
        SemanticType type = analyseExpression(arg.value.get());
        if (!param || arg.isSpread) continue;
        agree = unify(param->type, type, t.params, found) && agree;
    }
    diagnostics.resize(diagnosticCount);
    if (result) {
        if (auto hint = typeHints.find(at); hint != typeHints.end()) unify(*result, hint->second, t.params, found);
    }
    std::vector<SemanticType> types;
    for (size_t i = 0; i < found.size(); ++i) {
        if (!found[i] || !agree) {
            std::string example = t.params.size() == 1 ? "Int" : "[Int, Str]";
            error(at, agree ? "Can't tell what " + t.params[i] + " is in " + written + "; write it, e.g. " + written + ":" + example + "(...)"
                            : "The arguments of " + written + " don't agree on what " + t.params[i] + " is; write it, e.g. " + written + ":" + example + "(...)");
            return std::nullopt;
        }
        types.push_back(*found[i]);
    }
    return types;
}

} // namespace dessa
