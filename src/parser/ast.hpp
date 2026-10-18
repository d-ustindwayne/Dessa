#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "token.hpp"

namespace dessa {

// ============================================================
// Forward declarations
// ============================================================
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ============================================================
// TypeNode
// ------------------------------------------------------------
// A Dessa type annotation, e.g.:
//   Str            -> {name: "Str"}
//   Str<>          -> {name: "Str", isList: true}
//   Int[8]         -> {name: "Int", radix: 8}
//   Int{}          -> {name: "Int", isSet: true}
//   Int | Nil      -> {name: "Int", unionMembers: [{name: "Nil"}]}
// Used for var/const declarations, function return types, function
// parameters, and interface/class field declarations. See parseType()
// in parser.cpp for where this actually gets built.
// ============================================================
struct TypeNode {
    std::string name;               // Str, Int, Bul, Flt, Dbl, Chr, Emp,
                                     // Nil, or a user-defined class name
    bool isList = false;            // Str<>
    std::optional<int> radix;       // Int[2], Int[8], Int[10], Int[16]:
                                     // the display base of the integer.
                                     // Storage is always 64-bit.
    bool isSet = false;             // Int{}
    std::vector<TypeNode> unionMembers; // Int | Nil | Str -> siblings of
                                         // the first member (this node)

    // Func[Str, [Int, Int]] -> {name: "Func", isFunction: true,
    //                           funcReturn: [Str], funcParams: [Int, Int]}
    bool isFunction = false;
    std::vector<TypeNode> funcReturn;   // exactly one element when isFunction
    std::vector<TypeNode> funcParams;

    // (Int | Str)<> and Int<><>: the base type is group[0] instead of `name`
    std::vector<TypeNode> group;

