#include "parser.hpp"
#include "lexer.hpp"
#include <algorithm>
#include <utility>

namespace dessa {

namespace {

bool isReserved(TokenType type) {
    switch (type) {
        case TokenType::WITH:
        case TokenType::FROM:
            return true;
        default:
            return false;
    }
}

// Turns the escape sequences of a template-string literal part into the
// characters they stand for. `\{` and `\}` produce literal braces.
std::string unescape(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out += raw[i];
            continue;
        }
        char e = raw[++i];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '0': out += '\0'; break;
            case 'x':
                if (i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
                    out += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    out += 'x';
                }
                break;
            default:  out += e; break;
        }
    }
    return out;
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

bool Parser::hasErrors() const {
    return !errors.empty();
}

const std::vector<std::string>& Parser::getErrors() const {
    return errors;
}

std::vector<StmtPtr> Parser::parseProgram() {
    std::vector<StmtPtr> statements;
    while (!isAtEnd()) {
        StmtPtr stmt = declaration();
        if (stmt) statements.push_back(std::move(stmt));
    }
    return statements;
}

// ============================================================
// Token plumbing
// ============================================================

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

const Token& Parser::peek() const {
    return tokens[current];
}

const Token& Parser::peekAt(size_t offset) const {
    size_t index = current + offset;
    return index < tokens.size() ? tokens[index] : tokens.back();
}

const Token& Parser::previous() const {
    return tokens[current - 1];
}

const Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return type == TokenType::END_OF_FILE;
    return peek().type == type;
}

bool Parser::checkNext(TokenType type) const {
    if (isAtEnd()) return false;
    return peekAt(1).type == type;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    if (type == TokenType::SEMICOLON && current > 0 && previous().line < peek().line) {
        // `var[Int] g = 3` <newline> `var ...`: point at the line missing the ';'
        Token at = previous();
        at.column += static_cast<int>(at.lexeme.size());
        throw error(at, message);
    }
    if (isReserved(peek().type)) {
        throw error(peek(), "'" + peek().lexeme + "' is reserved for future use.");
    }
    throw error(peek(), message);
}

ParseError Parser::error(const Token& token, const std::string& message) {
    std::string formattedError = "[line " + std::to_string(token.line) + ", col " + std::to_string(token.column) + "] Parse error";
    if (token.type == TokenType::END_OF_FILE) {
        formattedError += " at end: " + message;
    } else {
        formattedError += " at '" + token.lexeme + "': " + message;
    }
    errors.push_back(formattedError);
    return ParseError{};
}

void Parser::synchronize() {
    if (!isAtEnd()) advance();
    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON) return;
        if (peek().type == TokenType::R_BRACE) return;

        switch (peek().type) {
            case TokenType::CLASS:
            case TokenType::FUNC:
            case TokenType::VAR:
            case TokenType::CONST:
            case TokenType::ENUM:
            case TokenType::INTERFACE:
            case TokenType::IF:
            case TokenType::SWITCH:
            case TokenType::FOR:
            case TokenType::WHILE:
            case TokenType::LOOP:
            case TokenType::RETURN:
            case TokenType::TRY:
            case TokenType::THROW:
                return;
            default:
                break;
        }
        advance();
    }
}

Parser::Checkpoint Parser::mark() const {
    return {current, errors.size()};
}

void Parser::reset(const Checkpoint& checkpoint) {
    current = checkpoint.position;
    errors.resize(checkpoint.errorCount);
}

// ============================================================
// Declarations
// ============================================================

StmtPtr Parser::declaration() {
    Token start = peek();
    try {
        return place(declarationInner(), start);
    } catch (const ParseError&) {
        synchronize();
        return nullptr;
    }
}

StmtPtr Parser::declarationInner() {
    if (match({TokenType::VAR}))   return varDeclaration();
    if (match({TokenType::CONST})) return constDeclaration();
    if (match({TokenType::FUNC}))  return funcDeclaration();
    if (check(TokenType::AT)) {
        Token at = peek();
        std::vector<Token> annotations = parseAnnotations();
        if (!match({TokenType::FUNC})) throw error(peek(), "Annotations like @override go before methods.");
        StmtPtr fn = funcDeclaration();
        static_cast<FuncDeclStmt*>(fn.get())->annotations = std::move(annotations);
        return fn;
    }
    if (match({TokenType::CLASS})) return classDeclaration();
    if (match({TokenType::ENUM}))  return enumDeclaration();
    if (match({TokenType::INTERFACE})) return interfaceDeclaration();
    if (check(TokenType::DOLLAR) || isImportAhead()) return importStatement();
    return statementInner();
}

StmtPtr Parser::varDeclaration() {
    consume(TokenType::L_BRACKET, "Expect '[' before variable type.");
    TypeNode type = parseType();
    consume(TokenType::R_BRACKET, "Expect ']' after variable type.");

    std::optional<std::string> name;
    if (check(TokenType::IDENTIFIER)) {
        name = advance().lexeme;
    } else {
        throw error(peek(), "Expect variable name after type.");
    }

    ExprPtr initializer = nullptr;
    if (match({TokenType::EQUAL})) {
        initializer = expression();
    }

    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
    return std::make_unique<VarDeclStmt>(false, std::move(type), std::move(name), std::move(initializer));
}

StmtPtr Parser::constDeclaration() {
    consume(TokenType::L_BRACKET, "Expect '[' before constant type.");
    TypeNode type = parseType();
    consume(TokenType::R_BRACKET, "Expect ']' after constant type.");

    std::string name = consume(TokenType::IDENTIFIER, "Expect constant name.").lexeme;

    if (!match({TokenType::EQUAL})) {
        throw error(peek(), "Constant declaration must have an initializer.");
    }
    ExprPtr initializer = expression();

    consume(TokenType::SEMICOLON, "Expect ';' after constant declaration.");
    return std::make_unique<VarDeclStmt>(true, std::move(type), std::move(name), std::move(initializer));
}

