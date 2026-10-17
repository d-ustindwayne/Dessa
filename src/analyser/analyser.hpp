// src/analyser/analyser.hpp
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ast.hpp"
#include "program.hpp"
#include "scope.hpp"
#include "semtype.hpp"

namespace dessa {

struct ClassInfo {
    std::string name;
    int moduleId = 0;
    const ClassDeclStmt* decl = nullptr;
    std::optional<SemanticType> parent;
    std::optional<SemanticType> interface;
    std::vector<std::string> fieldOrder;                // declaration order
    std::unordered_map<std::string, Symbol> fields;     // own fields only
    std::unordered_map<std::string, Symbol> methods;    // own methods only
    std::optional<Symbol> constructor;                  // init(...)
    SemanticType type() const { return SemanticType::userDefined(name, moduleId, UserTypeKind::Class); }
};

struct InterfaceInfo {
    std::string name;
    int moduleId = 0;
    const InterfaceDeclStmt* decl = nullptr;
    std::vector<std::string> fieldOrder;
    std::unordered_map<std::string, SemanticType> fields;
    std::vector<std::string> methodOrder;
    std::unordered_map<std::string, Symbol> methods;
    SemanticType type() const { return SemanticType::userDefined(name, moduleId, UserTypeKind::Interface); }
};

struct EnumInfo {
    std::string name;
    int moduleId = 0;
    const EnumDeclStmt* decl = nullptr;
    std::vector<std::string> values;
    SemanticType type() const { return SemanticType::userDefined(name, moduleId, UserTypeKind::Enum); }
};

// For each parameter of a call: the argument indexes that supply it.
using ArgumentOrder = std::vector<std::vector<int>>;

class Analyser {
public:
    Analyser();
    ~Analyser();

    void analyse(const Program& program);

    bool hasErrors() const;
    const std::vector<Diagnostic>& getDiagnostics() const;

    // ---- Results consumed by codegen ----
    // Type of an expression after narrowing (e.g. `a` after `a = 6` is Int,
    // even though it was declared Int | Nil).
    const SemanticType* typeOf(const Expr* expression) const;
    // The type an expression is being converted to (variable initialiser,
    // argument, return value, ...), when there is one.
    const SemanticType* expectedTypeOf(const Expr* expression) const;
    // The symbol an IdentifierExpr refers to.
    const Symbol* symbolOf(const Expr* expression) const;
    // The member a MemberExpr refers to (field, method or builtin method).
    const Symbol* memberOf(const Expr* expression) const;
    // `a?.b.c`: `.c` is evaluated as optional too because `a?.b` may be nil.
    bool isOptionalChain(const Expr* member) const;
    // For a CallExpr/NewExpr: for each parameter, the indexes of the
    // arguments that supply it -- usually one, several for a variadic
    // parameter, none when the default value is used.
    const ArgumentOrder* argumentOrder(const Expr* call) const;
    // Resolved declared type of a VarDeclStmt (also for-in loop variables).
    const SemanticType* declaredTypeOf(const Stmt* statement) const;
    // Resolved function/method/constructor signature.
    const Symbol* functionOf(const FuncDeclStmt* declaration) const;
    // Resolved type of a catch arm (absent for catch-all).
    const SemanticType* catchTypeOf(const CatchClause* clause) const;
    // The resolved type on the right of `x is T`.
    const SemanticType* isTargetOf(const IsExpr* expression) const;
    // A lambda stored in a variable that calls itself, and the identifiers
    // inside it that refer back to that variable.
    bool isRecursiveLambda(const VarDeclStmt* declaration) const { return recursiveLambdas.count(declaration) != 0; }
    bool isRecursiveRef(const Expr* identifier) const { return recursiveRefs.count(identifier) != 0; }
    // `case RED` in a switch on an enum: the enum it belongs to.
    const SemanticType* enumCaseOf(const Expr* caseValue) const {
        auto it = enumCases.find(caseValue);
        return it == enumCases.end() ? nullptr : &it->second;
    }

