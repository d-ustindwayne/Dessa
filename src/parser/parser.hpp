#pragma once

#include <vector>
#include <memory>
#include <optional>
#include <initializer_list>
#include <string>
#include "token.hpp"
#include "ast.hpp"

namespace dessa {

struct ParseError {};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::vector<StmtPtr> parseProgram();

    bool hasErrors() const;
    const std::vector<std::string>& getErrors() const;

private:
    std::vector<Token> tokens;
    size_t current = 0;
    std::vector<std::string> errors;

    bool isAtEnd() const;
    const Token& peek() const;
    const Token& peekAt(size_t offset) const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkNext(TokenType type) const;
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    ParseError error(const Token& token, const std::string& message);
    void synchronize();

    // Backtracking: a checkpoint remembers both the token position and the
    // number of errors, so an abandoned attempt leaves no stray diagnostics.
    struct Checkpoint { size_t position; size_t errorCount; };
    Checkpoint mark() const;
    void reset(const Checkpoint& checkpoint);

    template <typename T>
    T place(T node, const Token& at) {
        if (node && node->line == 0) { node->line = at.line; node->column = at.column; }
        return node;
    }

    StmtPtr declaration();
    StmtPtr declarationInner();
    StmtPtr varDeclaration();
    StmtPtr constDeclaration();
    StmtPtr declarators(bool isConst, const TypeNode& type);
    // var[Int] x = 1, y = 2; is one statement but several declarations:
    // the first is returned, the rest wait here for the caller's list
    std::vector<StmtPtr> moreDeclarations;
    void takeMoreDeclarations(std::vector<StmtPtr>& into);
    StmtPtr funcDeclaration();
    StmtPtr constructorDeclaration();
    StmtPtr classDeclaration();
    StmtPtr enumDeclaration();
    StmtPtr interfaceDeclaration();
    StmtPtr importStatement();
    std::vector<ImportName> parseImportNames();
    std::vector<Token> parseAnnotations();
    std::vector<TypeNode> parseThrows();
    ExprPtr pipe();
    bool isImportAhead() const;
    std::vector<ModuleSpec> parseModuleSpecs();

    StmtPtr statement();
    StmtPtr statementInner();
    StmtPtr blockStatement();
    StmtPtr bodyStatement();
    StmtPtr ifStatement();
    StmtPtr switchStatement();
    StmtPtr forStatement();
    StmtPtr whileStatement();
    StmtPtr loopStatement();
    StmtPtr returnStatement();
    StmtPtr breakStatement();
    StmtPtr continueStatement();
    StmtPtr tryStatement();
    StmtPtr throwStatement();
    StmtPtr expressionStatement();

    TypeNode parseType();
    std::vector<Param> parseParamList();

    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr ternary();
    ExprPtr nullishCoalescing();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr bitwiseOr();
    ExprPtr bitwiseXor();
    ExprPtr bitwiseAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr shift();
    ExprPtr additive();
    ExprPtr multiplicative();
    ExprPtr unary();
    ExprPtr exponent();
    ExprPtr postfix();
    ExprPtr call();
    ExprPtr primary();

    ExprPtr binary(ExprPtr left, Token op, ExprPtr right);
    std::vector<Argument> parseArguments();
    ExprPtr finishCall(ExprPtr callee);
    std::optional<ExprPtr> tryParseIndex(ExprPtr& object);
    ExprPtr parseLambdaOrGrouping(const Token& openParen);
    ExprPtr parseTemplateString(const Token& token);
};

} // namespace dessa
