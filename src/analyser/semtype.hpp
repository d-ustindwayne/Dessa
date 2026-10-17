#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dessa {

enum class SemanticTypeKind {
    Unknown,
    Nil,
    Emp,
    Str,
    Int,
    Bul,
    Flt,
    Dbl,
    Chr,
    List,
    Set,
    Map,        // Map[K, V]: keyType + elementType (the value type)
    UserDefined, // class, interface or enum; see `name` + `moduleId`
    Union,
    Function,
    Obj         // any value: fields are looked up when the program runs
};

// What a UserDefined type refers to.
enum class UserTypeKind { None, Class, Interface, Enum };

struct SemanticType {
    SemanticTypeKind kind = SemanticTypeKind::Unknown;

    // UserDefined: the declared name plus the module that declared it, so
    // two modules may both have a class called `Node`.
    std::string name;
    int moduleId = 0;
    UserTypeKind userKind = UserTypeKind::None;

    // Int[2] / Int[8] / Int[16]: display base. Not part of type identity.
    std::optional<int> radix;

    std::shared_ptr<SemanticType> elementType;   // List / Set element, Map value
    std::shared_ptr<SemanticType> keyType;       // Map key
    std::vector<SemanticType> unionMembers;      // Union
    std::vector<SemanticType> parameterTypes;    // Function
    std::shared_ptr<SemanticType> returnType;    // Function

    static SemanticType unknown();
    static SemanticType simple(SemanticTypeKind kind);
    static SemanticType userDefined(std::string name, int moduleId, UserTypeKind userKind);
    static SemanticType listOf(const SemanticType& element);
    static SemanticType setOf(const SemanticType& element);
    static SemanticType mapOf(const SemanticType& key, const SemanticType& value);
    static SemanticType unionOf(std::vector<SemanticType> members);
    static SemanticType functionOf(std::vector<SemanticType> parameters, const SemanticType& result);

    bool is(SemanticTypeKind k) const { return kind == k; }
    bool isUnknown() const { return kind == SemanticTypeKind::Unknown; }
    bool isNumeric() const;
    bool isInteger() const;
    bool isBoolean() const;
    bool isNullable() const;
    bool containsNil() const;
    bool isVoidLike() const;
    bool isClass() const { return kind == SemanticTypeKind::UserDefined && userKind == UserTypeKind::Class; }
    bool isInterface() const { return kind == SemanticTypeKind::UserDefined && userKind == UserTypeKind::Interface; }
    bool isEnum() const { return kind == SemanticTypeKind::UserDefined && userKind == UserTypeKind::Enum; }
    // Class or interface: held by reference (shared_ptr) in generated code.
    bool isReference() const { return isClass() || isInterface(); }

    // A unique key for a user-defined type across modules: "<module>:<name>"
    std::string key() const;

    std::string toString() const;
};

bool operator==(const SemanticType& left, const SemanticType& right);
bool operator!=(const SemanticType& left, const SemanticType& right);

// `T | Nil` -> `T` (or the union of the non-nil members).
SemanticType withoutNil(const SemanticType& type);
// `T` -> `T | Nil`
SemanticType withNil(const SemanticType& type);

} // namespace dessa
