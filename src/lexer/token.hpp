#pragma once

#include <string>
#include <cstdint>
#include <utility>

namespace dessa {

enum class TokenType {
    // ---- Literals ----
    IDENTIFIER,         // name, age, MyClass, add
    STRING,             // "Hello World"
    TEMPLATE_STRING,    // $"Hello, I am {self.name}."
    INT_LITERAL,        // 21, 20
    FLOAT_LITERAL,      // 9.23f (explicit Flt suffix)
    DOUBLE_LITERAL,     // 9.23 / 95.6d (unsuffixed decimals are Dbl, but may
                        // initialise a Flt -- see Analyser::literalFits)
    CHAR_LITERAL,       // 'A'
    BINARY_LITERAL,     // 0b1001
    OCTAL_LITERAL,      // 0o32
    HEX_LITERAL,        // 0xf8

    // ---- Keywords: declarations ----
    VAR, CONST, FUNC, CLASS, INTERFACE, ENUM,

    // ---- Keywords: built-in types ----
    TYPE_STR, TYPE_INT, TYPE_BUL, TYPE_FLT, TYPE_DBL, TYPE_CHR,
    TYPE_EMP, TYPE_NIL,

    // ---- Keywords: control flow ----
    IF, ELSE, SWITCH, CASE, DEFAULT,
    FOR, WHILE, LOOP, BREAK, CONTINUE, RETURN, IN,
    TRY, CATCH, THROW,

    // ---- Keywords: module system ----
    FROM, AS,

    // ---- Keywords: OOP / misc ----
    NEW, SELF, TRUE, FALSE, NIL, INIT, SUPER,

    // ---- Punctuation: grouping ----
    L_PAREN, R_PAREN,       // ( )
    L_BRACE, R_BRACE,       // { }
    L_BRACKET, R_BRACKET,   // [ ]
    L_ANGLE, R_ANGLE,       // < >

    // ---- Punctuation: separators ----
    COMMA, COLON, SEMICOLON, DOT, DOLLAR, // , : ; . $

    // ---- Operators: arithmetic ----
    PLUS, MINUS, MULT, DIV, EXPONENT, MOD, // + - * / ^ %

    // ---- Operators: assignment ----
    EQUAL, PLUS_EQUAL, MINUS_EQUAL, MULT_EQUAL, DIV_EQUAL, EXPONENT_EQUAL, MOD_EQUAL, 

    // ---- Operators: increment / decrement ----
    PLUS_PLUS, MINUS_MINUS, // ++ --

    // ---- Operators: comparison ----
    EQUAL_EQUAL, BANG_EQUAL, GREATER_EQUAL, LESS_EQUAL, // == != >= <=

    // ---- Operators: logical ----
    AND_AND, OR_OR, NOT, // && || !

    // ---- Operators: bitwise ----
    BITWISE_AND, BITWISE_OR, BITWISE_XOR, LEFT_SHIFT, RIGHT_SHIFT, BITWISE_NOT, 

    // ---- Operators: nullish / optional / ternary / lambda ----
    QUESTION_QUESTION,   // ??
    QUESTION_DOT,        // ?.
    QUESTION,            // ?
    COLON_EQUAL,         // :=
    ELLIPSIS,            // ...

    // ---- RESERVED TOKENS ----     | These tokens may be used in the future, but are not currently used in the language. ----
    QUESTION_EQUAL, QUESTION_BANG, AT, FUNC_PIPE, WITH, IS, // ?= ?! @ |> with is

    // ---- End of stream ----
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme; 
    
    int line;
    int column;

    Token(TokenType type, std::string lexeme, int line, int column)
        : type(type), lexeme(std::move(lexeme)), line(line), column(column) {}
};

} // namespace dessa