/***
 * The tree the parser builds: a twin of src/parser/ast.hpp. A class per kind
 * of node, told apart with `is`. What the C++ tree holds as a null pointer or
 * an optional is `| Nil` here.
 */

$("lexer") { Token }

// A type as written: Str, Int<>, Int[16], Int | Nil, Func[Str, [Int]], Map[K, V], Box:Int
class TypeNode {
    var[Str] name = "";
    var[Bul] isList = false;
    var[Int] radix = -1;   // Int[16]; -1: none
    var[Bul] isSet = false;
    var[TypeNode<>] unionMembers = <>;
    var[Bul] isFunction = false;
    var[TypeNode<>] funcReturn = <>;
    var[TypeNode<>] funcParams = <>;
    var[TypeNode<>] group = <>;   // (Int | Str)<>: the base type is group<0>
    var[Bul] isMap = false;
    var[TypeNode<>] mapTypes = <>;
    var[TypeNode<>] typeArgs = <>;
}

// ---- Expressions ----

class Expr {
    var[Int] line = 0;
    var[Int] column = 0;
    var[Int] chain = 0;   // how many operators deep (Parser.checkChain)
}

// name[Type], rest[Type*], greeting[Str] = "Hi"
class Param {
    var[Str] name = "";
    var[TypeNode] type = new TypeNode();
    var[Bul] isVariadic = false;
    var[Expr | Nil] defaultValue = nil;
    var[Int] line = 0;
    var[Int] column = 0;
}

// f(x), f(name = x), f(...xs)
class Argument {
    var[Str | Nil] name = nil;
    var[Expr] value = new Expr();
    var[Bul] isSpread = false;
    var[Bul] oldNamed = false;   // written name: value
}

class CollectionElement {
    var[Bul] isSpread = false;
    var[Expr] value = new Expr();
    init(isSpread[Bul], value[Expr]) {
        this.isSpread = isSpread;
        this.value = value;
    }
}

class LiteralExpr(Expr) {
    var[Token] token;
    init(token[Token]) { this.token = token; }
}

class TemplateStringExpr(Expr) {
    var[Token] token;
    var[Str<>] literalParts = <>;
    var[Expr<>] interpolations = <>;
    init(token[Token]) { this.token = token; }
}

class IdentifierExpr(Expr) {
    var[Token] name;
    var[TypeNode<>] typeArgs = <>;   // pick:Int(...)
    init(name[Token]) { this.name = name; }
}

class UnaryExpr(Expr) {
    var[Token] op;
    var[Expr] operand;
    init(op[Token], operand[Expr]) {
        this.op = op;
        this.operand = operand;
    }
}

class PostfixExpr(Expr) {
    var[Token] op;
    var[Expr] operand;
    init(op[Token], operand[Expr]) {
        this.op = op;
        this.operand = operand;
    }
}

class BinaryExpr(Expr) {
    var[Expr] left;
    var[Token] op;
    var[Expr] right;
    init(left[Expr], op[Token], right[Expr]) {
        this.left = left;
        this.op = op;
        this.right = right;
    }
}

class AssignExpr(Expr) {
    var[Expr] target;
    var[Token] op;
    var[Expr] value;
    init(target[Expr], op[Token], value[Expr]) {
        this.target = target;
        this.op = op;
        this.value = value;
    }
}

class TernaryExpr(Expr) {
    var[Expr] condition;
    var[Expr] thenBranch;
    var[Expr] elseBranch;
    init(condition[Expr], thenBranch[Expr], elseBranch[Expr]) {
        this.condition = condition;
        this.thenBranch = thenBranch;
        this.elseBranch = elseBranch;
    }
}

class CallExpr(Expr) {
    var[Expr] callee;
    var[Argument<>] args;
    var[Token] closingParen;
    init(callee[Expr], args[Argument<>], closingParen[Token]) {
        this.callee = callee;
        this.args = args;
        this.closingParen = closingParen;
    }
}

class IndexExpr(Expr) {
    var[Expr] object;
    var[Expr] index;
    init(object[Expr], index[Expr]) {
        this.object = object;
        this.index = index;
    }
}

class MemberExpr(Expr) {
    var[Expr] object;
    var[Token] name;
    var[TypeNode<>] typeArgs = <>;   // db.all:Person(...)
    var[Bul] isOptional = false;     // ?.
    init(object[Expr], name[Token], isOptional[Bul]) {
        this.object = object;
        this.name = name;
        this.isOptional = isOptional;
    }
}

