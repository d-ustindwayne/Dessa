#include "lexer.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

namespace dessa {

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"var",       TokenType::VAR},
    {"const",     TokenType::CONST},
    {"func",      TokenType::FUNC},
    {"class",     TokenType::CLASS},
    {"interface", TokenType::INTERFACE},
    {"enum",      TokenType::ENUM},
    {"Str",  TokenType::TYPE_STR},
    {"Int",  TokenType::TYPE_INT},
    {"Bul",  TokenType::TYPE_BUL},
    {"Flt",  TokenType::TYPE_FLT},
    {"Dbl",  TokenType::TYPE_DBL},
    {"Chr",  TokenType::TYPE_CHR},
    {"Emp",  TokenType::TYPE_EMP},
    {"Nil",  TokenType::TYPE_NIL},
    {"if",       TokenType::IF},
    {"else",     TokenType::ELSE},
    {"switch",   TokenType::SWITCH},
    {"case",     TokenType::CASE},
    {"default",  TokenType::DEFAULT},
    {"for",      TokenType::FOR},
    {"while",    TokenType::WHILE},
    {"loop",     TokenType::LOOP},
    {"break",    TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"return",   TokenType::RETURN},
    {"in",       TokenType::IN},
    {"try", TokenType::TRY},
    {"catch", TokenType::CATCH},
    {"throw", TokenType::THROW},
    {"as",   TokenType::AS},
    {"new",   TokenType::NEW},
    {"this",  TokenType::THIS},
    {"super", TokenType::SUPER},
    {"init",  TokenType::INIT},
    {"true",  TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"nil",   TokenType::NIL},
    {"with",  TokenType::WITH},
    {"is",    TokenType::IS},
};

Lexer::Lexer(std::string source) : source(std::move(source)) {}

std::vector<Token> Lexer::scanTokens() {
    while (!isAtEnd()) {
        start = current;
        startLine = line;
        startColumn = column;
        scanToken();
    }
    tokens.emplace_back(TokenType::END_OF_FILE, "", line, column);
    return tokens;
}

bool Lexer::isAtEnd() const {
    return current >= source.size();
}