    const ClassInfo* findClass(const SemanticType& type) const;
    const InterfaceInfo* findInterface(const SemanticType& type) const;
    const EnumInfo* findEnum(const SemanticType& type) const;
    const std::vector<std::string>& classOrder() const { return classKeys; }       // parents first
    const std::vector<std::string>& interfaceOrder() const { return interfaceKeys; }
    const std::vector<std::string>& enumOrder() const { return enumKeys; }
    const std::set<std::string>& builtinHeaders() const { return usedHeaders; }
    // Names of the builtin libraries the program imports (for link flags).
    const std::set<std::string>& builtinLibrariesUsed() const { return usedLibraries; }

    // ---- Editor support (dessa complete / hover) ----
    // Completion: the editor puts `placeholder` at the cursor (`obj.<placeholder>`
    // or just `<placeholder>`); analysis records what can go there.
    struct CompletionProbe {
        bool found = false;
        bool isMember = false;              // obj.|  (else a plain name)
        SemanticType objectType = SemanticType::unknown();
        bool objectIsType = false;          // Color.|  (enum values)
        bool objectIsThis = false;          // this.|   (private _members too)
        std::vector<Symbol> visible;        // plain name: everything in scope, innermost first
    };
    // Hover: the identifier / member name / declaration at a position of a module.
    struct HoverProbe {
        bool found = false;
        Symbol symbol;
        SemanticType type = SemanticType::unknown();   // after narrowing
        std::string memberOf;                          // "Str", "Customer", ... for obj.name
    };
    void setCompletionProbe(const std::string& placeholder) { probeName = placeholder; }
    void setHoverProbe(int moduleId, int line, int column) { hoverModule = moduleId; hoverLine = line; hoverColumn = column; }
    const CompletionProbe& completionProbe() const { return completion; }
    const HoverProbe& hoverProbe() const { return hover; }
    // Fields and methods of a value of this type (own, inherited and builtin).
    std::vector<Symbol> membersOf(const SemanticType& type) const;

    // Top-level symbols declared by a module (functions, classes, vars, ...)
    const std::unordered_map<std::string, Symbol>& exportsOf(int moduleId) const;
    // Everything visible at a module's top level (its own globals, imports
    // and, through parent(), the global builtins).
    const Scope* moduleScope(int moduleId) const;

private:
    struct ModuleState {
        std::unique_ptr<Scope> scope;
        std::unordered_map<std::string, Symbol> exports;
    };

    const Program* program = nullptr;
    std::unique_ptr<Scope> builtinScope;
    std::vector<ModuleState> modules;
    Scope* currentScope = nullptr;
    int currentModule = 0;

    std::vector<Diagnostic> diagnostics;

    std::unordered_map<std::string, ClassInfo> classes;
    std::unordered_map<std::string, InterfaceInfo> interfaces;
    std::unordered_map<std::string, EnumInfo> enums;
    std::vector<std::string> classKeys;
    std::vector<std::string> interfaceKeys;
    std::vector<std::string> enumKeys;
    std::set<std::string> usedHeaders;
    std::set<std::string> usedLibraries;

    // Function context
    int loopDepth = 0;
    int functionDepth = 0;
    std::optional<SemanticType> currentReturnType;
    bool inferringReturn = false;                 // block lambda without declared return
    std::optional<SemanticType> inferredReturn;
    std::string currentFunctionName;
    std::string currentClassKey;                  // "" outside classes
    bool inConstructor = false;
    const VarDeclStmt* definingLambda = nullptr;  // `fact` while analysing fact's lambda

    // Results
    std::unordered_map<const Expr*, SemanticType> exprTypes;
    std::unordered_map<const Expr*, SemanticType> expectedTypes;
    std::unordered_map<const Expr*, Symbol> identSymbols;
    std::unordered_map<const Expr*, Symbol> memberSymbols;
    std::unordered_set<const Expr*> optionalChains;
    std::unordered_map<const Expr*, ArgumentOrder> argOrders;
    std::unordered_map<const Stmt*, SemanticType> declTypes;
    std::unordered_map<const FuncDeclStmt*, Symbol> functionSymbols;
    std::unordered_map<const CatchClause*, SemanticType> catchTypes;
    std::unordered_map<const IsExpr*, SemanticType> isTargets;
    std::unordered_set<const VarDeclStmt*> recursiveLambdas;
    std::unordered_map<const Expr*, SemanticType> enumCases;
    std::unordered_set<const Expr*> recursiveRefs;