StmtPtr Parser::funcDeclaration() {
    consume(TokenType::L_BRACKET, "Expect '[' before return type.");
    TypeNode returnType = parseType();
    consume(TokenType::R_BRACKET, "Expect ']' after return type.");

    std::string name = consume(TokenType::IDENTIFIER, "Expect function name.").lexeme;

    consume(TokenType::L_PAREN, "Expect '(' after function name.");
    std::vector<Param> params = parseParamList();
    consume(TokenType::R_PAREN, "Expect ')' after parameters.");

    consume(TokenType::L_BRACE, "Expect '{' before function body.");
    StmtPtr body = blockStatement();

    return std::make_unique<FuncDeclStmt>(std::move(returnType), std::move(name), std::move(params), std::move(body));
}

// init(params) { body }
StmtPtr Parser::constructorDeclaration() {
    consume(TokenType::L_PAREN, "Expect '(' after 'init'.");
    std::vector<Param> params = parseParamList();
    consume(TokenType::R_PAREN, "Expect ')' after constructor parameters.");
    consume(TokenType::L_BRACE, "Expect '{' before constructor body.");
    StmtPtr body = blockStatement();

    TypeNode emp;
    emp.name = "Emp";
    auto ctor = std::make_unique<FuncDeclStmt>(std::move(emp), "init", std::move(params), std::move(body));
    ctor->isConstructor = true;
    return ctor;
}

StmtPtr Parser::classDeclaration() {
    std::optional<std::string> interfaceName;

    if (match({TokenType::L_BRACKET})) {
        interfaceName = consume(TokenType::IDENTIFIER, "Expect interface name.").lexeme;
        consume(TokenType::R_BRACKET, "Expect ']' after interface name.");
    }

    std::string name = consume(TokenType::IDENTIFIER, "Expect class name.").lexeme;

    std::optional<std::string> parentClassName;
    if (match({TokenType::L_PAREN})) {
        parentClassName = consume(TokenType::IDENTIFIER, "Expect parent class name.").lexeme;
        while (match({TokenType::DOT})) {   // class Circle(shapes.Shape)
            *parentClassName += "." + consume(TokenType::IDENTIFIER, "Expect class name after '.'.").lexeme;
        }
        consume(TokenType::R_PAREN, "Expect ')' after parent class name.");
    }

    consume(TokenType::L_BRACE, "Expect '{' before class body.");
    std::vector<StmtPtr> members;
    while (!check(TokenType::R_BRACE) && !isAtEnd()) {
        Token start = peek();
        try {
            std::vector<Token> annotations = parseAnnotations();
            StmtPtr member;
            if (match({TokenType::VAR})) member = varDeclaration();
            else if (match({TokenType::CONST})) member = constDeclaration();
            else if (match({TokenType::FUNC})) member = funcDeclaration();
            else if (match({TokenType::INIT})) member = constructorDeclaration();
            else throw error(peek(), "Expect var, const, func, or init declaration inside class.");
            if (!annotations.empty()) {
                if (member->kind == StmtKind::FuncDecl) static_cast<FuncDeclStmt*>(member.get())->annotations = std::move(annotations);
                else error(annotations.front(), "Annotations like @override go before methods");
            }
            members.push_back(place(std::move(member), start));
        } catch (const ParseError&) {
            synchronize();
        }
    }
    consume(TokenType::R_BRACE, "Expect '}' after class body.");

    return std::make_unique<ClassDeclStmt>(std::move(name), std::move(interfaceName), std::move(parentClassName), std::move(members));
}

// @override (several may follow each other)
std::vector<Token> Parser::parseAnnotations() {
    std::vector<Token> annotations;
    while (match({TokenType::AT})) {
        annotations.push_back(consume(TokenType::IDENTIFIER, "Expect an annotation name after '@', e.g. @override."));
    }
    return annotations;
}

StmtPtr Parser::enumDeclaration() {
    std::string name = consume(TokenType::IDENTIFIER, "Expect enum name.").lexeme;
    consume(TokenType::L_BRACE, "Expect '{' before enum values.");

    std::vector<EnumEntry> values;

    if (!check(TokenType::R_BRACE)) {
        do {
            if (check(TokenType::R_BRACE)) break; // trailing comma
            std::string valueName = consume(TokenType::IDENTIFIER, "Expect enum value.").lexeme;

            std::optional<ExprPtr> enumValue = std::nullopt;
            if (match({TokenType::L_PAREN})) {
                enumValue = expression();
                consume(TokenType::R_PAREN, "Expect ')' after enum value.");
            }

            values.push_back({std::move(valueName), std::move(enumValue)});
        } while (match({TokenType::COMMA}));
    }

    consume(TokenType::R_BRACE, "Expect '}' after enum values.");
    return std::make_unique<EnumDeclStmt>(std::move(name), std::move(values));
}

// interface IName {
//     field[Type];
//     func[Ret] method(params);
// }
StmtPtr Parser::interfaceDeclaration() {
    std::string name = consume(TokenType::IDENTIFIER, "Expect interface name.").lexeme;
    consume(TokenType::L_BRACE, "Expect '{' before interface members.");

    std::vector<StmtPtr> members;

    while (!check(TokenType::R_BRACE) && !isAtEnd()) {
        Token start = peek();
        if (check(TokenType::IDENTIFIER)) {
            std::string fieldName = advance().lexeme;

            consume(TokenType::L_BRACKET, "Expect '[' before field type.");
            TypeNode type = parseType();
            consume(TokenType::R_BRACKET, "Expect ']' after field type.");
            consume(TokenType::SEMICOLON, "Expect ';' after interface field declaration.");

            members.push_back(place(std::make_unique<VarDeclStmt>(
                false, std::move(type), std::optional<std::string>(std::move(fieldName)), nullptr), start));
        } else if (match({TokenType::FUNC})) {
            consume(TokenType::L_BRACKET, "Expect '[' before return type.");
            TypeNode returnType = parseType();
            consume(TokenType::R_BRACKET, "Expect ']' after return type.");

            std::string methodName = consume(TokenType::IDENTIFIER, "Expect interface method name.").lexeme;
            consume(TokenType::L_PAREN, "Expect '(' after method name.");
            std::vector<Param> params = parseParamList();
            consume(TokenType::R_PAREN, "Expect ')' after interface method parameters.");
            consume(TokenType::SEMICOLON, "Expect ';' after interface method declaration.");

            members.push_back(place(std::make_unique<FuncDeclStmt>(
                std::move(returnType), std::move(methodName), std::move(params), nullptr), start));
        } else {
            throw error(peek(), "Expect a field (name[Type];) or method (func[Type] name();) inside interface.");
        }
    }

    consume(TokenType::R_BRACE, "Expect '}' after interface members.");
    return std::make_unique<InterfaceDeclStmt>(std::move(name), std::move(members));
}