char Lexer::advance() {
    char c = source[current++];
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const {
    if (current + 1 >= source.size()) return '\0';
    return source[current + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) return false;
    advance();
    return true;
}

void Lexer::addToken(TokenType type) {
    addToken(type, source.substr(start, current - start));
}

void Lexer::addToken(TokenType type, std::string lexeme) {
    tokens.emplace_back(type, std::move(lexeme), startLine, startColumn);
}

void Lexer::error(const std::string& message) {
    std::string formattedError = "[line " + std::to_string(startLine) + ", col " + std::to_string(startColumn) + "] Lex error: " + message;
    errors.push_back(formattedError);
}

bool Lexer::hasErrors() const {
    return !errors.empty();
}

const std::vector<std::string>& Lexer::getErrors() const {
    return errors;
}

void Lexer::scanToken() {
    char c = advance();

    switch (c) {
        case ' ':
        case '\r':
        case '\t':
        case '\n':
            break;
        case '(': addToken(TokenType::L_PAREN); break;
        case ')': addToken(TokenType::R_PAREN); break;
        case '{': addToken(TokenType::L_BRACE); break;
        case '}': addToken(TokenType::R_BRACE); break;
        case '[': addToken(TokenType::L_BRACKET); break;
        case ']': addToken(TokenType::R_BRACKET); break;
        case '<':
            if (match('=')) addToken(TokenType::LESS_EQUAL);
            else if (match('<')) addToken(TokenType::LEFT_SHIFT);
            else addToken(TokenType::L_ANGLE);
            break;
        case '>':
            if (match('=')) addToken(TokenType::GREATER_EQUAL);
            else if (match('>')) addToken(TokenType::RIGHT_SHIFT);
            else addToken(TokenType::R_ANGLE);
            break;
        case ',': addToken(TokenType::COMMA); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case ':':
            addToken(match('=') ? TokenType::COLON_EQUAL : TokenType::COLON);
            break;
        case '.':
            if (peek() == '.' && peekNext() == '.') {
                advance(); advance();
                addToken(TokenType::ELLIPSIS);
            } else {
                addToken(TokenType::DOT);
            }
            break;
        case '@': addToken(TokenType::AT); break;
        case '$':
            if (peek() == '"' && peekNext() == '"' && current + 2 < source.size() && source[current + 2] == '"') {
                advance(); advance(); advance();
                scanTripleString(true);
            } else if (peek() == '"') {
                advance();
                scanTemplateString();
            } else {
                addToken(TokenType::DOLLAR);
            }
            break;
        case '+':
            if (match('+')) addToken(TokenType::PLUS_PLUS);
            else if (match('=')) addToken(TokenType::PLUS_EQUAL);
            else addToken(TokenType::PLUS);
            break;
        case '-':
            if (match('-')) addToken(TokenType::MINUS_MINUS);
            else if (match('=')) addToken(TokenType::MINUS_EQUAL);
            else addToken(TokenType::MINUS);
            break;
        case '*':
            addToken(match('=') ? TokenType::MULT_EQUAL : TokenType::MULT);
            break;
        case '%':
            addToken(match('=') ? TokenType::MOD_EQUAL : TokenType::MOD);
            break;
        case '/':
            if (match('/')) {
                while (peek() != '\n' && !isAtEnd()) advance();
            } else if (match('*')) {
                while (!(peek() == '*' && peekNext() == '/') && !isAtEnd()) {
                    advance();
                }
                if (isAtEnd()) {
                    error("Unterminated block comment.");
                } else {
                    advance();
                    advance();
                }
            } else if (match('=')) {
                addToken(TokenType::DIV_EQUAL);
            } else {
                addToken(TokenType::DIV);
            }
            break;
        case '^':
            // ^ is exponent, ^^ is bitwise XOR
            if (match('^')) addToken(TokenType::BITWISE_XOR);
            else if (match('=')) addToken(TokenType::EXPONENT_EQUAL);
            else addToken(TokenType::EXPONENT);
            break;
        case '=':
            addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
            break;
        case '!':
            addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::NOT);
            break;
        case '&':
            addToken(match('&') ? TokenType::AND_AND : TokenType::BITWISE_AND);
            break;
        case '|':
            if (match('|')) addToken(TokenType::OR_OR);
            else if (match('>')) addToken(TokenType::FUNC_PIPE);
            else addToken(TokenType::BITWISE_OR);
            break;
        case '~':
            addToken(TokenType::BITWISE_NOT);
            break;
        case '?':
            if (match('?')) addToken(TokenType::QUESTION_QUESTION);
            else if (match('.')) addToken(TokenType::QUESTION_DOT);
            else if (match('=')) addToken(TokenType::QUESTION_EQUAL);
            else if (match('!')) addToken(TokenType::QUESTION_BANG);
            else addToken(TokenType::QUESTION);
            break;
        case '"':
            if (peek() == '"' && peekNext() == '"') {
                advance(); advance();
                scanTripleString(false);
            } else {
                scanString();
            }
            break;
        case '\'':
            scanChar();
            break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                scanNumber();
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                scanIdentifierOrKeyword();
            } else {
                error(std::string("unexpected character '") + c + "'");
            }
            break;
    }
}

char Lexer::translateEscape(char escape) {
    switch (escape) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '0':  return '\0';
        case '\\': return '\\';
        case '"':  return '"';
        case '\'': return '\'';
        case '{':  return '{';
        case '}':  return '}';
        case 'x': {
            // \x1b -- a character by its two-digit hex code
            std::string digits;
            while (digits.size() < 2 && std::isxdigit(static_cast<unsigned char>(peek()))) digits += advance();
            if (digits.size() != 2) {
                error("\\x needs two hex digits, e.g. \\x1b");
                return 'x';
            }
            return static_cast<char>(std::stoi(digits, nullptr, 16));
        }
        default:
            error(std::string("unknown escape sequence '\\") + escape + "'.");
            return escape;
    }
}

