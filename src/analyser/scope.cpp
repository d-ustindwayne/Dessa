#include "scope.hpp"

namespace dessa {

Scope::Scope(ScopeKind kind, Scope* parent) : scopeKind(kind), parentScope(parent) {}

bool Scope::declare(const Symbol& symbol) {
    return symbols.emplace(symbol.name, symbol).second;
}

Symbol* Scope::resolveLocal(const std::string& name) {
    auto it = symbols.find(name);
    return it == symbols.end() ? nullptr : &it->second;
}

const Symbol* Scope::resolveLocal(const std::string& name) const {
    auto it = symbols.find(name);
    return it == symbols.end() ? nullptr : &it->second;
}

Symbol* Scope::resolve(const std::string& name) {
    for (Scope* scope = this; scope != nullptr; scope = scope->parentScope) {
        if (auto* symbol = scope->resolveLocal(name)) return symbol;
    }
    return nullptr;
}

const Symbol* Scope::resolve(const std::string& name) const {
    for (const Scope* scope = this; scope != nullptr; scope = scope->parentScope) {
        if (const auto* symbol = scope->resolveLocal(name)) return symbol;
    }
    return nullptr;
}

Scope* Scope::owner(const std::string& name) {
    for (Scope* scope = this; scope != nullptr; scope = scope->parentScope) {
        if (scope->resolveLocal(name)) return scope;
    }
    return nullptr;
}

void Scope::narrow(const std::string& name, const SemanticType& type) {
    narrowings[name] = type;
}

void Scope::unnarrow(const std::string& name) {
    narrowings[name] = std::nullopt;
}

std::optional<SemanticType> Scope::narrowed(const std::string& name) const {
    for (const Scope* scope = this; scope != nullptr; scope = scope->parentScope) {
        auto it = scope->narrowings.find(name);
        if (it != scope->narrowings.end()) return it->second;
        if (scope->resolveLocal(name)) return std::nullopt; // reached the declaration
        if (scope->scopeKind == ScopeKind::Function || scope->scopeKind == ScopeKind::Lambda) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

Scope* Scope::parent() const {
    return parentScope;
}

} // namespace dessa