// Pure lookahead for the old form `{ Name (as Alias)? (, ...)* } from` (reported)
bool Parser::isImportAhead() const {
    if (!check(TokenType::L_BRACE)) return false;
    size_t i = 1;
    for (;;) {
        if (peekAt(i).type != TokenType::IDENTIFIER) return false;
        ++i;
        if (peekAt(i).type == TokenType::AS) {
            if (peekAt(i + 1).type != TokenType::IDENTIFIER) return false;
            i += 2;
        }
        if (peekAt(i).type == TokenType::COMMA) { ++i; continue; }
        break;
    }
    return peekAt(i).type == TokenType::R_BRACE && peekAt(i + 1).type == TokenType::FROM;
}

// $("a.b", "c")  /  $("math")
std::vector<ModuleSpec> Parser::parseModuleSpecs() {
    consume(TokenType::DOLLAR, "Expect '$' before module list.");
    consume(TokenType::L_PAREN, "Expect '(' after '$'.");
    std::vector<ModuleSpec> specs;
    do {
        ModuleSpec spec;
        spec.line = peek().line;
        spec.column = peek().column;
        if (check(TokenType::L_ANGLE)) {
            // The old $(<math>): reported, but still understood
            Token angle = advance();
            spec.path = consume(TokenType::IDENTIFIER, "Expect library name after '<'.").lexeme;
            while (match({TokenType::DOT})) {
                spec.path += "." + consume(TokenType::IDENTIFIER, "Expect name after '.' in library path.").lexeme;
            }
            consume(TokenType::R_ANGLE, "Expect '>' after library name.");
            error(angle, "Libraries are imported by name without < >: $(\"" + spec.path + "\")");
        } else {
            Token path = consume(TokenType::STRING, "Expect a module or library name in quotes, e.g. $(\"math\").");
            if (path.lexeme.empty()) throw error(path, "Module path cannot be empty.");
            spec.path = path.lexeme;
        }
        if (match({TokenType::AS})) {
            spec.alias = consume(TokenType::IDENTIFIER, "Expect a name after 'as', e.g. $(\"math\" as m).").lexeme;
        }
        specs.push_back(std::move(spec));
    } while (match({TokenType::COMMA}));
    consume(TokenType::R_PAREN, "Expect ')' after module list.");
    return specs;
}

