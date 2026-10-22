#pragma once

#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analyser.hpp"
#include "ast.hpp"
#include "program.hpp"

namespace dessa {

// Turns an analysed Program into one C source file that includes
// runtime/dessa_rt.h (the runtime itself, dessa_rt.c, is compiled once and
// linked in). It's the default way programs are built: C compiles much
// faster than C++.
//
// The C backend doesn't cover everything yet. When a program uses something
// it can't translate (Obj, unions, sets, libraries with a C++ part, ...),
// generate() returns "" and says why; the program is then built from C++
// instead (see Codegen), so the user never notices.
//
// Values (see dessa_rt.h): Int Dbl Flt Bul Chr are C numbers; Str, List, Map,
// class instances and functions are counted heap values. The generated code
// keeps the counts right:
//   - every variable, field, list item and map entry holds its own count
//   - an expression's value is either borrowed (a variable, a field, an item)
//     or owned (a call's result, new text, a new object); owned values that
//     aren't stored anywhere are kept in temporaries, released when the
//     statement is done
//   - locals are released when their block ends (or at return, break,
//     continue); parameters are borrowed from the caller
//
// Layout of the generated file:
//   1. #include "dessa_rt.h", the source file names
//   2. struct names (typedefs) for classes and interfaces
//   3. class structs (all fields, the parents' first), method tables, the
//      interfaces' method tables, closures' captured values
//   4. prototypes and globals
//   5. text literals, element kinds, class descriptions
//   6. functions, methods, constructors, closures
//   7. main()
class CCodegen {
public:
    CCodegen(const Program& program, const Analyser& analyser);
    // The C program, or "" with the reason the C backend can't build it yet
    std::string generate(std::string& reason);

    // ---- internal types (public for the helpers in the .cpp files) ----

    // An expression's C code. Owned: the caller must store or release it
    // (only matters for counted values).
    struct CV {
        std::string code;
        bool owned = false;
    };

    // A local variable in scope
    struct Local {
        std::string name;      // Dessa name
        std::string cName;
        SemanticType type;
        bool release = false;  // released when its block ends
    };
    struct LambdaInfo;
    struct Scope {
        std::vector<Local> locals;
        bool isLoop = false;               // break / continue stop here
        std::string continueLabel;         // for loops: continue jumps here (the increment)
        std::string breakLabel;            // set when a break must jump (out of a switch-like block)
        std::string tryFrame;              // a try body: the ds_catch variable to pop on the way out
        LambdaInfo* lambda = nullptr;      // a lambda's body starts here: names outside are captured
        bool function = false;             // a function body starts here
    };
    struct Capture {
        std::string name;      // Dessa name ("this" for this)
        std::string field;     // in the closure struct
        SemanticType type;
        std::string cType;
    };
    struct LambdaInfo {
        int id = 0;
        std::vector<Capture> captures;
        std::string selfName;   // var[Func[..]] fact = (n) := .. fact(n - 1): the lambda is `fact` inside itself
    };

private:
    const Program& program;
    const Analyser& analyser;

    // Sections of the output
    std::ostringstream typedefs, structs, protos, data, code;
    // The function being written
    std::ostringstream* out = nullptr;
    int indentLevel = 0;
    int tempCounter = 0;

    // Context
    int moduleId = 0;
    const ClassInfo* currentClass = nullptr;
    std::optional<SemanticType> returnType;
    bool volatileLocals = false;           // the function has a try: locals are volatile (setjmp)
    std::vector<Scope> scopes;
    // temporaries of the statement being written: name, C type
    std::vector<std::vector<std::pair<std::string, std::string>>> temps;