    // Editor probes
    std::string probeName;
    CompletionProbe completion;
    int hoverModule = -1, hoverLine = 0, hoverColumn = 0;
    HoverProbe hover;
    bool hoverHits(const Token& name) const;
    void recordHover(const Token& name, const Symbol& symbol, const SemanticType& type, const std::string& memberOf = "");

    // ---- Diagnostics ----
    void report(DiagnosticSeverity severity, int line, int column, const std::string& message);
    void error(const Expr* at, const std::string& message);
    void error(const Stmt* at, const std::string& message);
    void error(int line, int column, const std::string& message);
    void warning(const Expr* at, const std::string& message);

    // ---- Pipeline ----
    void declareTypeNames(const Module& module);
    void processImports(const Module& module);
    void importSymbol(const Symbol& symbol, const std::string& localName, int line, int column, int importId);
    void reportUnusedImports(const Module& module);

    // One imported name, or one whole `$("x");`, and whether the code uses it
    struct ImportRecord {
        int moduleId = 0;
        const Stmt* statement = nullptr;
        std::string name;        // "" for `$("x");`
        int line = 0;
        int column = 0;
        int length = 0;
        bool used = false;
    };
    std::vector<ImportRecord> importRecords;
    void collectTypeMembers(const Module& module);
    void collectClassMembers(ClassInfo& info);
    void collectInterfaceMembers(InterfaceInfo& info);
    void collectFunctionSignatures(const Module& module);
    void collectExports(const Module& module);
    void checkClassHierarchy(const Module& module);
    void analyseModuleBody(const Module& module);

    // ---- Scopes ----
    void beginScope(ScopeKind kind);
    void endScope();
    bool declareSymbol(Symbol symbol, int line, int column);
    Symbol* resolveSymbol(const std::string& name);
    // o.name on an Obj: looked up when the program runs
    std::unordered_set<const Expr*> dynamicMembers;
public:
    bool isDynamicMember(const Expr* e) const { return dynamicMembers.count(e) != 0; }
private:
    void narrowAfterAssignment(const std::string& name, const SemanticType& valueType);

    // ---- Types ----
    SemanticType resolveType(const TypeNode& typeNode, int line, int column);
    SemanticType resolveTypeName(const std::string& name, int line, int column);
    Symbol makeFunctionSymbol(const FuncDeclStmt* declaration, SymbolKind kind);
    void checkDefaultValue(const Expr* value, const SemanticType& type, const std::string& name);
    const VarDeclStmt* findModuleConstant(const std::string& name) const;
    bool isIntDivision(const Expr* expression) const;
    bool isWholeNumberMath(const Expr* expression) const;
    bool isAssignable(const SemanticType& target, const SemanticType& value) const;
    bool isComparable(const SemanticType& left, const SemanticType& right) const;
    bool isSubclassOf(const SemanticType& derived, const SemanticType& base) const;
    bool implementsInterface(const SemanticType& klass, const SemanticType& iface) const;
    SemanticType commonType(const SemanticType& a, const SemanticType& b) const;
    // isAssignable plus literal conversions (9.23 -> Flt). Records the
    // expected type for codegen. Reports `context` on failure.
    bool checkConversion(const SemanticType& target, const Expr* value, const SemanticType& valueType, const std::string& context);