// { a, b as c }
std::vector<ImportName> Parser::parseImportNames() {
    std::vector<ImportName> names;
    consume(TokenType::L_BRACE, "Expect '{' before imported names.");
    if (!check(TokenType::R_BRACE)) {
        do {
            if (check(TokenType::R_BRACE)) break;   // trailing comma
            ImportName item;
            const Token& nameToken = consume(TokenType::IDENTIFIER, "Expect imported name.");
            item.name = nameToken.lexeme;
            item.line = nameToken.line;
            item.column = nameToken.column;
            if (match({TokenType::AS})) {
                item.alias = consume(TokenType::IDENTIFIER, "Expect alias after 'as'.").lexeme;
            }
            names.push_back(std::move(item));
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::R_BRACE, "Expect '}' after imported names.");
    return names;
}

//   $("math");                    everything
//   $("sql") { Database, x as y }  only these
//   $("math" as m)                 the module under a name: m.sqrt(2)
StmtPtr Parser::importStatement() {
    std::vector<ImportName> names;
    bool importAll = true;

    // The old form: { a } from $("x"); -- understood, but reported
    if (check(TokenType::L_BRACE)) {
        Token start = peek();
        names = parseImportNames();
        consume(TokenType::FROM, "Expect 'from' after imported names.");
        std::vector<ModuleSpec> modules = parseModuleSpecs();
        match({TokenType::SEMICOLON});
        std::string list;
        for (const auto& n : names) list += (list.empty() ? "" : ", ") + n.name + (n.alias ? " as " + *n.alias : "");
        std::string specs;
        for (const auto& m : modules) specs += (specs.empty() ? "" : ", ") + std::string("\"") + m.path + "\"";
        error(start, "Imports are written $(" + specs + ") { " + list + " } now");
        return std::make_unique<ImportStmt>(std::move(names), std::move(modules), false);
    }

    std::vector<ModuleSpec> modules = parseModuleSpecs();
    if (check(TokenType::L_BRACE)) {
        names = parseImportNames();
        importAll = false;
    }
    match({TokenType::SEMICOLON});   // optional
    return std::make_unique<ImportStmt>(std::move(names), std::move(modules), importAll);
}

// ============================================================
// Statements
// ============================================================

StmtPtr Parser::statement() {
    Token start = peek();
    return place(statementInner(), start);
}

StmtPtr Parser::statementInner() {
    if (match({TokenType::IF}))       return ifStatement();
    if (match({TokenType::SWITCH}))   return switchStatement();
    if (match({TokenType::FOR}))      return forStatement();
    if (match({TokenType::WHILE}))    return whileStatement();
    if (match({TokenType::LOOP}))     return loopStatement();
    if (match({TokenType::RETURN}))   return returnStatement();
    if (match({TokenType::BREAK}))    return breakStatement();
    if (match({TokenType::CONTINUE})) return continueStatement();
    if (match({TokenType::TRY}))      return tryStatement();
    if (match({TokenType::THROW}))    return throwStatement();
    if (match({TokenType::L_BRACE}))  return blockStatement();
    return expressionStatement();
}

// Called after '{' has been consumed.
StmtPtr Parser::blockStatement() {
    Token open = previous();
    std::vector<StmtPtr> statements;
    while (!check(TokenType::R_BRACE) && !isAtEnd()) {
        StmtPtr stmt = declaration();
        if (stmt) statements.push_back(std::move(stmt));
    }
    consume(TokenType::R_BRACE, "Expect '}' after block.");
    return place(std::make_unique<BlockStmt>(std::move(statements)), open);
}

// The body of if/else/for/while: either a { block } or a single statement.
StmtPtr Parser::bodyStatement() {
    if (match({TokenType::L_BRACE})) return blockStatement();
    return statement();
}

StmtPtr Parser::expressionStatement() {
    ExprPtr expr = expression();

    // `...` is a placeholder body; its trailing ';' is optional.
    if (expr->kind == ExprKind::Literal &&
        static_cast<LiteralExpr*>(expr.get())->token.type == TokenType::ELLIPSIS) {
        match({TokenType::SEMICOLON});
    } else {
        consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    }

    return std::make_unique<ExpressionStmt>(std::move(expr));
}

StmtPtr Parser::ifStatement() {
    consume(TokenType::L_PAREN, "Expect '(' after 'if'.");
    ExprPtr condition = expression();
    consume(TokenType::R_PAREN, "Expect ')' after if condition.");

    StmtPtr thenBranch = bodyStatement();

    StmtPtr elseBranch = nullptr;
    if (match({TokenType::ELSE})) {
        Token start = peek();
        if (match({TokenType::IF})) {
            elseBranch = place(ifStatement(), start);
        } else {
            elseBranch = bodyStatement();
        }
    }
    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

StmtPtr Parser::switchStatement() {
    consume(TokenType::L_PAREN, "Expect '(' after 'switch'.");
    ExprPtr discriminant = expression();
    consume(TokenType::R_PAREN, "Expect ')' after switch condition.");

    consume(TokenType::L_BRACE, "Expect '{' before switch cases.");
    std::vector<SwitchCase> cases;

    while (!check(TokenType::R_BRACE) && !isAtEnd()) {
        if (match({TokenType::CASE})) {
            ExprPtr val = expression();
            consume(TokenType::COLON_EQUAL, "Expect ':=' after case value.");
            consume(TokenType::L_BRACE, "Expect '{' before case body.");
            cases.push_back({std::move(val), blockStatement()});
        } else if (match({TokenType::DEFAULT})) {
            consume(TokenType::COLON_EQUAL, "Expect ':=' after default.");
            consume(TokenType::L_BRACE, "Expect '{' before default body.");
            cases.push_back({nullptr, blockStatement()});
        } else {
            throw error(peek(), "Expect 'case' or 'default' inside switch.");
        }
    }
    consume(TokenType::R_BRACE, "Expect '}' after switch block.");
    return std::make_unique<SwitchStmt>(std::move(discriminant), std::move(cases));
}

// for (var[T] x in iterable) body
// for (init; condition; increment) body
StmtPtr Parser::forStatement() {
    consume(TokenType::L_PAREN, "Expect '(' after 'for'.");

    StmtPtr init = nullptr;
    if (check(TokenType::VAR)) {
        Token start = advance();
        consume(TokenType::L_BRACKET, "Expect '[' before loop variable type.");
        TypeNode type = parseType();
        consume(TokenType::R_BRACKET, "Expect ']' after loop variable type.");
        std::string name = consume(TokenType::IDENTIFIER, "Expect loop variable name.").lexeme;

        if (match({TokenType::IN})) {
            StmtPtr decl = place(std::make_unique<VarDeclStmt>(false, std::move(type), std::move(name), nullptr), start);
            ExprPtr iterable = expression();
            consume(TokenType::R_PAREN, "Expect ')' after iterable.");
            StmtPtr body = bodyStatement();
            return std::make_unique<ForInStmt>(std::move(decl), std::move(iterable), std::move(body));
        }

        ExprPtr initializer = nullptr;
        if (match({TokenType::EQUAL})) initializer = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after loop initializer.");
        init = place(std::make_unique<VarDeclStmt>(false, std::move(type), std::move(name), std::move(initializer)), start);
    } else if (!match({TokenType::SEMICOLON})) {
        init = statement(); // expression statement, consumes its ';'
    }

    ExprPtr condition = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        condition = expression();
    }
    consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");

    ExprPtr increment = nullptr;
    if (!check(TokenType::R_PAREN)) {
        increment = expression();
    }
    consume(TokenType::R_PAREN, "Expect ')' after for clauses.");

    StmtPtr body = bodyStatement();
    return std::make_unique<ForStmt>(std::move(init), std::move(condition), std::move(increment), std::move(body));
}

StmtPtr Parser::whileStatement() {
    consume(TokenType::L_PAREN, "Expect '(' after 'while'.");
    ExprPtr condition = expression();
    consume(TokenType::R_PAREN, "Expect ')' after while condition.");
    StmtPtr body = bodyStatement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

StmtPtr Parser::loopStatement() {
    consume(TokenType::L_BRACE, "Expect '{' before loop body.");
    return std::make_unique<LoopStmt>(blockStatement());
}

StmtPtr Parser::returnStatement() {
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        value = expression();
    }
    consume(TokenType::SEMICOLON, "Expect ';' after return value.");
    return std::make_unique<ReturnStmt>(std::move(value));
}

StmtPtr Parser::breakStatement() {
    consume(TokenType::SEMICOLON, "Expect ';' after break.");
    return std::make_unique<BreakStmt>();
}

StmtPtr Parser::continueStatement() {
    consume(TokenType::SEMICOLON, "Expect ';' after continue.");
    return std::make_unique<ContinueStmt>();
}

StmtPtr Parser::tryStatement() {
    consume(TokenType::L_BRACE, "Expect '{' before try block.");
    StmtPtr tryBlock = blockStatement();

    std::vector<CatchClause> catches;
    while (match({TokenType::CATCH})) {
        CatchClause clause;
        clause.line = previous().line;
        clause.column = previous().column;
        if (match({TokenType::L_PAREN})) {
            clause.param = consume(TokenType::IDENTIFIER, "Expect catch parameter name.").lexeme;
            consume(TokenType::L_BRACKET, "Expect '[' before catch parameter type.");
            clause.type = parseType();
            consume(TokenType::R_BRACKET, "Expect ']' after catch parameter type.");
            consume(TokenType::R_PAREN, "Expect ')' after catch parameter declaration.");
        }
        consume(TokenType::L_BRACE, "Expect '{' before catch block.");
        clause.body = blockStatement();
        catches.push_back(std::move(clause));
    }
    if (catches.empty()) {
        throw error(peek(), "Expect 'catch' after try block.");
    }

    return std::make_unique<TryStmt>(std::move(tryBlock), std::move(catches));
}

StmtPtr Parser::throwStatement() {
    ExprPtr expr = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after throw expression.");
    return std::make_unique<ThrowStmt>(std::move(expr));
}

// ============================================================
// Shared grammar fragments
// ============================================================

TypeNode Parser::parseType() {
    TypeNode node;

    // (Int | Str)<> -- parentheses group a type, e.g. to make a list of a union
    if (match({TokenType::L_PAREN})) {
        node.group.push_back(parseType());
        consume(TokenType::R_PAREN, "Expect ')' after the type in parentheses.");
    } else if (check(TokenType::IDENTIFIER) ||
        check(TokenType::TYPE_STR) || check(TokenType::TYPE_INT) || check(TokenType::TYPE_BUL) ||
        check(TokenType::TYPE_FLT) || check(TokenType::TYPE_DBL) || check(TokenType::TYPE_CHR) ||
        check(TokenType::TYPE_EMP) || check(TokenType::TYPE_NIL)) {
        node.name = advance().lexeme;
        // m.Database: a type from a module imported as m
        while (previous().type == TokenType::IDENTIFIER && check(TokenType::DOT) && checkNext(TokenType::IDENTIFIER)) {
            advance();
            node.name += "." + advance().lexeme;
        }
    } else {
        throw error(peek(), "Expect type name.");
    }

    // Func[ReturnType, [ParamType, ...]]  /  Func[ReturnType, ParamType]  /  Func[ReturnType]
    // Map[KeyType, ValueType]
    if (node.name == "Map" && match({TokenType::L_BRACKET})) {
        node.isMap = true;
        node.mapTypes.push_back(parseType());
        consume(TokenType::COMMA, "Expect ',' between the key and value types, e.g. Map[Str, Int].");
        node.mapTypes.push_back(parseType());
        consume(TokenType::R_BRACKET, "Expect ']' after Map types, e.g. Map[Str, Int].");
    } else if (node.name == "Func" && match({TokenType::L_BRACKET})) {
        node.isFunction = true;
        node.funcReturn.push_back(parseType());
        if (match({TokenType::COMMA})) {
            if (match({TokenType::L_BRACKET})) {
                if (!check(TokenType::R_BRACKET)) {
                    do {
                        node.funcParams.push_back(parseType());
                    } while (match({TokenType::COMMA}));
                }
                consume(TokenType::R_BRACKET, "Expect ']' after Func parameter types.");
            } else {
                node.funcParams.push_back(parseType());
            }
        }
        consume(TokenType::R_BRACKET, "Expect ']' after Func type, e.g. Func[Int, [Int, Int]].");
    }

    // Int[16]: an integer base
    if (!node.isFunction && !node.isMap && !node.name.empty() && match({TokenType::L_BRACKET})) {
        Token radixToken = consume(TokenType::INT_LITERAL, "Expect integer base (2, 8, 10 or 16).");
        try {
            node.radix = std::stoi(radixToken.lexeme);
        } catch (const std::exception&) {
            throw error(radixToken, "Invalid integer base.");
        }
        consume(TokenType::R_BRACKET, "Expect ']' after integer base.");
    }

    // Collection suffixes nest: Int<> (list), Int{} (set), Int<><> (list of lists)
    for (;;) {
        bool list;
        if (match({TokenType::L_ANGLE})) {
            consume(TokenType::R_ANGLE, "Expect '>' after '<' in list type.");
            list = true;
        } else if (match({TokenType::L_BRACE})) {
            consume(TokenType::R_BRACE, "Expect '}' after '{' in set type.");
            list = false;
        } else {
            break;
        }
        if (node.isList || node.isSet) {
            TypeNode inner = std::move(node);
            node = TypeNode{};
            node.group.push_back(std::move(inner));
        }
        if (list) node.isList = true;
        else node.isSet = true;
    }

    while (match({TokenType::BITWISE_OR})) {
        node.unionMembers.push_back(parseType());
    }

    return node;
}

// name[Type], rest[Type*], greeting[Str]="Hi", ...   (possibly empty; stops before ')')
std::vector<Param> Parser::parseParamList() {
    std::vector<Param> params;
    if (!check(TokenType::R_PAREN)) {
        do {
            Param param;
            const Token& nameToken = consume(TokenType::IDENTIFIER, "Expect parameter name.");
            param.name = nameToken.lexeme;
            param.line = nameToken.line;
            param.column = nameToken.column;
            consume(TokenType::L_BRACKET, "Expect '[' before parameter type.");
            param.type = parseType();
            param.isVariadic = match({TokenType::MULT});
            consume(TokenType::R_BRACKET, "Expect ']' after parameter type.");
            if (match({TokenType::EQUAL})) param.defaultValue = expression();
            params.push_back(std::move(param));
        } while (match({TokenType::COMMA}));
    }
    return params;
}

// ============================================================
// Expressions: precedence-climbing chain
// ============================================================

ExprPtr Parser::binary(ExprPtr left, Token op, ExprPtr right) {
    int line = left->line, column = left->column;
    auto node = std::make_unique<BinaryExpr>(std::move(left), std::move(op), std::move(right));
    node->line = line;
    node->column = column;
    return node;
}

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    ExprPtr expr = ternary();
    if (match({TokenType::EQUAL, TokenType::PLUS_EQUAL, TokenType::MINUS_EQUAL,
               TokenType::MULT_EQUAL, TokenType::DIV_EQUAL, TokenType::EXPONENT_EQUAL,
               TokenType::MOD_EQUAL, TokenType::QUESTION_EQUAL})) {
        Token op = previous();
        ExprPtr value = assignment();
        int line = expr->line, column = expr->column;
        auto node = std::make_unique<AssignExpr>(std::move(expr), std::move(op), std::move(value));
        node->line = line;
        node->column = column;
        return node;
    }
    return expr;
}