class LambdaExpr(Expr) {
    var[Param<>] params;
    var[Stmt] body;
    init(params[Param<>], body[Stmt]) {
        this.params = params;
        this.body = body;
    }
}

class MarkupAttribute {
    var[Str] name = "";
    var[Expr | Nil] value = nil;
    var[Bul] isText = false;
    var[Int] line = 0;
    var[Int] column = 0;
}

enum MarkupKind { ELEMENT, TEXT, VALUE }

class MarkupNode {
    var[MarkupKind] kind = MarkupKind.ELEMENT;
    var[Str] tag = "";
    var[MarkupAttribute<>] attributes = <>;
    var[MarkupNode<>] children = <>;
    var[Str] text = "";
    var[Expr | Nil] value = nil;
    var[Int] line = 0;
    var[Int] column = 0;
}

class AwaitExpr(Expr) {
    var[Expr] operand;
    init(operand[Expr]) { this.operand = operand; }
}

class MarkupExpr(Expr) {
    var[MarkupNode] root;
    init(root[MarkupNode]) { this.root = root; }
}

class NewExpr(Expr) {
    var[Token] className;
    var[TypeNode<>] typeArgs = <>;
    var[Argument<>] args;
    var[Bul] isAnonymous = false;
    var[(TypeNode | Nil)<>] fieldTypes = <>;   // new { var[Str] name = "x" }
    init(className[Token], args[Argument<>]) {
        this.className = className;
        this.args = args;
    }
}

class ListExpr(Expr) {
    var[CollectionElement<>] elements;
    var[Bul] oldBrackets = false;   // [1, 2]
    init(elements[CollectionElement<>]) { this.elements = elements; }
}

class SetExpr(Expr) {
    var[CollectionElement<>] elements;
    init(elements[CollectionElement<>]) { this.elements = elements; }
}

// A spread has no key, and the map to copy in as its value
class MapEntry {
    var[Expr | Nil] key;
    var[Expr] value;
    init(key[Expr | Nil], value[Expr]) {
        this.key = key;
        this.value = value;
    }
}

class MapExpr(Expr) {
    var[MapEntry<>] entries;
    init(entries[MapEntry<>]) { this.entries = entries; }
}

class ConvertExpr(Expr) {
    var[Token] type;
    var[Expr] value;
    init(type[Token], value[Expr]) {
        this.type = type;
        this.value = value;
    }
}

class IsExpr(Expr) {
    var[Expr] value;
    var[TypeNode] type;
    init(value[Expr], type[TypeNode]) {
        this.value = value;
        this.type = type;
    }
}

class GroupingExpr(Expr) {
    var[Expr] inner;
    init(inner[Expr]) { this.inner = inner; }
}

// ---- Statements ----

class Stmt {
    var[Int] line = 0;
    var[Int] column = 0;
}

class ExpressionStmt(Stmt) {
    var[Expr] expression;
    init(expression[Expr]) { this.expression = expression; }
}

class VarDeclStmt(Stmt) {
    var[Bul] isConst;
    var[TypeNode] type;
    var[Str | Nil] name;
    var[Expr | Nil] initializer;
    init(isConst[Bul], type[TypeNode], name[Str | Nil], initializer[Expr | Nil]) {
        this.isConst = isConst;
        this.type = type;
        this.name = name;
        this.initializer = initializer;
    }
}

class FuncDeclStmt(Stmt) {
    var[TypeNode] returnType;
    var[Str] name;
    var[Param<>] params;
    var[Stmt | Nil] body;   // nil: an interface's method
    var[Bul] isConstructor = false;
    var[Bul] isComp = false;
    var[Bul] isAsync = false;
    var[TypeNode] asyncResult = new TypeNode();
    var[Token<>] annotations = <>;   // @override
    var[TypeNode<>] throwsTypes = <>;
    var[Str<>] typeParams = <>;      // func[T] first:T(...)
    var[Token<>] templateTokens = <>;
    init(returnType[TypeNode], name[Str], params[Param<>], body[Stmt | Nil]) {
        this.returnType = returnType;
        this.name = name;
        this.params = params;
        this.body = body;
    }
}

class ClassDeclStmt(Stmt) {
    var[Str] name;
    var[Str | Nil] interfaceName;
    var[Str | Nil] parentClassName;
    var[TypeNode | Nil] parentType = nil;   // class IntBox(Box:Int)
    var[Stmt<>] members;
    var[Str<>] typeParams = <>;
    var[Token<>] templateTokens = <>;
    var[Stmt<>] methodTemplates = <>;
    init(name[Str], interfaceName[Str | Nil], parentClassName[Str | Nil], members[Stmt<>]) {
        this.name = name;
        this.interfaceName = interfaceName;
        this.parentClassName = parentClassName;
        this.members = members;
    }
}