    // Map[Str, Int] -> {name: "Map", isMap: true, mapTypes: [Str, Int]}
    bool isMap = false;
    std::vector<TypeNode> mapTypes;     // key, value
};

// ============================================================
// Supporting value types shared by several nodes
// ============================================================

// A single function/lambda parameter: `num1[Flt]`.
// NOTE: parameters put the type AFTER the name; var/const declarations
// put it BEFORE (`var[Flt] num1`). Easy detail to trip over between
// parseParamList() and parseType()/varDeclaration().
//   names[Str*]         -> variadic: any number of Str arguments, received
//                          as a Str<> (must be the last parameter)
//   greeting[Str]="Hi"  -> default value, used when the argument is left out
struct Param {
    std::string name;
    TypeNode type;
    bool isVariadic = false;
    ExprPtr defaultValue; // nullptr if there is none
    int line = 0;
    int column = 0;
};

// A single call argument, positional or named:
//   add(2, 12.23)              -> both Argument.name empty
//   add(num1: 2, num2: 12.23)  -> Argument.name = "num1" / "num2"
//   greet(...names)            -> isSpread: a list passed to a variadic parameter
struct Argument {
    std::optional<std::string> name;
    ExprPtr value;
    bool isSpread = false;
};

// One element of a list/set literal. Covers the spread form:
//   [...registeredVoters, ...southVoters]
struct CollectionElement {
    bool isSpread = false;
    ExprPtr value;
};

// `{ MyClass }` or `{ MyClass as MC }` inside an import statement.
struct ImportName {
    std::string name;
    std::optional<std::string> alias;
    int line = 0;
    int column = 0;
};

// One module named inside `$(...)`:
//   $("my.other.directory") -> {path: "my.other.directory", isBuiltin: false}
//   $("math")               -> {path: "math",               isBuiltin: true}
// User modules map dots to folders: my/other/directory.ds. A file wins over a
// library of the same name; the loader decides and sets isBuiltin.
struct ModuleSpec {
    std::string path;
    mutable bool isBuiltin = false;
    std::optional<std::string> alias;   // $("math" as m): used as m.sqrt(2)
    int line = 0;
    int column = 0;
};

// One `case`/`default` arm of a switch statement.
// `value == nullptr` marks the `default := { ... }` arm.
struct SwitchCase {
    ExprPtr value;
    StmtPtr body;
};

// ============================================================
// Expr hierarchy
// ------------------------------------------------------------
// NOTE ON DESIGN: no visitor interface here on purpose. The parser only
// needs to BUILD the tree, not walk it. Once the analyser/codegen phase
// needs to traverse these nodes polymorphically, that's the point to
// add either a classic visitor (accept() + an ExprVisitor interface) or
// just switch on `kind`. Better to decide that once you know what the
// analyser actually needs, rather than guess now.
// ============================================================
enum class ExprKind {
    Literal, TemplateString, Identifier, Unary, Postfix, Binary, Assign,
    Ternary, Call, Index, Member, Lambda, New, List, Set, Grouping, Is, Map, Convert
};

struct Expr {
    ExprKind kind;
    int line = 0;   // position of the token that starts the expression,
    int column = 0; // filled in by the parser for diagnostics
    explicit Expr(ExprKind kind) : kind(kind) {}
    virtual ~Expr() = default;
};

// 21, 9.23, 9.23f, 'A', "Hello World", true, false, 0b1001, 0o32, 0xf8, nil
// Keeps the raw Token. The analyser decides which declared types a literal
// may initialise (e.g. an unsuffixed 9.23 fits a Flt) and codegen turns
// the text into the C++ literal.
struct LiteralExpr : Expr {
    Token token;
    explicit LiteralExpr(Token token)
        : Expr(ExprKind::Literal), token(std::move(token)) {}
};

// $"Hello, I am {this.name}. nice to meet you."
// Holds the raw interpolated text as the lexer captured it (braces
// included), PLUS the split-out pieces once the parser has separated
// them. See parseTemplateString() in parser.cpp.
struct TemplateStringExpr : Expr {
    Token token;                            // the raw TEMPLATE_STRING token
    std::vector<std::string> literalParts;  // text between interpolations
    std::vector<ExprPtr> interpolations;    // the {expr} pieces, parsed
    explicit TemplateStringExpr(Token token)
        : Expr(ExprKind::TemplateString), token(std::move(token)) {}
};

// A bare name reference: `a`, `age`, `students`, `ListOfPerson`.
struct IdentifierExpr : Expr {
    Token name;
    explicit IdentifierExpr(Token name)
        : Expr(ExprKind::Identifier), name(std::move(name)) {}
};

// Prefix operators: !no, -x, ~mask
struct UnaryExpr : Expr {
    Token op;
    ExprPtr operand;
    UnaryExpr(Token op, ExprPtr operand)
        : Expr(ExprKind::Unary), op(std::move(op)), operand(std::move(operand)) {}
};

// Postfix operators: num4++, num4--
struct PostfixExpr : Expr {
    Token op;
    ExprPtr operand;
    PostfixExpr(Token op, ExprPtr operand)
        : Expr(ExprKind::Postfix), op(std::move(op)), operand(std::move(operand)) {}
};

// left/op/right -- arithmetic, comparison, logical, bitwise, AND nullish
// coalescing (??) all share this shape. Only the operator token differs,
// so there's no need for a node per operator family.
struct BinaryExpr : Expr {
    ExprPtr left;
    Token op;
    ExprPtr right;
    BinaryExpr(ExprPtr left, Token op, ExprPtr right)
        : Expr(ExprKind::Binary), left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
};

// =, +=, -=, *=, /=, ^=, %=
// `target` is whatever expression was parsed on the left; the analyser
// rejects anything that isn't an identifier, member, or index.
struct AssignExpr : Expr {
    ExprPtr target;
    Token op;
    ExprPtr value;
    AssignExpr(ExprPtr target, Token op, ExprPtr value)
        : Expr(ExprKind::Assign), target(std::move(target)), op(std::move(op)), value(std::move(value)) {}
};

// cond ? thenExpr : elseExpr
struct TernaryExpr : Expr {
    ExprPtr condition;
    ExprPtr thenBranch;
    ExprPtr elseBranch;
    TernaryExpr(ExprPtr condition, ExprPtr thenBranch, ExprPtr elseBranch)
        : Expr(ExprKind::Ternary), condition(std::move(condition)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}
};

// callee(args...) -- print("Hello World"), add(num1: 2, num2: 12.23)
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<Argument> args;
    Token closingParen; // kept for error messages ("... in call at line N")
    CallExpr(ExprPtr callee, std::vector<Argument> args, Token closingParen)
        : Expr(ExprKind::Call), callee(std::move(callee)),
          args(std::move(args)), closingParen(std::move(closingParen)) {}
};