// x |> f(a)  is  f(x, a);   x |> f  is  f(x)
ExprPtr Parser::pipe() {
    ExprPtr expr = nullishCoalescing();
    while (match({TokenType::FUNC_PIPE})) {
        Token op = previous();
        int line = expr->line, column = expr->column;
        ExprPtr target = call();
        if (target->kind == ExprKind::Call) {
            auto* callExpr = static_cast<CallExpr*>(target.get());
            Argument first;
            first.value = std::move(expr);
            callExpr->args.insert(callExpr->args.begin(), std::move(first));
            expr = std::move(target);
        } else {
            std::vector<Argument> args(1);
            args[0].value = std::move(expr);
            expr = std::make_unique<CallExpr>(std::move(target), std::move(args), op);
        }
        expr->line = line;
        expr->column = column;
    }
    return expr;
}

ExprPtr Parser::ternary() {
    ExprPtr condition = pipe();
    if (match({TokenType::QUESTION})) {
        ExprPtr thenBranch = expression();
        consume(TokenType::COLON, "Expect ':' after ternary 'then' branch.");
        ExprPtr elseBranch = ternary();
        int line = condition->line, column = condition->column;
        auto node = std::make_unique<TernaryExpr>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
        node->line = line;
        node->column = column;
        return node;
    }
    return condition;
}

