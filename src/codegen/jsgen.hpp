#pragma once

#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "analyser.hpp"
#include "ast.hpp"
#include "program.hpp"

namespace dessa {

// Turns an analysed Program into JavaScript (dessa build --target web / js).
// The twin of Codegen: same program, same output, run by a browser or Node.
//
// Layout of the generated file:
//   (function () {
//     <runtime/dessa_runtime.js>              the `$` object
//     <a library's .js native part, e.g. webutils.js>
//     enums, then classes (parents first)
//     per module: globals and functions        m<id>$name
//     per module: m<id>$init() / the entry's m<id>$run()
//     $.run(...)
//   })();
//
// See runtime/dessa_runtime.js for how Dessa values are represented.
class JsCodegen {
public:
    JsCodegen(const Program& program, const Analyser& analyser);
    // `runtime`: the text of runtime/dessa_runtime.js.
    std::string generate(const std::string& runtime);

private:
    const Program& program;
    const Analyser& analyser;
    std::ostringstream out;
    int indentLevel = 0;
    int tempCounter = 0;
    int moduleId = 0;
    const ClassInfo* currentClass = nullptr;
    std::optional<SemanticType> returnType;
    std::set<std::string> externClasses;   // class keys mapped onto JS objects (webutils)

    void line(const std::string& text);
    std::string temp(const std::string& base);
    const SemanticType& typeOf(const Expr* e) const;

    // ---- Names ----
    static std::string jsName(const std::string& name);
    static std::string memberName(const std::string& name);
    static std::string jsString(const std::string& text);
    std::string moduleName(int id, const std::string& name) const;
    std::string globalName(const Symbol& symbol) const;
    std::string className(const SemanticType& type) const;
    std::string fileVar(int id) const { return "m" + std::to_string(id) + "$file"; }

    // ---- Types ----
    bool isExtern(const SemanticType& type) const;
    bool isExternMember(const Symbol* symbol) const;
    std::string defaultValue(const SemanticType& type) const;
    std::string fltShape(const SemanticType& type) const;
    static bool needsCopy(const SemanticType& type);
    static bool isNumber(const SemanticType& type);   // Dbl / Flt
    static bool isPrimitive(const SemanticType& type);
    std::string convert(const std::string& code, const SemanticType& from, const SemanticType& to) const;

    // ---- Program ----
    void collectExternClasses();
    void emitEnums(const Module& module);
    void emitClass(const ClassInfo& info);
    void emitGlobals(const Module& module);
    void emitFunction(const FuncDeclStmt* decl, const std::string& name, bool isMethod);
    void emitModuleEntry(const Module& module);
    const Symbol* nearestConstructor(const ClassInfo& info) const;
    std::string params(const Symbol& symbol) const;

    // ---- Statements ----
    void stmt(const Stmt* statement);
    void block(const Stmt* statement);
    void varDecl(const VarDeclStmt* decl, bool asGlobalAssignment);
    void forIn(const ForInStmt* s);
    void switchStmt(const SwitchStmt* s);
    void tryStmt(const TryStmt* s);

    // ---- Expressions ----
    std::string expr(const Expr* e);
    std::string exprAs(const Expr* e, const SemanticType& target);
    std::string printable(const Expr* e);
    std::string literal(const LiteralExpr* e);
    std::string identifier(const IdentifierExpr* e);
    std::string binary(const BinaryExpr* e);
    std::string assign(const AssignExpr* e);
    std::string postfix(const PostfixExpr* e);
    std::string call(const CallExpr* e);
    std::string member(const MemberExpr* e);
    std::string memberTarget(const MemberExpr* e);
    std::string lambda(const LambdaExpr* e);
    std::string collection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at,
                           const SemanticType* target = nullptr);
    std::string mapLiteral(const MapExpr* e, const SemanticType* target);
    std::string arguments(const Expr* callExpr, const std::vector<Argument>& args,
                          const std::vector<FunctionParameterSymbol>& params, bool toExtern = false);
    std::string isCheck(const std::string& value, const SemanticType& target) const;
    // Can `value` be stored in a place of this type? (Obj setting a class field)
    std::string fitsCheck(const std::string& value, const SemanticType& target) const;
    std::string number(const Expr* e);   // the operand as a JS number (Int converted)
    std::string fromExtern(const std::string& code, const SemanticType& type) const;
    std::string toExtern(const std::string& code, const SemanticType& type) const;
    // Plain-variable / field reads that would share a list instead of copying it.
    static bool isStorageRead(const Expr* e);
    // The locals a lambda uses from outside (captured by value, like C++'s [=]).
    void capturedLocals(const Expr* e, const LambdaExpr* lambda, std::vector<std::pair<std::string, SemanticType>>& out,
                        std::set<std::string>& seen) const;
    void capturedLocals(const Stmt* s, const LambdaExpr* lambda, std::vector<std::pair<std::string, SemanticType>>& out,
                        std::set<std::string>& seen) const;
};

} // namespace dessa