    // ---- Statements ----
    void analyseStatement(const Stmt* statement);
    void analyseBlock(const BlockStmt* statement, ScopeKind kind = ScopeKind::Block);
    void analyseBranch(const Stmt* statement, const std::vector<std::pair<std::string, SemanticType>>& narrowings);
    void analyseVariableDeclaration(const VarDeclStmt* statement);
    void analyseFunctionBody(const FuncDeclStmt* statement);
    void analyseClassBody(const ClassDeclStmt* statement);
    void analyseIf(const IfStmt* statement);
    void analyseSwitch(const SwitchStmt* statement);
    void analyseFor(const ForStmt* statement);
    void analyseForIn(const ForInStmt* statement);
    void analyseWhile(const WhileStmt* statement);
    void analyseLoop(const LoopStmt* statement);
    void analyseReturn(const ReturnStmt* statement);
    void analyseTry(const TryStmt* statement);
    void analyseThrow(const ThrowStmt* statement);
    void forgetNarrowings(const std::unordered_set<std::string>& names);
    void analyseLoopBody(const Stmt* body, const std::vector<std::pair<std::string, SemanticType>>& narrowings = {});

    // ---- Expressions ----
    SemanticType analyseExpression(const Expr* expression, bool allowTypeName = false);
    SemanticType analyseExpressionInner(const Expr* expression, bool allowTypeName);
    SemanticType analyseLiteral(const LiteralExpr* expression);
    SemanticType analyseIs(const IsExpr* expression);
    SemanticType analyseMapLiteral(const MapExpr* expression);
    SemanticType analyseConvert(const ConvertExpr* expression);
    SemanticType analyseTemplateString(const TemplateStringExpr* expression);
    SemanticType analyseIdentifier(const IdentifierExpr* expression, bool allowTypeName);
    SemanticType analyseUnary(const UnaryExpr* expression);
    SemanticType analysePostfix(const PostfixExpr* expression);
    SemanticType analyseBinary(const BinaryExpr* expression);
    SemanticType analyseAssignment(const AssignExpr* expression);
    SemanticType analyseAssignmentTarget(const Expr* target);
    SemanticType analyseTernary(const TernaryExpr* expression);
    SemanticType analyseCall(const CallExpr* expression);
    SemanticType analyseZip(const CallExpr* expression, const Symbol& symbol);
    SemanticType analyseIndex(const IndexExpr* expression);
    SemanticType analyseMember(const MemberExpr* expression);
    SemanticType analyseLambda(const LambdaExpr* expression, std::optional<SemanticType> declaredReturn = std::nullopt);
    SemanticType analyseNew(const NewExpr* expression);
    SemanticType analyseCollection(const std::vector<CollectionElement>& elements, bool isSet, const Expr* at);

    SemanticType binaryResult(const Token& op, const SemanticType& left, const SemanticType& right, const Expr* at);

    // Narrowing facts implied by a condition being true / false.
    using Narrowings = std::vector<std::pair<std::string, SemanticType>>;
    void conditionNarrowings(const Expr* condition, Narrowings& whenTrue, Narrowings& whenFalse);
    SemanticType analyseCondition(const Expr* condition, const std::string& what);

    // ---- Members ----
    const Symbol* findMember(const SemanticType& objectType, const std::string& name) const;
    const Symbol* findClassMember(const std::string& classKey, const std::string& name) const;
    std::optional<Symbol> builtinMethod(const SemanticType& receiver, const std::string& name) const;
    const ClassInfo* currentClass() const;

    // ---- Calls ----
    ArgumentOrder checkArguments(const std::string& calleeName,
                                    const std::vector<FunctionParameterSymbol>& parameters,
                                    const std::vector<Argument>& args,
                                    const Expr* at,
                                    bool variadic = false,
                                    std::vector<SemanticType>* argTypes = nullptr);
    SemanticType callResult(const Symbol& callee, const std::vector<SemanticType>& argTypes) const;

    // ---- Control flow ----
    bool statementDefinitelyExits(const Stmt* statement, bool countLoopJumps = true) const;
    bool blockDefinitelyExits(const BlockStmt* block, bool countLoopJumps = true) const;
    bool isStubBody(const Stmt* body) const;
    bool isAssignableExpression(const Expr* expression) const;
};

} // namespace dessa