namespace {

// Escapes in a triple-quoted string: \n \t \r \0 \\ \" \' \{ \} \xHH.
// Unknown ones are kept as written (\d stays \d).
std::string unescapeRaw(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out += raw[i];
            continue;
        }
        char e = raw[i + 1];
        switch (e) {
            case 'n': out += '\n'; ++i; break;
            case 't': out += '\t'; ++i; break;
            case 'r': out += '\r'; ++i; break;
            case '0': out += '\0'; ++i; break;
            case '\\': case '"': case '\'': case '{': case '}': out += e; ++i; break;
            case 'x':
                if (i + 3 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 2])) &&
                    std::isxdigit(static_cast<unsigned char>(raw[i + 3]))) {
                    out += static_cast<char>(std::stoi(raw.substr(i + 2, 2), nullptr, 16));
                    i += 3;
                    break;
                }
                out += raw[i];
                break;
            default: out += raw[i]; break;
        }
    }
    return out;
}

// C#-style layout of a triple-quoted string: the line break right after the
// opening """ is dropped, and if the closing """ is on its own line, that
// line is dropped and its indentation is removed from every line.
std::string dedentTriple(const std::string& raw) {
    std::string text = raw;
    size_t start = text.find_first_not_of(" \t");
    if (start != std::string::npos && (text[start] == '\n' || text[start] == '\r')) {
        if (text[start] == '\r') ++start;
        text = text.substr(start + 1);
    }
    size_t lastBreak = text.find_last_of('\n');
    if (lastBreak == std::string::npos) return text;
    std::string indent = text.substr(lastBreak + 1);
    if (indent.find_first_not_of(" \t") != std::string::npos) return text; // closing """ follows text
    text = text.substr(0, lastBreak);
    if (!text.empty() && text.back() == '\r') text.pop_back();

    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (line.compare(0, indent.size(), indent) == 0) line = line.substr(indent.size());
        else line = line.substr(std::min(line.find_first_not_of(" \t"), line.size())); // less indented: trim
        out += line;
        if (end == std::string::npos) break;
        out += '\n';
        pos = end + 1;
    }
    return out;
}

} // namespace

// """...""" and $"""...""": may span lines.
void Lexer::scanTripleString(bool isTemplate) {
    std::string raw;
    while (!isAtEnd() && !(peek() == '"' && peekNext() == '"' && current + 2 < source.size() && source[current + 2] == '"')) {
        if (peek() == '\\') {
            raw += advance();
            if (!isAtEnd()) raw += advance();
        } else {
            raw += advance();
        }
    }
    if (isAtEnd()) {
        error("Unterminated \"\"\" string (it needs a closing \"\"\").");
        return;
    }
    advance(); advance(); advance();
    raw.erase(std::remove(raw.begin(), raw.end(), '\r'), raw.end()); // Windows line endings
    std::string text = dedentTriple(raw);
    if (isTemplate) {
        // The parser splits out {interpolations} and handles escapes.
        std::string oneLine;
        for (char c : text) oneLine += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("") : std::string(1, c);
        addToken(TokenType::TEMPLATE_STRING, oneLine);
    } else {
        addToken(TokenType::STRING, unescapeRaw(text));
    }
}

void Lexer::scanString() {
    std::string value;
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') break;
        if (peek() == '\\') {
            advance();
            if (isAtEnd()) break;
            value += translateEscape(advance());
        } else {
            value += advance();
        }
    }

    if (isAtEnd() || peek() != '"') {
        error("Unterminated string literal.");
        return;
    }

    advance();
    addToken(TokenType::STRING, value);
}

