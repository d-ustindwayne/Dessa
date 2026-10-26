#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "semtype.hpp"

namespace dessa {

struct Expr;

enum class SymbolKind {
    Variable,
    Constant,
    Function,
    Class,
    Interface,
    Enum,
    Parameter,
    Field,
    Method,
    Builtin,    // a builtin library function or constant
    Module      // $("math" as m): a module under a name
};

struct FunctionParameterSymbol {
    std::string name;
    SemanticType type;                    // for a variadic parameter: the list type (Str<>)
    bool variadic = false;                // name[Str*]
    const Expr* defaultValue = nullptr;   // name[Str]="John" (points into the AST)
};

struct Symbol {
    SymbolKind kind = SymbolKind::Variable;
    std::string name;
    SemanticType type = SemanticType::unknown();
    bool isConst = false;
    std::vector<FunctionParameterSymbol> parameters;
    SemanticType returnType = SemanticType::unknown();

    // Where it was declared
    int moduleId = 0;
    int line = 0;
    int column = 0;

    // Global (module-level) symbol vs. a local of some function
    bool isGlobal = false;
    // Field / Method: the class that declares it ("<module>:<name>")
    std::string ownerClass;
    // Imported under a different local name: the original name
    std::string originalName;
    // Builtin: the C++ expression the call/constant maps to
    std::string cppName;
    bool variadic = false;
    // Imported: which import brought it in (Analyser::importRecords), so
    // unused imports can be found
    int importId = -1;
    // Module: which Analyser::aliasTables holds its names
    int aliasTable = -1;
    // Function / Method: what it says it may throw (func[Int !! NumberError])
    std::vector<SemanticType> throws;
    // A generic function or class (Analyser::templates): filled in when used
    int templateId = -1;
};

enum class ScopeKind {
    Module,     // top level of a .ds file
    Class,      // class body (fields + methods visible)
    Function,   // function / method body
    Lambda,     // lambda body: outer locals are captured by value
    Block       // { }, loop bodies, if branches
};

class Scope {
public:
    explicit Scope(ScopeKind kind, Scope* parent = nullptr);

    bool declare(const Symbol& symbol);
    Symbol* resolveLocal(const std::string& name);
    const Symbol* resolveLocal(const std::string& name) const;
    Symbol* resolve(const std::string& name);
    const Symbol* resolve(const std::string& name) const;
    // The scope in which `name` is declared (or nullptr)
    Scope* owner(const std::string& name);

    // Flow narrowing: `name` is known to hold a value of `type` (no Nil)
    // from this point within this scope. std::nullopt = explicitly not
    // narrowed (shadows a narrowing in an outer scope).
    void narrow(const std::string& name, const SemanticType& type);
    void unnarrow(const std::string& name);
    // Looks for a narrowing, stopping at function/lambda boundaries since a
    // function may run at any time.
    std::optional<SemanticType> narrowed(const std::string& name) const;

    ScopeKind kind() const { return scopeKind; }
    Scope* parent() const;
    const std::unordered_map<std::string, Symbol>& all() const { return symbols; }

private:
    ScopeKind scopeKind;
    Scope* parentScope = nullptr;
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::optional<SemanticType>> narrowings;
};

} // namespace dessa