    // What the program uses
    std::unordered_set<const Stmt*> reachable;
    std::unordered_map<std::string, const Stmt*> declarations;   // "<module>:<name>" -> top-level declaration
    std::map<std::string, std::string> literals;                // text -> C name
    std::map<std::string, std::string> elemNames;               // element key -> C name
    std::map<std::string, std::string> emptyLists;              // element C name -> shared empty list
    std::map<std::string, int> interfaceIds;                    // interface key -> id
    std::set<std::string> functionValues;                       // functions used as values (wrapper made)
    std::set<std::string> overridden;                           // "<class key>.<method>": overridden below that class
    std::set<std::string> glueLibraries;                        // libraries reached through their C++ bridge (cglue)
    std::set<std::string> gluePrototypes;
    std::string builtinPrelude;   // set by builtinCall: arguments evaluated first, in order
    std::string recursiveName;    // the variable whose lambda is being made (it may call itself)
    // a?.m(): builtinMethod's receiver, already evaluated (not nil)
    const std::string* receiverOverride = nullptr;
    SemanticType receiverOverrideType;
    int lambdaCounter = 0;
    int sortCounter = 0;

    // ---- Output ----
    void line(const std::string& text);
    std::string temp();
    std::string literal(const std::string& text);
    [[noreturn]] void unsupported(const std::string& what) const;

    // ---- Types ----
    bool isCounted(const SemanticType& type) const;          // a counted heap value (may be nil)
    bool isOptValue(const SemanticType& type) const;         // Int | Nil and the like: a ds_opt_* struct
    std::string cType(const SemanticType& type) const;
    std::string optType(const SemanticType& base) const;
    std::string elem(const SemanticType& type);              // &ds_elem_int, ...
    std::string emptyList(const SemanticType& elementType);
    std::string structName(const SemanticType& classType) const;
    std::string clsName(const SemanticType& classType) const; // the class description variable
    std::string defaultValue(const SemanticType& type);      // a fresh (owned) default value
    const ClassInfo* classOf(const SemanticType& type) const;
    const InterfaceInfo* interfaceOf(const SemanticType& type) const;
    std::vector<const ClassInfo*> chain(const ClassInfo& info) const;   // the class, then its parents
    std::vector<std::pair<std::string, SemanticType>> allFields(const ClassInfo& info) const;
    std::vector<std::string> allMethods(const ClassInfo& info) const;   // parents' first
    const ClassInfo* methodOwner(const ClassInfo& info, const std::string& method) const;
    const Symbol* nearestConstructor(const ClassInfo& info) const;
    std::string mangle(const std::string& name) const;
    std::string globalName(const Symbol& symbol) const;
    std::string functionName(int moduleId, const std::string& name) const;
    std::string methodName(const ClassInfo& info, const std::string& method) const;

    // ---- Program ----
    void findReachable();
    void markType(const SemanticType& type, std::vector<const Stmt*>& work);
    void markSymbol(const Symbol& symbol, std::vector<const Stmt*>& work);
    void visitStmt(const Stmt* s, std::vector<const Stmt*>& work);
    void visitExpr(const Expr* e, std::vector<const Stmt*>& work);
    bool isReachable(const Stmt* s) const { return reachable.count(s) != 0; }
    void emitTypes();
    void emitClassStruct(const ClassInfo& info);
    void emitClassCode(const ClassInfo& info);
    void emitInterface(const InterfaceInfo& info);
    void emitEnum(const EnumDeclStmt* decl, int moduleId);
    void emitFunction(const FuncDeclStmt* decl, const std::string& name, const ClassInfo* owner);
    void emitConstructor(const ClassInfo& info, const FuncDeclStmt* ctor);
    std::string signature(const Symbol& symbol, const std::string& name, bool method) const;
    void emitMain();

