#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "token.hpp"

namespace dessa {

// ============================================================
// Lexer
// ------------------------------------------------------------
// Turns raw Dessa source text into a flat vector of Tokens.
// One Lexer instance = one source file / one pass.
//
// Usage:
//   Lexer lexer(sourceString);
//   std::vector<Token> tokens = lexer.scanTokens();
//   if (lexer.hasErrors()) { ... lexer.getErrors() ... }
// ============================================================
class Lexer {
public:
    explicit Lexer(std::string source);

    // Runs the lexer to completion and returns every token,
    // including a trailing END_OF_FILE token.
    std::vector<Token> scanTokens();

    // ---- Error checking for the main compiler loop ----
    bool hasErrors() const;
    const std::vector<std::string>& getErrors() const;

private:
    // ---- Source + cursor state ----
    std::string source;
    std::vector<Token> tokens;

    size_t start   = 0;  // index where the current lexeme began
    size_t current = 0;  // index of the character about to be read
    int    line    = 1;
    int    column  = 1;  // column of `current`, reset on '\n'
    int    startLine = 1;
    int    startColumn = 1;

    // ---- Keyword lookup ----
    static const std::unordered_map<std::string, TokenType> keywords;

    // ---- Character stream helpers ("the plumbing") ----
    bool isAtEnd() const;
    char advance();               // consumes and returns the current char
    char peek() const;            // current char, without consuming
    char peekNext() const;        // char after current, without consuming
    bool match(char expected);    // consumes current char IF it equals expected

    // ---- Token emission ----
    void addToken(TokenType type);
    void addToken(TokenType type, std::string lexeme);

    // ---- Core scanning ----
    void scanToken();

    // ---- Lexeme-specific scanners ----
    void scanString();             // "..."
    void scanTemplateString();     // $"..." with {expr} interpolation
    void scanTripleString(bool isTemplate); // """...""" and $"""...""" (multi-line)
    void scanNumber();             // 20 / 9.23 / 0b1001 / 0o32 / 0xf8
    void scanIdentifierOrKeyword();
    void scanChar();               // 'A'
    char translateEscape(char escape); // the char after a backslash

    // ---- Error reporting (internal) ----
    std::vector<std::string> errors;
    void error(const std::string& message);
};

} // namespace dessa