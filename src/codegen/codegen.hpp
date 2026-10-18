#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "analyser.hpp"
#include "ast.hpp"
#include "program.hpp"

namespace dessa {

// Turns an analysed Program into a single self-contained C++20 source file
// that includes runtime/dessa_runtime.hpp.
//
// Layout of the generated file:
//   1. includes (runtime + imported builtin libraries)
//   2. per module: forward declarations of classes/interfaces, enums
//   3. interfaces (abstract bases), then classes, parents first
//   4. per module: global variables and function prototypes
//   5. per module: method, constructor and function bodies
//   6. per module: ds_init() (imported modules: global initialisers) or
//      ds_run() (entry module: all top-level code)
//   7. main()
//
// Each module lives in namespace ds_m<id>. Dessa classes are held by
// dessa::Ref<T> (std::shared_ptr) and derive from dessa::Object.
class Codegen {
public:
    Codegen(const Program& program, const Analyser& analyser);
    std::string generate();

private:
    const Program& program;
    const Analyser& analyser;
    std::ostringstream out;
    int indentLevel = 0;
    int tempCounter = 0;

    // Context of the code being emitted
    int moduleId = 0;
    const ClassInfo* currentClass = nullptr;
    std::optional<SemanticType> returnType;

    // ---- Output helpers ----
    void line(const std::string& text);
    std::string temp(const std::string& base);

    // ---- Names & types ----
    static std::string mangle(const std::string& name);
    static std::string ns(int moduleId);
    std::string typeName(const SemanticType& type) const;      // ds_m0::Person (no Ref)
    std::string cppType(const SemanticType& type) const;       // dessa::Ref<ds_m0::Person>
    std::string globalName(const Symbol& symbol) const;
    static std::string cppString(const std::string& text);
    static std::string cppChar(char c);
    bool isPlain(const SemanticType& type) const;
    bool isOptionalValue(const SemanticType& type) const;      // maps to dessa::Opt<T>

    // ---- Declarations ----
    void emitTypeDeclarations();
    void emitInterface(const InterfaceInfo& info);
    void emitClass(const ClassInfo& info);
    void emitGlobalsAndPrototypes(const Module& module);
    void emitBodies(const Module& module);
    void emitClassBodies(const ClassInfo& info);
    void emitFunction(const FuncDeclStmt* decl, const std::string& qualifiedName);
    void emitConstructor(const ClassInfo& info, const FuncDeclStmt* ctor);
    const Symbol* nearestConstructor(const ClassInfo& info) const;
    void emitModuleEntry(const Module& module);
    std::string signature(const Symbol& symbol, const std::string& name) const;

    // ---- Statements ----
    void stmt(const Stmt* statement);
    void block(const Stmt* statement);
    void varDecl(const VarDeclStmt* decl, bool asGlobalAssignment);
    void forIn(const ForInStmt* statement);
    std::string loopItem(const std::string& item, const SemanticType& iterable, const SemanticType* declared, const VarDeclStmt* decl);
    void switchStmt(const SwitchStmt* statement);
    void tryStmt(const TryStmt* statement);

    // ---- Expressions ----
    std::string expr(const Expr* expression);
    std::string exprAs(const Expr* expression, const SemanticType& target);
    std::string literal(const LiteralExpr* expression);
    std::string identifier(const IdentifierExpr* expression);
    std::string binary(const BinaryExpr* expression);
    std::string assign(const AssignExpr* expression);
    std::string call(const CallExpr* expression);
    std::string member(const MemberExpr* expression);
    std::string lambda(const LambdaExpr* expression);
    std::string collection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at,
                           const SemanticType* target = nullptr);
    std::string templateString(const TemplateStringExpr* expression);
    std::string mapLiteral(const MapExpr* expression, const SemanticType* target);
    // Arguments in parameter order. When `hoist` is given and evaluation
    // order matters (2+ arguments with side effects), arguments are first
    // stored in temporaries, in source order; wrap the call with withHoist().
    std::string arguments(const Expr* call, const std::vector<Argument>& args,
                          const std::vector<FunctionParameterSymbol>& params, std::string* hoist = nullptr);
    static std::string withHoist(const std::string& hoist, const std::string& callCode);
    static bool hasSideEffects(const Expr* expression);
    std::string printable(const Expr* expression);   // wraps Int[r] in dessa::Radix
    const SemanticType& typeOf(const Expr* expression) const;
};

} // namespace dessa