ExprPtr Parser::nullishCoalescing() {
    ExprPtr expr = logicalOr();
    while (match({TokenType::QUESTION_QUESTION, TokenType::QUESTION_BANG})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), logicalOr());
    }
    return expr;
}

ExprPtr Parser::logicalOr() {
    ExprPtr expr = logicalAnd();
    while (match({TokenType::OR_OR})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), logicalAnd());
    }
    return expr;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr expr = bitwiseOr();
    while (match({TokenType::AND_AND})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), bitwiseOr());
    }
    return expr;
}

ExprPtr Parser::bitwiseOr() {
    ExprPtr expr = bitwiseXor();
    while (match({TokenType::BITWISE_OR})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), bitwiseXor());
    }
    return expr;
}

ExprPtr Parser::bitwiseXor() {
    ExprPtr expr = bitwiseAnd();
    while (match({TokenType::BITWISE_XOR})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), bitwiseAnd());
    }
    return expr;
}

ExprPtr Parser::bitwiseAnd() {
    ExprPtr expr = equality();
    while (match({TokenType::BITWISE_AND})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), equality());
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    if (match({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), comparison());

        if (check(TokenType::EQUAL_EQUAL) || check(TokenType::BANG_EQUAL)) {
            throw error(peek(), "Equality operators (==, !=) cannot be chained. Use logical operators (&&, ||).");
        }
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = shift();
    if (match({TokenType::IS})) {
        int line = expr->line, column = expr->column;
        TypeNode type = parseType();
        expr = std::make_unique<IsExpr>(std::move(expr), std::move(type));
        expr->line = line;
        expr->column = column;
        return expr;
    }
    if (match({TokenType::L_ANGLE, TokenType::LESS_EQUAL,
               TokenType::R_ANGLE, TokenType::GREATER_EQUAL})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), shift());

        if (check(TokenType::L_ANGLE) || check(TokenType::LESS_EQUAL) ||
            check(TokenType::R_ANGLE) || check(TokenType::GREATER_EQUAL)) {
            throw error(peek(), "Comparison operators (<, >, <=, >=) cannot be chained. Use logical operators (&&, ||).");
        }
    }
    return expr;
}

ExprPtr Parser::shift() {
    ExprPtr expr = additive();
    while (match({TokenType::LEFT_SHIFT, TokenType::RIGHT_SHIFT})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), additive());
    }
    return expr;
}

ExprPtr Parser::additive() {
    ExprPtr expr = multiplicative();
    while (match({TokenType::PLUS, TokenType::MINUS})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), multiplicative());
    }
    return expr;
}

ExprPtr Parser::multiplicative() {
    ExprPtr expr = unary();
    while (match({TokenType::MULT, TokenType::DIV, TokenType::MOD})) {
        Token op = previous();
        expr = binary(std::move(expr), std::move(op), unary());
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match({TokenType::NOT, TokenType::MINUS, TokenType::BITWISE_NOT})) {
        Token op = previous();
        ExprPtr operand = unary();
        return place(std::make_unique<UnaryExpr>(op, std::move(operand)), op);
    }
    return exponent();
}

// ^ is right-associative and binds tighter than unary minus on its left:
// -2 ^ 2 == -(2 ^ 2)
ExprPtr Parser::exponent() {
    ExprPtr left = postfix();
    if (match({TokenType::EXPONENT})) {
        Token op = previous();
        return binary(std::move(left), std::move(op), unary());
    }
    return left;
}

ExprPtr Parser::postfix() {
    ExprPtr expr = call();
    if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
        Token op = previous();
        int line = expr->line, column = expr->column;
        expr = std::make_unique<PostfixExpr>(std::move(op), std::move(expr));
        expr->line = line;
        expr->column = column;
    }
    return expr;
}

