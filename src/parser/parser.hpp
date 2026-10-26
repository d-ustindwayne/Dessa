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

    // Where the old [1, 2] list literals are: the [ and the ] (for
    // `dessa lists`, which rewrites them as <1, 2>)
    struct BracketList { int openLine, openColumn, closeLine, closeColumn; };
    const std::vector<BracketList>& bracketLists() const { return oldLists; }

    // Generic functions and classes (not in the program itself: they are
    // filled in by the analyser for each use)
    std::vector<StmtPtr> takeTemplates() { return std::move(templates); }
    // Parses one generic declaration again with its type parameters filled
    // in (name -> a TypeNode with `resolved` set)
    static StmtPtr instantiate(const std::vector<Token>& tokens, const std::vector<std::pair<std::string, TypeNode>>& types);

private:
    std::vector<Token> tokens;
    size_t current = 0;
    std::vector<std::string> errors;
    std::vector<BracketList> oldLists;
    std::vector<StmtPtr> templates;
    std::vector<std::pair<std::string, TypeNode>> substitutions;   // while instantiating
    bool instantiating = false;
    // In a ternary's then-branch or a map key a ':' ends the expression, so
    // name:Type( is not a generic call there
    bool colonEnds = false;
    struct ColonScope {
        Parser& parser;
        bool saved;
        ColonScope(Parser& p, bool value) : parser(p), saved(p.colonEnds) { p.colonEnds = value; }
        ~ColonScope() { parser.colonEnds = saved; }
    };
    std::vector<std::string> parseTypeParams();
    std::vector<TypeNode> parseTypeArgs();
    TypeNode parseTypeBase();
    bool typeArgAhead() const;
    bool typeArgAheadAt(size_t offset) const;

    // Inside <...> a list literal's own > ends it, so a > there is not a
    // comparison (unless it is inside brackets of its own: <(a > b)>)
    bool inAngleList = false;
    int indexDepth = 0;   // inside students<...>: m<xs<0>> closes two at once
    struct AngleListScope {
        Parser& parser;
        bool saved;
        AngleListScope(Parser& p, bool value) : parser(p), saved(p.inAngleList) { p.inAngleList = value; }
        ~AngleListScope() { parser.inAngleList = saved; }
    };
    void splitDoubleAngle(TokenType single);

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
