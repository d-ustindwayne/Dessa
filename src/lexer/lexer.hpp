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
// \xHH: the character U+00HH, as UTF-8 ("\xe9" is "é", like JavaScript's)
inline std::string hexEscape(const std::string& twoDigits) {
    const unsigned code = static_cast<unsigned>(std::stoi(twoDigits, nullptr, 16));
    if (code < 0x80) return std::string(1, static_cast<char>(code));
    return {static_cast<char>(0xC0 | (code >> 6)), static_cast<char>(0x80 | (code & 0x3F))};
}

class Lexer {
public:
    // componentFile: an .xds file, where @(<...>) is one MARKUP token
    explicit Lexer(std::string source, bool componentFile = false);

    // Runs the lexer to completion and returns every token,
    // including a trailing END_OF_FILE token.
    std::vector<Token> scanTokens();

    // ---- Error checking for the main compiler loop ----
    bool hasErrors() const;
    // The rest of the file went with an error: parsing it would only add noise
    bool stopped() const { return stoppedEarly; }
    const std::vector<std::string>& getErrors() const;

private:
    // ---- Source + cursor state ----
    std::string source;
    bool componentFile = false;
    bool stoppedEarly = false;
    bool scanMarkup();
    bool skipMarkupElement(int depth);
    bool skipMarkupExpression(int depth);
    bool markupAhead(size_t& at) const;
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
    std::string translateEscape(char escape); // what the char after a backslash stands for

    // ---- Error reporting (internal) ----
    std::vector<std::string> errors;
    void error(const std::string& message);
};

} // namespace dessa