ExprPtr Parser::call() {
    ExprPtr expr = primary();

    for (;;) {
        int line = expr->line, column = expr->column;
        if (match({TokenType::L_PAREN})) {
            // super(...) is short for super.init(...)
            if (expr->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(expr.get())->name.type == TokenType::SUPER) {
                const Token& superToken = static_cast<const IdentifierExpr*>(expr.get())->name;
                Token init(TokenType::INIT, "init", superToken.line, superToken.column);
                expr = std::make_unique<MemberExpr>(std::move(expr), init, false);
                expr->line = line;
                expr->column = column;
            }
            expr = finishCall(std::move(expr));
        } else if (match({TokenType::DOT, TokenType::QUESTION_DOT})) {
            bool optional = previous().type == TokenType::QUESTION_DOT;
            Token name = check(TokenType::INIT) ? advance()
                       : consume(TokenType::IDENTIFIER, "Expect member name after '.'.");
            expr = std::make_unique<MemberExpr>(std::move(expr), std::move(name), optional);
        } else if (check(TokenType::L_ANGLE)) {
            auto attempt = tryParseIndex(expr);
            if (!attempt.has_value()) break;
            expr = std::move(*attempt);
        } else {
            break;
        }
        expr->line = line;
        expr->column = column;
    }

    return expr;
}

// students<0> vs. a < b: an index is `<` additive-expression `>`. If that
// shape doesn't match, rewind and let comparison() treat `<` as less-than.
std::optional<ExprPtr> Parser::tryParseIndex(ExprPtr& object) {
    Checkpoint checkpoint = mark();
    advance(); // '<'

    try {
        ExprPtr index = additive();
        if (check(TokenType::R_ANGLE)) {
            advance();
            return std::make_unique<IndexExpr>(std::move(object), std::move(index));
        }
    } catch (const ParseError&) {
        // not an index
    }

    reset(checkpoint);
    return std::nullopt;
}

std::vector<Argument> Parser::parseArguments() {
    std::vector<Argument> args;
    if (!check(TokenType::R_PAREN)) {
        do {
            Argument arg;
            if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON)) {
                arg.name = advance().lexeme;
                advance(); // ':'
            }
            arg.isSpread = match({TokenType::ELLIPSIS});
            arg.value = expression();
            args.push_back(std::move(arg));
        } while (match({TokenType::COMMA}));
    }
    return args;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
    std::vector<Argument> args = parseArguments();
    Token paren = consume(TokenType::R_PAREN, "Expect ')' after arguments.");
    return std::make_unique<CallExpr>(std::move(callee), std::move(args), std::move(paren));
}

// Called after '(' has been consumed.
ExprPtr Parser::parseLambdaOrGrouping(const Token& openParen) {
    bool isLambda = (check(TokenType::R_PAREN) && checkNext(TokenType::COLON_EQUAL)) ||
                    (check(TokenType::IDENTIFIER) && checkNext(TokenType::L_BRACKET));

    if (isLambda) {
        std::vector<Param> params = parseParamList();
        consume(TokenType::R_PAREN, "Expect ')' after lambda parameters.");
        consume(TokenType::COLON_EQUAL, "Expect ':=' after lambda parameters.");

        StmtPtr body;
        if (match({TokenType::L_BRACE})) {
            body = blockStatement();
        } else {
            Token start = peek();
            body = place(std::make_unique<ExpressionStmt>(expression()), start);
        }
        return place(std::make_unique<LambdaExpr>(std::move(params), std::move(body)), openParen);
    }

    ExprPtr expr = expression();
    consume(TokenType::R_PAREN, "Expect ')' after expression.");
    return place(std::make_unique<GroupingExpr>(std::move(expr)), openParen);
}

// $"Hello {name}, you are {age + 1}" -> literal parts + parsed interpolations.
// Braces nest and string literals inside an interpolation are skipped, so
// {fmt("}")} works. `\{` is a literal brace.
ExprPtr Parser::parseTemplateString(const Token& token) {
    const std::string& text = token.lexeme;
    std::vector<std::string> literals;
    std::vector<ExprPtr> interpolations;

    std::string pending;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            pending += text.substr(i, 2);
            i += 2;
            continue;
        }
        if (c == '}') {
            throw error(token, "Unmatched '}' in template string (use \\} for a literal brace).");
        }
        if (c != '{') {
            pending += c;
            ++i;
            continue;
        }

        // Find the matching '}'
        size_t depth = 1;
        size_t j = i + 1;
        char quote = 0;
        for (; j < text.size() && depth > 0; ++j) {
            char d = text[j];
            if (quote) {
                if (d == '\\') ++j;
                else if (d == quote) quote = 0;
            } else if (d == '"' || d == '\'') {
                quote = d;
            } else if (d == '{') {
                ++depth;
            } else if (d == '}') {
                --depth;
            }
        }
        if (depth != 0) throw error(token, "Unterminated '{' in template string.");

        std::string exprText = text.substr(i + 1, j - i - 2);
        literals.push_back(unescape(pending));
        pending.clear();

        Lexer innerLexer(exprText);
        std::vector<Token> innerTokens = innerLexer.scanTokens();
        for (auto& t : innerTokens) {
            // Re-anchor positions to the template string in the source file.
            // (Template strings are single-line; +2 skips the `$"` prefix.)
            t.column += token.column + 1 + static_cast<int>(i);
            t.line = token.line;
        }
        if (innerLexer.hasErrors() || exprText.find_first_not_of(" \t") == std::string::npos) {
            throw error(token, "Invalid interpolation '{" + exprText + "}' in template string.");
        }

        Parser innerParser(std::move(innerTokens));
        ExprPtr inner;
        try {
            inner = innerParser.expression();
            if (!innerParser.isAtEnd()) innerParser.error(innerParser.peek(), "Unexpected token in interpolation.");
        } catch (const ParseError&) {
        }
        if (innerParser.hasErrors() || !inner) {
            for (const auto& e : innerParser.getErrors()) errors.push_back(e);
            throw ParseError{};
        }
        interpolations.push_back(std::move(inner));
        i = j;
    }
    literals.push_back(unescape(pending));

    auto expr = std::make_unique<TemplateStringExpr>(token);
    expr->literalParts = std::move(literals);
    expr->interpolations = std::move(interpolations);
    return place(std::move(expr), token);
}