class EnumEntry {
    var[Str] name;
    var[Expr | Nil] value;
    init(name[Str], value[Expr | Nil]) {
        this.name = name;
        this.value = value;
    }
}

class EnumDeclStmt(Stmt) {
    var[Str] name;
    var[EnumEntry<>] values;
    init(name[Str], values[EnumEntry<>]) {
        this.name = name;
        this.values = values;
    }
}

class InterfaceDeclStmt(Stmt) {
    var[Str] name;
    var[Stmt<>] members;
    init(name[Str], members[Stmt<>]) {
        this.name = name;
        this.members = members;
    }
}

class ImportName {
    var[Str] name = "";
    var[Str | Nil] alias = nil;
    var[Int] line = 0;
    var[Int] column = 0;
}

class ModuleSpec {
    var[Str] path = "";
    var[Bul] isBuiltin = false;
    var[Bul] isPrelude = false;
    var[Bul] isPageLibrary = false;
    var[Bul] isHidden = false;
    var[Str | Nil] alias = nil;
    var[Int] line = 0;
    var[Int] column = 0;
}

class ImportStmt(Stmt) {
    var[ImportName<>] names;
    var[ModuleSpec<>] modules;
    var[Bul] importAll;
    init(names[ImportName<>], modules[ModuleSpec<>], importAll[Bul]) {
        this.names = names;
        this.modules = modules;
        this.importAll = importAll;
    }
}

class IfStmt(Stmt) {
    var[Expr] condition;
    var[Stmt | Nil] thenBranch;
    var[Stmt | Nil] elseBranch;
    init(condition[Expr], thenBranch[Stmt | Nil], elseBranch[Stmt | Nil]) {
        this.condition = condition;
        this.thenBranch = thenBranch;
        this.elseBranch = elseBranch;
    }
}

// `value` nil: the default arm
class SwitchCase {
    var[Expr | Nil] value;
    var[Stmt] body;
    init(value[Expr | Nil], body[Stmt]) {
        this.value = value;
        this.body = body;
    }
}

class SwitchStmt(Stmt) {
    var[Expr] discriminant;
    var[SwitchCase<>] cases;
    init(discriminant[Expr], cases[SwitchCase<>]) {
        this.discriminant = discriminant;
        this.cases = cases;
    }
}

class ForStmt(Stmt) {
    var[Stmt | Nil] init_;
    var[Expr | Nil] condition;
    var[Expr | Nil] increment;
    var[Stmt | Nil] body;
    init(start[Stmt | Nil], condition[Expr | Nil], increment[Expr | Nil], body[Stmt | Nil]) {
        this.init_ = start;
        this.condition = condition;
        this.increment = increment;
        this.body = body;
    }
}

class ForInStmt(Stmt) {
    var[Stmt] declaration;
    var[Expr] iterable;
    var[Stmt | Nil] body;
    init(declaration[Stmt], iterable[Expr], body[Stmt | Nil]) {
        this.declaration = declaration;
        this.iterable = iterable;
        this.body = body;
    }
}

class WhileStmt(Stmt) {
    var[Expr] condition;
    var[Stmt | Nil] body;
    init(condition[Expr], body[Stmt | Nil]) {
        this.condition = condition;
        this.body = body;
    }
}

class LoopStmt(Stmt) {
    var[Stmt] body;
    init(body[Stmt]) { this.body = body; }
}

class BreakStmt(Stmt) {}
class ContinueStmt(Stmt) {}

class ReturnStmt(Stmt) {
    var[Expr | Nil] value;
    var[Bul] isMarkup = false;
    init(value[Expr | Nil]) { this.value = value; }
}

class BlockStmt(Stmt) {
    var[Stmt<>] statements;
    init(statements[Stmt<>]) { this.statements = statements; }
}

class CatchClause {
    var[Str | Nil] param = nil;
    var[TypeNode | Nil] type = nil;
    var[Stmt] body = new Stmt();
    var[Int] line = 0;
    var[Int] column = 0;
}

class TryStmt(Stmt) {
    var[Stmt] tryBlock;
    var[CatchClause<>] catches;
    init(tryBlock[Stmt], catches[CatchClause<>]) {
        this.tryBlock = tryBlock;
        this.catches = catches;
    }
}

class ThrowStmt(Stmt) {
    var[Expr] value;
    init(value[Expr]) { this.value = value; }
}
