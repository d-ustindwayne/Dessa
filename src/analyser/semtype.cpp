// src/analyser/semtype.cpp
#include "semtype.hpp"

#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <utility>

namespace dessa {

SemanticType SemanticType::unknown() {
    return {};
}

SemanticType SemanticType::simple(SemanticTypeKind kind) {
    SemanticType type;
    type.kind = kind;
    return type;
}

SemanticType SemanticType::userDefined(std::string name, int moduleId, UserTypeKind userKind) {
    SemanticType type;
    type.kind = SemanticTypeKind::UserDefined;
    type.name = std::move(name);
    type.moduleId = moduleId;
    type.userKind = userKind;
    return type;
}

SemanticType SemanticType::listOf(const SemanticType& element) {
    SemanticType type;
    type.kind = SemanticTypeKind::List;
    type.elementType = std::make_shared<SemanticType>(element);
    return type;
}

SemanticType SemanticType::setOf(const SemanticType& element) {
    SemanticType type;
    type.kind = SemanticTypeKind::Set;
    type.elementType = std::make_shared<SemanticType>(element);
    return type;
}

SemanticType SemanticType::mapOf(const SemanticType& key, const SemanticType& value) {
    SemanticType type;
    type.kind = SemanticTypeKind::Map;
    type.keyType = std::make_shared<SemanticType>(key);
    type.elementType = std::make_shared<SemanticType>(value);
    return type;
}

SemanticType SemanticType::unionOf(std::vector<SemanticType> members) {
    std::vector<SemanticType> flat;
    for (const auto& member : members) {
        if (member.kind == SemanticTypeKind::Union) {
            flat.insert(flat.end(), member.unionMembers.begin(), member.unionMembers.end());
        } else {
            flat.push_back(member);
        }
    }

    std::vector<SemanticType> unique;
    for (const auto& member : flat) {
        if (member.isUnknown()) return SemanticType::unknown();
        if (member.kind == SemanticTypeKind::Obj) return member;   // Obj holds everything already
        bool found = std::any_of(unique.begin(), unique.end(), [&](const SemanticType& e) { return e == member; });
        if (!found) unique.push_back(member);
    }

    if (unique.empty()) return SemanticType::unknown();
    if (unique.size() == 1) return unique.front();

    // Keep Nil last so `Int | Nil` and `Nil | Int` print and compare the same.
    std::stable_partition(unique.begin(), unique.end(), [](const SemanticType& t) {
        return t.kind != SemanticTypeKind::Nil;
    });

    SemanticType type;
    type.kind = SemanticTypeKind::Union;
    type.unionMembers = std::move(unique);
    return type;
}

SemanticType SemanticType::functionOf(std::vector<SemanticType> parameters, const SemanticType& result) {
    SemanticType type;
    type.kind = SemanticTypeKind::Function;
    type.parameterTypes = std::move(parameters);
    type.returnType = std::make_shared<SemanticType>(result);
    return type;
}

bool SemanticType::isNumeric() const {
    return kind == SemanticTypeKind::Int || kind == SemanticTypeKind::Flt || kind == SemanticTypeKind::Dbl;
}

bool SemanticType::isInteger() const {
    return kind == SemanticTypeKind::Int;
}

bool SemanticType::isBoolean() const {
    return kind == SemanticTypeKind::Bul;
}

bool SemanticType::isNullable() const {
    return containsNil();
}

bool SemanticType::containsNil() const {
    if (kind == SemanticTypeKind::Nil) return true;
    if (kind != SemanticTypeKind::Union) return false;
    return std::any_of(unionMembers.begin(), unionMembers.end(), [](const SemanticType& member) {
        return member.containsNil();
    });
}

bool SemanticType::isVoidLike() const {
    return kind == SemanticTypeKind::Emp;
}

std::string SemanticType::key() const {
    return std::to_string(moduleId) + ":" + name;
}

static std::unordered_map<std::string, std::string>& shownNames() {
    static std::unordered_map<std::string, std::string> names;
    return names;
}

void SemanticType::setShownName(const std::string& key, const std::string& shown) {
    shownNames()[key] = shown;
}

void SemanticType::clearShownNames() {
    shownNames().clear();
}

// A union element needs brackets: (Person | Int)<> is not Person | Int<>.
static std::string elementString(const SemanticType* element) {
    if (!element) return "Unknown";
    if (element->kind == SemanticTypeKind::Union) return "(" + element->toString() + ")";
    return element->toString();
}

std::string SemanticType::toString() const {
    switch (kind) {
        case SemanticTypeKind::Unknown: return "Unknown";
        case SemanticTypeKind::Nil: return "Nil";
        case SemanticTypeKind::Emp: return "Emp";
        case SemanticTypeKind::Str: return "Str";
        case SemanticTypeKind::Int:
            if (radix && *radix != 10) return "Int[" + std::to_string(*radix) + "]";
            return "Int";
        case SemanticTypeKind::Bul: return "Bul";
        case SemanticTypeKind::Flt: return "Flt";
        case SemanticTypeKind::Dbl: return "Dbl";
        case SemanticTypeKind::Chr: return "Chr";
        case SemanticTypeKind::Obj: return "Obj";
        case SemanticTypeKind::List: return elementString(elementType.get()) + "<>";
        case SemanticTypeKind::Set: return elementString(elementType.get()) + "{}";
        case SemanticTypeKind::Map:
            return "Map[" + (keyType ? keyType->toString() : "Unknown") + ", " +
                   (elementType ? elementType->toString() : "Unknown") + "]";
        case SemanticTypeKind::UserDefined: {
            if (name.find('$') == std::string::npos) return name;
            auto shown = shownNames().find(key());
            return shown == shownNames().end() ? name : shown->second;
        }
        case SemanticTypeKind::Union: {
            std::ostringstream out;
            for (size_t i = 0; i < unionMembers.size(); ++i) {
                if (i != 0) out << " | ";
                const auto& m = unionMembers[i];
                bool wrap = m.kind == SemanticTypeKind::Function;
                out << (wrap ? "(" : "") << m.toString() << (wrap ? ")" : "");
            }
            return out.str();
        }
        case SemanticTypeKind::Function: {
            // Written the way it is declared: Func[Ret, [A, B]]
            std::ostringstream out;
            out << "Func[" << (returnType ? returnType->toString() : "Unknown");
            out << ", [";
            for (size_t i = 0; i < parameterTypes.size(); ++i) {
                if (i != 0) out << ", ";
                out << parameterTypes[i].toString();
            }
            out << "]]";
            return out.str();
        }
    }
    return "Unknown";
}

bool operator==(const SemanticType& a, const SemanticType& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case SemanticTypeKind::UserDefined:
            return a.name == b.name && a.moduleId == b.moduleId;
        case SemanticTypeKind::List:
        case SemanticTypeKind::Set:
            if (!a.elementType || !b.elementType) return a.elementType == b.elementType;
            return *a.elementType == *b.elementType;
        case SemanticTypeKind::Map: {
            auto same = [](const std::shared_ptr<SemanticType>& x, const std::shared_ptr<SemanticType>& y) {
                if (!x || !y) return x == y;
                return *x == *y;
            };
            return same(a.keyType, b.keyType) && same(a.elementType, b.elementType);
        }
        case SemanticTypeKind::Union:
            if (a.unionMembers.size() != b.unionMembers.size()) return false;
            // order-insensitive
            return std::all_of(a.unionMembers.begin(), a.unionMembers.end(), [&](const SemanticType& m) {
                return std::find(b.unionMembers.begin(), b.unionMembers.end(), m) != b.unionMembers.end();
            });
        case SemanticTypeKind::Function:
            if (a.parameterTypes != b.parameterTypes) return false;
            if (!a.returnType || !b.returnType) return a.returnType == b.returnType;
            return *a.returnType == *b.returnType;
        default:
            return true; // radix is deliberately ignored
    }
}

bool operator!=(const SemanticType& left, const SemanticType& right) {
    return !(left == right);
}

SemanticType withoutNil(const SemanticType& type) {
    if (type.kind == SemanticTypeKind::Nil) return SemanticType::unknown();
    if (type.kind != SemanticTypeKind::Union) return type;
    std::vector<SemanticType> members;
    for (const auto& member : type.unionMembers) {
        if (member.kind != SemanticTypeKind::Nil) members.push_back(member);
    }
    return SemanticType::unionOf(std::move(members));
}

SemanticType withNil(const SemanticType& type) {
    if (type.isUnknown() || type.containsNil()) return type;
    return SemanticType::unionOf({type, SemanticType::simple(SemanticTypeKind::Nil)});
}

} // namespace dessa