ExprPtr Parser::primary() {
    if (match({TokenType::INT_LITERAL, TokenType::FLOAT_LITERAL, TokenType::DOUBLE_LITERAL,
               TokenType::CHAR_LITERAL, TokenType::STRING, TokenType::TRUE, TokenType::FALSE,
               TokenType::BINARY_LITERAL, TokenType::OCTAL_LITERAL, TokenType::HEX_LITERAL,
               TokenType::NIL, TokenType::ELLIPSIS})) {
        return place(std::make_unique<LiteralExpr>(previous()), previous());
    }

    // Int("42"), Str(42), ... -- conversions
    if ((check(TokenType::TYPE_INT) || check(TokenType::TYPE_STR) || check(TokenType::TYPE_BUL) ||
         check(TokenType::TYPE_FLT) || check(TokenType::TYPE_DBL) || check(TokenType::TYPE_CHR)) &&
        checkNext(TokenType::L_PAREN)) {
        Token type = advance();
        advance(); // '('
        if (check(TokenType::R_PAREN)) throw error(peek(), type.lexeme + "(...) needs a value to convert, e.g. " + type.lexeme + "(x)");
        ExprPtr value = expression();
        consume(TokenType::R_PAREN, "Expect ')' after the value to convert; a conversion takes one value.");
        return place(std::make_unique<ConvertExpr>(type, std::move(value)), type);
    }

    if (match({TokenType::TEMPLATE_STRING})) {
        return parseTemplateString(previous());
    }

    if (match({TokenType::IDENTIFIER, TokenType::THIS, TokenType::SUPER})) {
        return place(std::make_unique<IdentifierExpr>(previous()), previous());
    }

    if (match({TokenType::NEW})) {
        Token newToken = previous();
        // new { message = "Hi", code = 200 }: an anonymous object (an Obj)
        if (check(TokenType::L_BRACE)) {
            Token brace = advance();
            std::vector<Argument> fields;
            while (!check(TokenType::R_BRACE) && !isAtEnd()) {
                Argument field;
                field.name = consume(TokenType::IDENTIFIER, "Expect a field name, e.g. new { message = \"Hi\" }.").lexeme;
                if (!match({TokenType::EQUAL, TokenType::COLON})) throw error(peek(), "Expect '=' after the field name.");
                field.value = expression();
                fields.push_back(std::move(field));
                if (!match({TokenType::COMMA})) break;
            }
            consume(TokenType::R_BRACE, "Expect '}' after the fields of the object.");
            auto anonymous = std::make_unique<NewExpr>(brace, std::move(fields));
            anonymous->isAnonymous = true;
            return place(std::move(anonymous), newToken);
        }
        Token className = consume(TokenType::IDENTIFIER, "Expect class name after 'new'.");
        while (match({TokenType::DOT})) {   // new m.Database()
            className.lexeme += "." + consume(TokenType::IDENTIFIER, "Expect class name after '.'.").lexeme;
        }
        consume(TokenType::L_PAREN, "Expect '(' after class name.");
        std::vector<Argument> args = parseArguments();
        consume(TokenType::R_PAREN, "Expect ')' after constructor arguments.");
        return place(std::make_unique<NewExpr>(std::move(className), std::move(args)), newToken);
    }

    if (match({TokenType::L_BRACKET})) {
        Token open = previous();
        std::vector<CollectionElement> elements;
        if (!check(TokenType::R_BRACKET)) {
            do {
                if (check(TokenType::R_BRACKET)) break; // trailing comma
                bool isSpread = match({TokenType::ELLIPSIS});
                elements.push_back({isSpread, expression()});
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::R_BRACKET, "Expect ']' after list elements.");
        return place(std::make_unique<ListExpr>(std::move(elements)), open);
    }

    if (match({TokenType::L_BRACE})) {
        Token open = previous();

        // {:} -- an empty map
        if (check(TokenType::COLON) && checkNext(TokenType::R_BRACE)) {
            advance();
            advance();
            return place(std::make_unique<MapExpr>(std::vector<MapEntry>{}), open);
        }

        std::vector<CollectionElement> elements;
        std::vector<MapEntry> entries;
        bool isMap = false;
        if (!check(TokenType::R_BRACE)) {
            do {
                if (check(TokenType::R_BRACE)) break; // trailing comma
                bool isSpread = match({TokenType::ELLIPSIS});
                ExprPtr first = expression();
                // The first `key: value` decides that this is a map (any
                // spreads before it were maps being copied in).
                if (!isMap && !isSpread && check(TokenType::COLON) &&
                    std::all_of(elements.begin(), elements.end(), [](const CollectionElement& e) { return e.isSpread; })) {
                    isMap = true;
                    for (auto& e : elements) entries.push_back({nullptr, std::move(e.value)});
                    elements.clear();
                }
                if (isMap) {
                    if (isSpread) {
                        entries.push_back({nullptr, std::move(first)});
                    } else {
                        consume(TokenType::COLON, "Expect ':' between key and value in a map literal.");
                        entries.push_back({std::move(first), expression()});
                    }
                } else {
                    elements.push_back({isSpread, std::move(first)});
                }
            } while (match({TokenType::COMMA}));
        }
        if (isMap) {
            consume(TokenType::R_BRACE, "Expect '}' after map entries.");
            return place(std::make_unique<MapExpr>(std::move(entries)), open);
        }
        consume(TokenType::R_BRACE, "Expect '}' after set elements.");
        return place(std::make_unique<SetExpr>(std::move(elements)), open);
    }

    if (match({TokenType::L_PAREN})) {
        return parseLambdaOrGrouping(previous());
    }

    if (check(TokenType::DOLLAR)) {
        throw error(peek(), "Module references '$(...)' are only allowed in import statements.");
    }
    if (isReserved(peek().type)) {
        throw error(peek(), "'" + peek().lexeme + "' is reserved for future use.");
    }
    throw error(peek(), "Expect expression.");
}

} // namespace dessa