// students<0> -- indexed/keyed access using angle brackets.
// Built by Parser::tryParseIndex(), which also documents the real
// ambiguity between this and a plain `<` comparison now that generics
// aren't a third meaning to worry about.
struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;
    IndexExpr(ExprPtr object, ExprPtr index)
        : Expr(ExprKind::Index), object(std::move(object)), index(std::move(index)) {}
};

// this.name, p.name, Database.parseKey, stng?.config (isOptional=true)
struct MemberExpr : Expr {
    ExprPtr object;
    Token name;
    bool isOptional = false; // ?. vs .
    MemberExpr(ExprPtr object, Token name, bool isOptional)
        : Expr(ExprKind::Member), object(std::move(object)), name(std::move(name)), isOptional(isOptional) {}
};

// (a[Int]) := a ^ 2
// (item[DataKey]) := { item.authenticate.getLicensedKey(); }
// `body` is always a Stmt -- an ExpressionStmt for the single-expression
// form, a BlockStmt for the braced form -- so consumers don't need to
// care which syntax was used to write it.
struct LambdaExpr : Expr {
    std::vector<Param> params;
    StmtPtr body;
    LambdaExpr(std::vector<Param> params, StmtPtr body)
        : Expr(ExprKind::Lambda), params(std::move(params)), body(std::move(body)) {}
};

// new ClassB(), new Setting()
// new Dog("Rex") -- or an anonymous object, new { message = "Hi", code = 200 },
// whose fields are the named args
struct NewExpr : Expr {
    Token className;
    std::vector<Argument> args;
    bool isAnonymous = false;
    NewExpr(Token className, std::vector<Argument> args)
        : Expr(ExprKind::New), className(std::move(className)), args(std::move(args)) {}
};

// ["Dustin", "Dwayne", "Diaz"], [...registeredVoters, ...southVoters]
struct ListExpr : Expr {
    std::vector<CollectionElement> elements;
    explicit ListExpr(std::vector<CollectionElement> elements)
        : Expr(ExprKind::List), elements(std::move(elements)) {}
};

// {1, 5, 2, 7}
struct SetExpr : Expr {
    std::vector<CollectionElement> elements;
    explicit SetExpr(std::vector<CollectionElement> elements)
        : Expr(ExprKind::Set), elements(std::move(elements)) {}
};

// {"Ann": 30, "Bo": 25}, {...other, "Cy": 41}, {:} (empty)
// A spread entry has `key == nullptr` and the map to copy in `value`.
struct MapEntry {
    ExprPtr key;
    ExprPtr value;
};
struct MapExpr : Expr {
    std::vector<MapEntry> entries;
    explicit MapExpr(std::vector<MapEntry> entries)
        : Expr(ExprKind::Map), entries(std::move(entries)) {}
};

// A parenthesized expression used purely to override precedence. NOT
// the same thing as a lambda's `(params)` -- see parseLambdaOrGrouping()
// in parser.cpp for how those are told apart.
// Int("42"), Str(42), Bul(1), Dbl("2.5"), Flt(x), Chr(65) -- a type name
// called like a function converts a value to that type.
struct ConvertExpr : Expr {
    Token type;      // TYPE_INT, TYPE_STR, ...
    ExprPtr value;
    ConvertExpr(Token type, ExprPtr value)
        : Expr(ExprKind::Convert), type(std::move(type)), value(std::move(value)) {}
};

// account is SavingsAccount   /   value is Int   (for unions like Int | Str)
// Inside `if (x is T) { ... }`, x is treated as a T.
struct IsExpr : Expr {
    ExprPtr value;
    TypeNode type;
    IsExpr(ExprPtr value, TypeNode type)
        : Expr(ExprKind::Is), value(std::move(value)), type(std::move(type)) {}
};

struct GroupingExpr : Expr {
    ExprPtr inner;
    explicit GroupingExpr(ExprPtr inner)
        : Expr(ExprKind::Grouping), inner(std::move(inner)) {}
};