    // ---- Statements ----
    void beginFunction(bool hasTry);
    void endFunction();
    void pushScope() { pushScope(Scope()); }
    void pushScope(Scope scope);
    void popScope(bool emitReleases = true);
    void releaseScopes(size_t downTo, bool popTries);  // releases locals of scopes[downTo..]
    std::string declareLocal(const std::string& name, const SemanticType& type, bool release);
    void stmt(const Stmt* s);
    void block(const Stmt* s) { block(s, Scope()); }
    void block(const Stmt* s, Scope scope);
    void blockBody(const Stmt* s);
    void varDecl(const VarDeclStmt* decl);
    void ifStmt(const IfStmt* s);
    void forStmt(const ForStmt* s);
    void forIn(const ForInStmt* s);
    void whileStmt(const Expr* condition, const Stmt* body);
    void switchStmt(const SwitchStmt* s);
    void tryStmt(const TryStmt* s);
    void returnStmt(const ReturnStmt* s);
    void throwStmt(const ThrowStmt* s);
    void exprStmt(const Expr* e);
    // Writes the statement collected in gen() with its temporaries around it
    void beginTemps();
    std::vector<std::pair<std::string, std::string>> endTemps();
    void declareTemps(const std::vector<std::pair<std::string, std::string>>& list);
    void releaseTemps(const std::vector<std::pair<std::string, std::string>>& list);
    bool hasTry(const Stmt* s) const;
    bool assignsName(const Stmt* s, const std::string& name) const;
    bool assignsNameExpr(const Expr* e, const std::string& name) const;

    // ---- Expressions ----
    CV expr(const Expr* e);
    CV exprAs(const Expr* e, const SemanticType& target);
    CV convert(CV value, const SemanticType& from, const SemanticType& to);
    std::string use(const CV& v, const SemanticType& type);     // borrowed code
    std::string own(const CV& v, const SemanticType& type);     // owned code
    std::string discard(const CV& v, const SemanticType& type); // evaluated for its effect
    CV literalExpr(const LiteralExpr* e);
    CV identifier(const IdentifierExpr* e);
    CV thisValue();
    std::string resolveLocal(const std::string& name, const SemanticType** type = nullptr);
    CV binary(const BinaryExpr* e);
    CV equality(const Expr* l, const Expr* r, bool negate);
    CV comparison(const BinaryExpr* e);
    CV textConcat(const BinaryExpr* e);
    CV assign(const AssignExpr* e);
    std::string lvalue(const Expr* target, SemanticType* type = nullptr);   // C lvalue (address-able)
    CV call(const CallExpr* e);
    CV builtinCall(const CallExpr* e, const Symbol& symbol);
    std::string glueFunction(const std::string& cppName);       // "" if it can't cross the bridge
    std::string glueConstant(const std::string& cppName);
    CV printCall(const CallExpr* e);
    CV methodCall(const CallExpr* e, const MemberExpr* m, const Symbol& symbol);
    CV builtinMethod(const CallExpr* e, const MemberExpr* m, const Symbol& symbol, const std::vector<std::string>& args);
    CV classMethodCall(const ClassInfo* info, const InterfaceInfo* iface, const std::string& object, const std::string& method,
                       const Symbol& symbol, const std::string& args, bool isSuper);
    CV functionValueCall(const CallExpr* e);
    struct CallArgs {
        std::string prelude;              // evaluates arguments with effects in order (or "")
        std::vector<std::string> values;  // in parameter order
    };
    CallArgs arguments(const Expr* call, const std::vector<Argument>& args, const std::vector<FunctionParameterSymbol>& params);
    std::string callWith(const std::string& fn, const CallArgs& args, const std::string& first = "");
    CV member(const MemberExpr* e);
    CV index(const IndexExpr* e);
    CV lambda(const LambdaExpr* e);
    CV functionValue(const Symbol& symbol);
    CV builtinValue(const Symbol& symbol);   // sqrt passed as a Func
    CV newObject(const NewExpr* e);
    CV listLiteral(const std::vector<CollectionElement>& elements, const SemanticType& type);
    CV setLiteral(const std::vector<CollectionElement>& elements, const SemanticType& type);
    CV mapLiteral(const MapExpr* e, const SemanticType& type);
    CV templateString(const TemplateStringExpr* e);
    CV convertExpr(const ConvertExpr* e);
    CV isExpr(const IsExpr* e);
    CV optionalChain(const MemberExpr* m, const CallExpr* call);
    std::string toText(const CV& v, const SemanticType& type, bool forPrint = true);   // an owned ds_str*
    void appendText(std::string& code, const std::string& builder, const Expr* e);
    std::string callFn(const std::string& fn, const SemanticType& fnType, const CallArgs& args);
    std::string sortComparator(const SemanticType& element, const SemanticType& fnType);
};

} // namespace dessa
