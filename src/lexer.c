/*
 * lexer.c - Tokenizer implementation
 */

#include "lexer.h"
#include "common.h"
#include <string.h>

typedef struct {
    const char* source;     // Original source (for diagnostics)
    const char* start;      // Start of current token
    const char* current;    // Current position
    const char* lineStart;  // Start of current line (for column calc)
    int line;
} Lexer;

static Lexer lexer;

void initLexer(const char* source) {
    lexer.source = source;
    lexer.start = source;
    lexer.current = source;
    lexer.lineStart = source;
    lexer.line = 1;
}

const char* getLexerSource(void) {
    return lexer.source;
}

static bool isAtEnd(void) {
    return *lexer.current == '\0';
}

static char advance(void) {
    lexer.current++;
    return lexer.current[-1];
}

static char peek(void) {
    return *lexer.current;
}

static char peekNext(void) {
    if (isAtEnd()) return '\0';
    return lexer.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (int)(lexer.current - lexer.start);
    token.line = lexer.line;
    token.column = (int)(lexer.start - lexer.lineStart) + 1;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = 1;  /* Error tokens highlight just one character */
    token.line = lexer.line;
    token.column = (int)(lexer.current - lexer.lineStart);
    return token;
}

static void skipWhitespace(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                lexer.line++;
                advance();
                lexer.lineStart = lexer.current;
                break;
            case '-':
                // Lua comments: -- or --[[ ]]
                if (peekNext() == '-') {
                    advance(); advance();  // consume --
                    
                    // Check for block comment --[[
                    if (peek() == '[' && peekNext() == '[') {
                        advance(); advance();  // consume [[
                        while (!isAtEnd()) {
                            if (peek() == ']' && peekNext() == ']') {
                                advance(); advance();
                                break;
                            }
                            if (peek() == '\n') {
                                lexer.line++;
                                advance();
                                lexer.lineStart = lexer.current;
                            } else {
                                advance();
                            }
                        }
                    } else {
                        // Line comment
                        while (peek() != '\n' && !isAtEnd()) advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (lexer.current - lexer.start == start + length &&
        memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

/* Keyword trie - check identifier against reserved words */
static TokenType identifierType(void) {
    switch (lexer.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': 
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
            break;
        case 'd': return checkKeyword(1, 1, "o", TOKEN_DO);
        case 'e':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'l':
                        if (lexer.current - lexer.start > 2) {
                            switch (lexer.start[2]) {
                                case 's':
                                    if (lexer.current - lexer.start > 3 && lexer.start[3] == 'e') {
                                        if (lexer.current - lexer.start == 4) return TOKEN_ELSE;
                                        return checkKeyword(4, 2, "if", TOKEN_ELSEIF);
                                    }
                            }
                        }
                        break;
                    case 'n': return checkKeyword(2, 1, "d", TOKEN_END);
                    case 'x': return checkKeyword(2, 5, "tends", TOKEN_EXTENDS);
                }
            }
            break;
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 6, "nction", TOKEN_FUNCTION);
                }
            }
            break;
        case 'i':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'f': if (lexer.current - lexer.start == 2) return TOKEN_IF; break;
                    case 'n': if (lexer.current - lexer.start == 2) return TOKEN_IN; break;
                    case 'm': return checkKeyword(2, 8, "plements", TOKEN_IMPLEMENTS);
                }
            }
            break;
        case 'l': return checkKeyword(1, 4, "ocal", TOKEN_LOCAL);
        case 'n':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'e': return checkKeyword(2, 1, "w", TOKEN_NEW);
                    case 'i': return checkKeyword(2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 6, "rivate", TOKEN_PRIVATE);
        case 'r':
            if (lexer.current - lexer.start > 2) {
                switch (lexer.start[2]) {
                    case 'p': return checkKeyword(1, 5, "epeat", TOKEN_REPEAT);
                    case 't': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
                }
            }
            break;
        case 's':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'e': return checkKeyword(2, 2, "lf", TOKEN_SELF);
                    case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'h': return checkKeyword(2, 2, "en", TOKEN_THEN);
                    case 'r':
                        if (lexer.current - lexer.start > 2) {
                            switch (lexer.start[2]) {
                                case 'u': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                                case 'a': return checkKeyword(2, 3, "ait", TOKEN_TRAIT);
                            }
                        }
                        break;
                }
            }
            break;
        case 'u': return checkKeyword(1, 4, "ntil", TOKEN_UNTIL);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number(void) {
    while (isDigit(peek())) advance();
    
    // Decimal part
    if (peek() == '.' && isDigit(peekNext())) {
        advance();  // consume '.'
        while (isDigit(peek())) advance();
    }
    
    // Exponent
    if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (isDigit(peek())) advance();
    }
    
    return makeToken(TOKEN_NUMBER);
}

static Token string(char quote) {
    while (peek() != quote && !isAtEnd()) {
        if (peek() == '\n') {
            lexer.line++;
            advance();
            lexer.lineStart = lexer.current;
        } else {
            if (peek() == '\\' && peekNext() != '\0') advance();  // escape sequence
            advance();
        }
    }
    
    if (isAtEnd()) return errorToken("Unterminated string.");
    
    advance();  // closing quote
    return makeToken(TOKEN_STRING);
}

/* Long string [[...]] */
static Token longString(void) {
    while (!isAtEnd()) {
        if (peek() == ']' && peekNext() == ']') {
            advance(); advance();
            return makeToken(TOKEN_STRING);
        }
        if (peek() == '\n') {
            lexer.line++;
            advance();
            lexer.lineStart = lexer.current;
        } else {
            advance();
        }
    }
    return errorToken("Unterminated long string.");
}

Token scanToken(void) {
    skipWhitespace();
    lexer.start = lexer.current;
    
    if (isAtEnd()) return makeToken(TOKEN_EOF);
    
    char c = advance();
    
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();
    
    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case '[':
            if (peek() == '[') {
                advance();
                return longString();
            }
            return makeToken(TOKEN_LEFT_BRACKET);
        case ']': return makeToken(TOKEN_RIGHT_BRACKET);
        case ',': return makeToken(TOKEN_COMMA);
        case ':': return makeToken(TOKEN_COLON);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case '+': return makeToken(TOKEN_PLUS);
        case '-': return makeToken(TOKEN_MINUS);
        case '*': return makeToken(TOKEN_STAR);
        case '/': return makeToken(TOKEN_SLASH);
        case '%': return makeToken(TOKEN_PERCENT);
        case '#': return makeToken(TOKEN_HASH);
        case '~': return makeToken(match('=') ? TOKEN_TILDE_EQUAL : TOKEN_TILDE);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '.':
            if (match('.')) {
                return makeToken(match('.') ? TOKEN_DOT_DOT_DOT : TOKEN_DOT_DOT);
            }
            return makeToken(TOKEN_DOT);
        case '"': return string('"');
        case '\'': return string('\'');
    }
    
    return errorToken("Unexpected character.");
}