// Template strings keep their raw text (escapes included) so the parser
// can tell an escaped \{ apart from an interpolation brace. The parser
// unescapes the literal parts once it has split them out.
void Lexer::scanTemplateString() {
    std::string value;
    int depth = 0; // inside {interpolation}: quotes start nested literals
    while ((peek() != '"' || depth > 0) && !isAtEnd()) {
        if (peek() == '\n') break;
        char c = peek();
        if (c == '\\') {
            value += advance();
            if (!isAtEnd()) value += advance();
        } else if (depth > 0 && (c == '"' || c == '\'')) {
            char quote = advance();
            value += quote;
            while (!isAtEnd() && peek() != quote && peek() != '\n') {
                if (peek() == '\\') value += advance();
                if (!isAtEnd()) value += advance();
            }
            if (peek() == quote) value += advance();
        } else {
            if (c == '{') ++depth;
            else if (c == '}' && depth > 0) --depth;
            value += advance();
        }
    }

    if (isAtEnd() || peek() != '"') {
        error("Unterminated template string.");
        return;
    }

    advance();
    addToken(TokenType::TEMPLATE_STRING, value);
}

void Lexer::scanNumber() {
    char firstDigit = source[start];

    // Base prefixes are strictly lowercase: 0b1001, 0o32, 0xf8
    if (firstDigit == '0' && (peek() == 'x' || peek() == 'b' || peek() == 'o')) {
        char prefix = advance();
        TokenType type = prefix == 'x' ? TokenType::HEX_LITERAL
                       : prefix == 'b' ? TokenType::BINARY_LITERAL
                                       : TokenType::OCTAL_LITERAL;
        const char* baseName = prefix == 'x' ? "hexadecimal" : prefix == 'b' ? "binary" : "octal";

        size_t digitsStart = current;
        while (std::isalnum(static_cast<unsigned char>(peek()))) {
            char d = peek();
            bool valid = prefix == 'x' ? std::isxdigit(static_cast<unsigned char>(d)) != 0
                       : prefix == 'b' ? (d == '0' || d == '1')
                                       : (d >= '0' && d <= '7');
            if (!valid) error(std::string("invalid digit '") + d + "' in " + baseName + " literal.");
            advance();
        }
        if (current == digitsStart) error(std::string("missing digits after '0") + prefix + "' prefix.");
        addToken(type, source.substr(start, current - start));
        return;
    }

    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();

    bool isDecimal = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        isDecimal = true;
        advance(); // consume '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    // 'f' suffix -> Flt, 'd' suffix -> Dbl, plain decimal -> Dbl
    auto endsWord = [this]() {
        char n = peekNext();
        return !(std::isalnum(static_cast<unsigned char>(n)) || n == '_');
    };
    if (peek() == 'f' && endsWord()) {
        advance();
        addToken(TokenType::FLOAT_LITERAL);
    } else if (peek() == 'd' && endsWord()) {
        advance();
        addToken(TokenType::DOUBLE_LITERAL);
    } else if (isDecimal) {
        addToken(TokenType::DOUBLE_LITERAL);
    } else {
        addToken(TokenType::INT_LITERAL);
    }

    if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
        error("invalid suffix on numeric literal.");
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    }
}

void Lexer::scanIdentifierOrKeyword() {
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        advance();
    }

    std::string text = source.substr(start, current - start);

    auto it = keywords.find(text);
    if (it != keywords.end()) {
        addToken(it->second);
    } else {
        addToken(TokenType::IDENTIFIER, text);
    }
}

void Lexer::scanChar() {
    std::string value;

    if (peek() == '\'' || peek() == '\n' || isAtEnd()) {
        error("Empty char literal.");
        if (peek() == '\'') advance();
        addToken(TokenType::CHAR_LITERAL, std::string(1, '\0'));
        return;
    }

    if (peek() == '\\') {
        advance();
        value += translateEscape(advance());
    } else {
        // One character, which may be several UTF-8 bytes: 'é', '😀'
        unsigned char lead = static_cast<unsigned char>(advance());
        value += static_cast<char>(lead);
        int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
        for (int k = 0; k < extra && !isAtEnd() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80; ++k) {
            value += advance();
        }
    }

    if (peek() != '\'') {
        error("Unterminated or multi-character char literal.");
        while (peek() != '\'' && peek() != '\n' && !isAtEnd()) {
            advance();
        }
    }

    if (peek() == '\'') advance();

    addToken(TokenType::CHAR_LITERAL, value);
}

} // namespace dessa
