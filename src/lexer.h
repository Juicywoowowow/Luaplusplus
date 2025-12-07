/*
 * lexer.h - Tokenizer for Lua++
 * 
 * Scans source code into tokens. Extends Lua with OOP keywords:
 * class, extends, new, super, private
 */

#ifndef luapp_lexer_h
#define luapp_lexer_h

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON, TOKEN_COLON,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_HASH,             // # (length operator)
    
    // One or two character tokens
    TOKEN_TILDE,            // ~ (bitwise not, or part of ~=)
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_TILDE_EQUAL,      // ~= (not equal in Lua)
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_DOT_DOT,          // .. (concat)
    TOKEN_DOT_DOT_DOT,      // ... (varargs)
    
    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    
    // Keywords (Lua)
    TOKEN_AND, TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_DO, TOKEN_ELSE, TOKEN_ELSEIF,
    TOKEN_END, TOKEN_FALSE, TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF,
    TOKEN_IN, TOKEN_LOCAL, TOKEN_NIL, TOKEN_NOT, TOKEN_OR,
    TOKEN_REPEAT, TOKEN_RETURN, TOKEN_THEN, TOKEN_TRUE, TOKEN_UNTIL,
    TOKEN_WHILE,
    
    // Keywords (Lua++ OOP extensions)
    TOKEN_CLASS,
    TOKEN_EXTENDS,
    TOKEN_NEW,
    TOKEN_SUPER,
    TOKEN_SELF,
    TOKEN_PRIVATE,
    TOKEN_TRAIT,
    TOKEN_IMPLEMENTS,
    
    // Special
    TOKEN_ERROR,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char* start;  // Pointer into source
    int length;
    int line;
    int column;         // Column number (1-indexed)
} Token;

void initLexer(const char* source);
Token scanToken(void);

/* Get the source pointer for diagnostic context */
const char* getLexerSource(void);

#endif