// ============================================================
// Stmt hierarchy
// ============================================================
enum class StmtKind {
    Expression, VarDecl, FuncDecl, ClassDecl, EnumDecl, InterfaceDecl,
    Import, If, Switch, For, ForIn, While, Loop, Break, Continue,
    Return, Block, Try, Throw
};

struct Stmt {
    StmtKind kind;
    int line = 0;   // position of the statement's first token,
    int column = 0; // filled in by the parser for diagnostics
    explicit Stmt(StmtKind kind) : kind(kind) {}
    virtual ~Stmt() = default;
};

// Any bare expression followed by ';' -- print(...), a = 6, num4++,
// stng?.config.setScreen(200).
struct ExpressionStmt : Stmt {
    ExprPtr expression;
    explicit ExpressionStmt(ExprPtr expression)
        : Stmt(StmtKind::Expression), expression(std::move(expression)) {}
};

// var[Type] name; / var[Type] name = expr; / const[Type] name = expr;
// `name` is optional only so the parser can recover from a missing name;
// the analyser reports it as an error.
struct VarDeclStmt : Stmt {
    bool isConst;
    TypeNode type;
    std::optional<std::string> name;
    ExprPtr initializer; // nullptr if there's no `= ...`
    VarDeclStmt(bool isConst, TypeNode type, std::optional<std::string> name, ExprPtr initializer)
        : Stmt(StmtKind::VarDecl), isConst(isConst), type(std::move(type)),
          name(std::move(name)), initializer(std::move(initializer)) {}
};

// func[ReturnType] name(params) { body }
// init(params) { body }  -- class constructor: name "init", returnType Emp,
//                           isConstructor = true
// Interface method signatures are FuncDeclStmts with body == nullptr.
struct FuncDeclStmt : Stmt {
    TypeNode returnType;
    std::string name;
    std::vector<Param> params;
    StmtPtr body; // a BlockStmt, or nullptr for an interface signature
    bool isConstructor = false;
    std::vector<Token> annotations;   // @override
    FuncDeclStmt(TypeNode returnType, std::string name, std::vector<Param> params, StmtPtr body)
        : Stmt(StmtKind::FuncDecl), returnType(std::move(returnType)), name(std::move(name)),
          params(std::move(params)), body(std::move(body)) {}
};

// class Name { members }
// class[Interface] Name(Parent) { members }
struct ClassDeclStmt : Stmt {
    std::string name;
    std::optional<std::string> interfaceName;
    std::optional<std::string> parentClassName;
    std::vector<StmtPtr> members; // VarDeclStmt / FuncDeclStmt entries

    ClassDeclStmt(std::string name, 
                  std::optional<std::string> interfaceName,
                  std::optional<std::string> parentClassName, 
                  std::vector<StmtPtr> members)
        : Stmt(StmtKind::ClassDecl), 
          name(std::move(name)), 
          interfaceName(std::move(interfaceName)),
          parentClassName(std::move(parentClassName)), 
          members(std::move(members)) {}
};

// enum Status { CANCELLED, ONGOING, COMPLETED }
struct EnumEntry {
    std::string name;
    std::optional<ExprPtr> value;
};

struct EnumDeclStmt : Stmt {
    std::string name;
    std::vector<EnumEntry> values;

    EnumDeclStmt(std::string name, std::vector<EnumEntry> values)
        : Stmt(StmtKind::EnumDecl),
          name(std::move(name)),
          values(std::move(values)) {}
};

struct InterfaceDeclStmt : Stmt {
    std::string name;
    std::vector<StmtPtr> members;

    InterfaceDeclStmt(std::string name, std::vector<StmtPtr> members)
        : Stmt(StmtKind::InterfaceDecl),
          name(std::move(name)),
          members(std::move(members)) {}
};

// $("my.other.directory") { MyClass }
// $("my.other.directory") { MyClass as MC }
// $("java.util", "java.sql", "my.other");   -> importAll, every top-level
//                                              declaration of each module
// $("math") { sqrt }                   -> builtin library
struct ImportStmt : Stmt {
    std::vector<ImportName> names; // empty when importAll
    std::vector<ModuleSpec> modules;
    bool importAll = false;
    ImportStmt(std::vector<ImportName> names, std::vector<ModuleSpec> modules, bool importAll)
        : Stmt(StmtKind::Import), names(std::move(names)), modules(std::move(modules)), importAll(importAll) {}
};

// if (cond) { ... } else if (cond2) { ... } else { ... }
// `elseBranch` may be another IfStmt (the else-if chain), a plain
// BlockStmt (the final else), or nullptr if there's no else at all.
struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // nullable
    IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch)
        : Stmt(StmtKind::If), condition(std::move(condition)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}
};

// switch (discriminant) { case 'A' := { ... } ... default := { ... } }
// No implicit fall-through between cases.
struct SwitchStmt : Stmt {
    ExprPtr discriminant;
    std::vector<SwitchCase> cases;
    SwitchStmt(ExprPtr discriminant, std::vector<SwitchCase> cases)
        : Stmt(StmtKind::Switch), discriminant(std::move(discriminant)), cases(std::move(cases)) {}
};

// for (init; condition; increment) { body }
struct ForStmt : Stmt {
    StmtPtr init;      // nullable -- e.g. var[Int] i = 0
    ExprPtr condition; // nullable
    ExprPtr increment; // nullable
    StmtPtr body;
    ForStmt(StmtPtr init, ExprPtr condition, ExprPtr increment, StmtPtr body)
        : Stmt(StmtKind::For), init(std::move(init)), condition(std::move(condition)),
          increment(std::move(increment)), body(std::move(body)) {}
};

// for (var[Person] p in ListOfPerson) { body }
struct ForInStmt : Stmt {
    StmtPtr declaration; // a VarDeclStmt for the loop variable
    ExprPtr iterable;
    StmtPtr body;
    ForInStmt(StmtPtr declaration, ExprPtr iterable, StmtPtr body)
        : Stmt(StmtKind::ForIn), declaration(std::move(declaration)),
          iterable(std::move(iterable)), body(std::move(body)) {}
};

// while (condition) { body }
struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(ExprPtr condition, StmtPtr body)
        : Stmt(StmtKind::While), condition(std::move(condition)), body(std::move(body)) {}
};

// loop { body } -- infinite loop, exited via an ordinary `break` inside
struct LoopStmt : Stmt {
    StmtPtr body;
    explicit LoopStmt(StmtPtr body)
        : Stmt(StmtKind::Loop), body(std::move(body)) {}
};

struct BreakStmt : Stmt {
    BreakStmt() : Stmt(StmtKind::Break) {}
};

struct ContinueStmt : Stmt {
    ContinueStmt() : Stmt(StmtKind::Continue) {}
};

// return; / return expr;
struct ReturnStmt : Stmt {
    ExprPtr value; // nullptr for a bare `return;`
    explicit ReturnStmt(ExprPtr value)
        : Stmt(StmtKind::Return), value(std::move(value)) {}
};

// { statement* } -- function/if/while/for/loop/class bodies.
struct BlockStmt : Stmt {
    std::vector<StmtPtr> statements;
    explicit BlockStmt(std::vector<StmtPtr> statements)
        : Stmt(StmtKind::Block), statements(std::move(statements)) {}
};

// One `catch` arm:  catch (e[NotFound]) { ... }  or  catch { ... }
struct CatchClause {
    std::optional<std::string> param; // the 'e' in catch (e[NotFound])
    std::optional<TypeNode> type;     // the [NotFound]; absent for a catch-all
    StmtPtr body;
    int line = 0;                     // position of `catch`, for diagnostics
    int column = 0;
};

// try { ... } catch (a[A]) { ... } catch (e[Str]) { ... } catch { ... }
// Arms are tried in order; there is no `finally`.
struct TryStmt : Stmt {
    StmtPtr tryBlock;
    std::vector<CatchClause> catches;
    TryStmt(StmtPtr tryBlock, std::vector<CatchClause> catches)
        : Stmt(StmtKind::Try), tryBlock(std::move(tryBlock)), catches(std::move(catches)) {}
};

struct ThrowStmt : Stmt {
    ExprPtr value;
    explicit ThrowStmt(ExprPtr value) : Stmt(StmtKind::Throw), value(std::move(value)) {}
};

} // namespace dessa