/*
 * test_lexer.cpp - Comprehensive lexer tests for Lua++
 * 
 * Tests tokenization including edge cases, malformed input,
 * and adversarial patterns that might trip up the lexer.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>

extern "C" {
#include "lexer.h"
}

// Helper to collect all tokens from source
std::vector<Token> tokenize(const char* source) {
    std::vector<Token> tokens;
    initLexer(source);
    Token token;
    do {
        token = scanToken();
        tokens.push_back(token);
    } while (token.type != TOKEN_EOF);
    return tokens;
}

// Helper to get token text
std::string tokenText(const Token& t) {
    return std::string(t.start, t.length);
}

// ============== Basic Token Tests ==============

class LexerBasicTest : public ::testing::Test {};

TEST_F(LexerBasicTest, EmptySource) {
    auto tokens = tokenize("");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TOKEN_EOF);
}

TEST_F(LexerBasicTest, WhitespaceOnly) {
    auto tokens = tokenize("   \t\n\r  \n\n  ");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TOKEN_EOF);
}

TEST_F(LexerBasicTest, SingleCharacterTokens) {
    auto tokens = tokenize("( ) { } [ ] , . ; : + - * / % #");
    
    std::vector<TokenType> expected = {
        TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
        TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
        TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
        TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON, TOKEN_COLON,
        TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
        TOKEN_HASH, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]) << "Mismatch at index " << i;
    }
}

TEST_F(LexerBasicTest, TwoCharacterTokens) {
    auto tokens = tokenize("== ~= <= >= .. ...");
    
    std::vector<TokenType> expected = {
        TOKEN_EQUAL_EQUAL, TOKEN_TILDE_EQUAL,
        TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
        TOKEN_DOT_DOT, TOKEN_DOT_DOT_DOT, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]) << "Mismatch at index " << i;
    }
}

TEST_F(LexerBasicTest, SingleVsDoubleCharTokens) {
    // Test that single char tokens work when not followed by matching char
    auto tokens = tokenize("= < > ~ .");
    
    std::vector<TokenType> expected = {
        TOKEN_EQUAL, TOKEN_LESS, TOKEN_GREATER, TOKEN_TILDE, TOKEN_DOT, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]);
    }
}

// ============== Keyword Tests ==============

class LexerKeywordTest : public ::testing::Test {};

TEST_F(LexerKeywordTest, LuaKeywords) {
    auto tokens = tokenize("and break do else elseif end false for function if in local nil not or repeat return then true until while");
    
    std::vector<TokenType> expected = {
        TOKEN_AND, TOKEN_BREAK, TOKEN_DO, TOKEN_ELSE, TOKEN_ELSEIF,
        TOKEN_END, TOKEN_FALSE, TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF,
        TOKEN_IN, TOKEN_LOCAL, TOKEN_NIL, TOKEN_NOT, TOKEN_OR,
        TOKEN_REPEAT, TOKEN_RETURN, TOKEN_THEN, TOKEN_TRUE, TOKEN_UNTIL,
        TOKEN_WHILE, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]) << "Keyword mismatch at index " << i;
    }
}

TEST_F(LexerKeywordTest, LuaPlusPlusKeywords) {
    auto tokens = tokenize("class extends new super self private");
    
    std::vector<TokenType> expected = {
        TOKEN_CLASS, TOKEN_EXTENDS, TOKEN_NEW, TOKEN_SUPER, TOKEN_SELF, TOKEN_PRIVATE, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]);
    }
}

TEST_F(LexerKeywordTest, KeywordPrefixesAreIdentifiers) {
    // Words that start like keywords but aren't
    auto tokens = tokenize("andy breaking done elsewhere ending falsely fortune functional iffy inner locally nilly notify oracle repeated returning thence truthy untilnow whileloop");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER) << "Token " << i << " should be identifier";
    }
}

TEST_F(LexerKeywordTest, KeywordCaseSensitivity) {
    // Keywords are case-sensitive - uppercase should be identifiers
    auto tokens = tokenize("AND Break DO ELSE Class EXTENDS");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER) << "Uppercase keyword should be identifier";
    }
}

TEST_F(LexerKeywordTest, KeywordsWithUnderscores) {
    // Keywords followed by underscore become identifiers
    auto tokens = tokenize("class_ _class for_ _for");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER);
    }
}

// ============== Number Tests ==============

class LexerNumberTest : public ::testing::Test {};

TEST_F(LexerNumberTest, Integers) {
    auto tokens = tokenize("0 1 42 12345 999999");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_NUMBER);
    }
}

TEST_F(LexerNumberTest, Decimals) {
    auto tokens = tokenize("0.0 1.5 3.14159 0.001 123.456");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_NUMBER);
    }
}

TEST_F(LexerNumberTest, ScientificNotation) {
    auto tokens = tokenize("1e10 1E10 1e+10 1e-10 1.5e10 3.14e-5");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_NUMBER);
    }
}

TEST_F(LexerNumberTest, LeadingZeros) {
    auto tokens = tokenize("007 00123 000");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_NUMBER);
    }
}

TEST_F(LexerNumberTest, NumberFollowedByDot) {
    // 123. should be number then dot (not decimal)
    auto tokens = tokenize("123.abc");
    
    EXPECT_EQ(tokens[0].type, TOKEN_NUMBER);
    EXPECT_EQ(tokenText(tokens[0]), "123");
    EXPECT_EQ(tokens[1].type, TOKEN_DOT);
    EXPECT_EQ(tokens[2].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerNumberTest, NumberFollowedByDoubleDot) {
    // 1..10 should be number, dotdot, number (range-like)
    auto tokens = tokenize("1..10");
    
    EXPECT_EQ(tokens[0].type, TOKEN_NUMBER);
    EXPECT_EQ(tokens[1].type, TOKEN_DOT_DOT);
    EXPECT_EQ(tokens[2].type, TOKEN_NUMBER);
}

// ============== String Tests ==============

class LexerStringTest : public ::testing::Test {};

TEST_F(LexerStringTest, DoubleQuotedStrings) {
    auto tokens = tokenize("\"hello\" \"world\" \"\"");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_STRING);
    EXPECT_EQ(tokens[2].type, TOKEN_STRING);
}

TEST_F(LexerStringTest, SingleQuotedStrings) {
    auto tokens = tokenize("'hello' 'world' ''");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_STRING);
    EXPECT_EQ(tokens[2].type, TOKEN_STRING);
}

TEST_F(LexerStringTest, StringWithEscapes) {
    auto tokens = tokenize("\"hello\\nworld\" \"tab\\there\" \"quote\\\"inside\"");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_STRING);
    }
}

TEST_F(LexerStringTest, StringWithNewlines) {
    auto tokens = tokenize("\"line1\nline2\"");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    // Line number is at end of token (after newline)
    EXPECT_EQ(tokens[0].line, 2);
}

TEST_F(LexerStringTest, LongStrings) {
    auto tokens = tokenize("[[hello world]] [[multi\nline\nstring]]");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_STRING);
}

TEST_F(LexerStringTest, UnterminatedString) {
    auto tokens = tokenize("\"unterminated");
    
    EXPECT_EQ(tokens[0].type, TOKEN_ERROR);
}

TEST_F(LexerStringTest, UnterminatedLongString) {
    auto tokens = tokenize("[[unterminated long string");
    
    EXPECT_EQ(tokens[0].type, TOKEN_ERROR);
}

TEST_F(LexerStringTest, EmptyLongString) {
    auto tokens = tokenize("[[]]");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
}

// ============== Comment Tests ==============

class LexerCommentTest : public ::testing::Test {};

TEST_F(LexerCommentTest, LineComment) {
    auto tokens = tokenize("x -- this is a comment\ny");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokenText(tokens[0]), "x");
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokenText(tokens[1]), "y");
}

TEST_F(LexerCommentTest, BlockComment) {
    auto tokens = tokenize("x --[[ block comment ]] y");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerCommentTest, MultilineBlockComment) {
    auto tokens = tokenize("x --[[\nmulti\nline\n]] y");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerCommentTest, CommentAtEndOfFile) {
    auto tokens = tokenize("x -- comment at end");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_EOF);
}

TEST_F(LexerCommentTest, OnlyComment) {
    auto tokens = tokenize("-- just a comment");
    
    EXPECT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TOKEN_EOF);
}

// ============== Identifier Tests ==============

class LexerIdentifierTest : public ::testing::Test {};

TEST_F(LexerIdentifierTest, SimpleIdentifiers) {
    auto tokens = tokenize("x foo bar123 _private __dunder__");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER);
    }
}

TEST_F(LexerIdentifierTest, IdentifiersWithNumbers) {
    auto tokens = tokenize("x1 var2 test123 a1b2c3");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER);
    }
}

TEST_F(LexerIdentifierTest, UnderscoreIdentifiers) {
    auto tokens = tokenize("_ __ ___ _a a_ a_b");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER);
    }
}

TEST_F(LexerIdentifierTest, LongIdentifier) {
    auto tokens = tokenize("thisIsAVeryLongIdentifierNameThatShouldStillWork");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
}

// ============== Line Number Tests ==============

class LexerLineNumberTest : public ::testing::Test {};

TEST_F(LexerLineNumberTest, SingleLine) {
    auto tokens = tokenize("x y z");
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].line, 1);
    }
}

TEST_F(LexerLineNumberTest, MultipleLines) {
    auto tokens = tokenize("x\ny\nz");
    
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 2);
    EXPECT_EQ(tokens[2].line, 3);
}

TEST_F(LexerLineNumberTest, BlankLines) {
    auto tokens = tokenize("x\n\n\ny");
    
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 4);
}

TEST_F(LexerLineNumberTest, LineNumberAfterBlockComment) {
    auto tokens = tokenize("x --[[\ncomment\n]] y");
    
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 3);
}

// ============== Edge Cases & Adversarial Input ==============

class LexerEdgeCaseTest : public ::testing::Test {};

TEST_F(LexerEdgeCaseTest, UnexpectedCharacters) {
    auto tokens = tokenize("@");
    EXPECT_EQ(tokens[0].type, TOKEN_ERROR);
    
    tokens = tokenize("$");
    EXPECT_EQ(tokens[0].type, TOKEN_ERROR);
    
    tokens = tokenize("`");
    EXPECT_EQ(tokens[0].type, TOKEN_ERROR);
}

TEST_F(LexerEdgeCaseTest, TokensWithoutSpaces) {
    auto tokens = tokenize("x+y*z");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_PLUS);
    EXPECT_EQ(tokens[2].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[3].type, TOKEN_STAR);
    EXPECT_EQ(tokens[4].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerEdgeCaseTest, ConsecutiveOperators) {
    auto tokens = tokenize("+-*/");
    
    EXPECT_EQ(tokens[0].type, TOKEN_PLUS);
    EXPECT_EQ(tokens[1].type, TOKEN_MINUS);
    EXPECT_EQ(tokens[2].type, TOKEN_STAR);
    EXPECT_EQ(tokens[3].type, TOKEN_SLASH);
}

TEST_F(LexerEdgeCaseTest, DotSequences) {
    // Test various dot combinations
    auto tokens = tokenize(". .. ...");
    
    EXPECT_EQ(tokens[0].type, TOKEN_DOT);
    EXPECT_EQ(tokens[1].type, TOKEN_DOT_DOT);
    EXPECT_EQ(tokens[2].type, TOKEN_DOT_DOT_DOT);
}

TEST_F(LexerEdgeCaseTest, FourDots) {
    // .... should be ... followed by .
    auto tokens = tokenize("....");
    
    EXPECT_EQ(tokens[0].type, TOKEN_DOT_DOT_DOT);
    EXPECT_EQ(tokens[1].type, TOKEN_DOT);
}

TEST_F(LexerEdgeCaseTest, MixedQuoteStrings) {
    // Single quotes inside double quotes and vice versa
    auto tokens = tokenize("\"it's\" 'say \"hi\"'");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_STRING);
}

TEST_F(LexerEdgeCaseTest, NestedBrackets) {
    auto tokens = tokenize("[[[x]]]");
    
    // [[ starts long string, then x]], then ]
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);  // [[x]]
    EXPECT_EQ(tokens[1].type, TOKEN_RIGHT_BRACKET);
}

TEST_F(LexerEdgeCaseTest, MinusMinusNotComment) {
    // - - (with space) is two minus tokens, not a comment
    auto tokens = tokenize("x - - y");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_MINUS);
    EXPECT_EQ(tokens[2].type, TOKEN_MINUS);
    EXPECT_EQ(tokens[3].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerEdgeCaseTest, TildeAlone) {
    auto tokens = tokenize("~");
    EXPECT_EQ(tokens[0].type, TOKEN_TILDE);
}

TEST_F(LexerEdgeCaseTest, TildeEqual) {
    auto tokens = tokenize("~=");
    EXPECT_EQ(tokens[0].type, TOKEN_TILDE_EQUAL);
}

TEST_F(LexerEdgeCaseTest, NumberThenIdentifier) {
    // 123abc should be number then identifier
    auto tokens = tokenize("123abc");
    
    EXPECT_EQ(tokens[0].type, TOKEN_NUMBER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerEdgeCaseTest, VeryLongNumber) {
    auto tokens = tokenize("12345678901234567890");
    EXPECT_EQ(tokens[0].type, TOKEN_NUMBER);
}

TEST_F(LexerEdgeCaseTest, VeryLongString) {
    std::string longStr = "\"" + std::string(1000, 'a') + "\"";
    auto tokens = tokenize(longStr.c_str());
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
}

// ============== Malicious/Adversarial Input ==============

class LexerAdversarialTest : public ::testing::Test {};

TEST_F(LexerAdversarialTest, NullByteInSource) {
    // Source with embedded null - should stop at null
    const char source[] = "x\0y";
    auto tokens = tokenize(source);
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_EOF);  // Stops at null
}

TEST_F(LexerAdversarialTest, ManyConsecutiveNewlines) {
    std::string source = "x" + std::string(100, '\n') + "y";
    auto tokens = tokenize(source.c_str());
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].line, 101);
}

TEST_F(LexerAdversarialTest, ManyConsecutiveSpaces) {
    std::string source = "x" + std::string(1000, ' ') + "y";
    auto tokens = tokenize(source.c_str());
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerAdversarialTest, DeepNestedComments) {
    // Block comments don't nest in Lua, so ]] ends it
    auto tokens = tokenize("--[[ outer --[[ inner ]] still comment? ]] x");
    
    // After first ]], we're out of comment
    // "still comment? ]] x" should be tokenized
    bool foundIdentifier = false;
    for (const auto& t : tokens) {
        if (t.type == TOKEN_IDENTIFIER) foundIdentifier = true;
    }
    EXPECT_TRUE(foundIdentifier);
}

TEST_F(LexerAdversarialTest, StringLookingLikeCode) {
    auto tokens = tokenize("\"function end if then\"");
    
    // Should be single string, not keywords
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_EOF);
}

TEST_F(LexerAdversarialTest, CommentLookingLikeCode) {
    auto tokens = tokenize("-- function foo() return 1 end\nx");
    
    // Only x should be tokenized
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokenText(tokens[0]), "x");
}

TEST_F(LexerAdversarialTest, AlmostKeywords) {
    // Test strings that are very close to keywords
    auto tokens = tokenize("clas classe classs");  // not "class"
    
    for (size_t i = 0; i < tokens.size() - 1; i++) {
        EXPECT_EQ(tokens[i].type, TOKEN_IDENTIFIER);
    }
}

TEST_F(LexerAdversarialTest, UnicodeInIdentifier) {
    // Non-ASCII should cause error (not valid identifier char)
    auto tokens = tokenize("café");
    
    // 'caf' is valid, then é causes error
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokenText(tokens[0]), "caf");
}

TEST_F(LexerAdversarialTest, TabsAndSpacesMixed) {
    auto tokens = tokenize("x\t \t y");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerAdversarialTest, CarriageReturns) {
    auto tokens = tokenize("x\r\ny\rz");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TOKEN_IDENTIFIER);
}

// ============== Real Code Patterns ==============

class LexerRealCodeTest : public ::testing::Test {};

TEST_F(LexerRealCodeTest, FunctionDeclaration) {
    auto tokens = tokenize("function foo(x, y) return x + y end");
    
    std::vector<TokenType> expected = {
        TOKEN_FUNCTION, TOKEN_IDENTIFIER, TOKEN_LEFT_PAREN,
        TOKEN_IDENTIFIER, TOKEN_COMMA, TOKEN_IDENTIFIER, TOKEN_RIGHT_PAREN,
        TOKEN_RETURN, TOKEN_IDENTIFIER, TOKEN_PLUS, TOKEN_IDENTIFIER,
        TOKEN_END, TOKEN_EOF
    };
    
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].type, expected[i]);
    }
}

TEST_F(LexerRealCodeTest, ClassDeclaration) {
    auto tokens = tokenize("class Dog extends Animal\nfunction init(name)\nself.name = name\nend\nend");
    
    // Just verify key tokens are present
    EXPECT_EQ(tokens[0].type, TOKEN_CLASS);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TOKEN_EXTENDS);
    EXPECT_EQ(tokens[3].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerRealCodeTest, MethodCall) {
    auto tokens = tokenize("obj:method(arg1, arg2)");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_COLON);
    EXPECT_EQ(tokens[2].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[3].type, TOKEN_LEFT_PAREN);
}

TEST_F(LexerRealCodeTest, PropertyAccess) {
    auto tokens = tokenize("obj.field.subfield");
    
    EXPECT_EQ(tokens[0].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].type, TOKEN_DOT);
    EXPECT_EQ(tokens[2].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[3].type, TOKEN_DOT);
    EXPECT_EQ(tokens[4].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerRealCodeTest, NewExpression) {
    auto tokens = tokenize("local x = new MyClass(1, 2)");
    
    EXPECT_EQ(tokens[0].type, TOKEN_LOCAL);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TOKEN_EQUAL);
    EXPECT_EQ(tokens[3].type, TOKEN_NEW);
    EXPECT_EQ(tokens[4].type, TOKEN_IDENTIFIER);
}

TEST_F(LexerRealCodeTest, ForLoop) {
    auto tokens = tokenize("for i = 1, 10, 2 do print(i) end");
    
    EXPECT_EQ(tokens[0].type, TOKEN_FOR);
    EXPECT_EQ(tokens[1].type, TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TOKEN_EQUAL);
    EXPECT_EQ(tokens[3].type, TOKEN_NUMBER);
}

TEST_F(LexerRealCodeTest, StringConcatenation) {
    auto tokens = tokenize("\"hello\" .. \" \" .. \"world\"");
    
    EXPECT_EQ(tokens[0].type, TOKEN_STRING);
    EXPECT_EQ(tokens[1].type, TOKEN_DOT_DOT);
    EXPECT_EQ(tokens[2].type, TOKEN_STRING);
    EXPECT_EQ(tokens[3].type, TOKEN_DOT_DOT);
    EXPECT_EQ(tokens[4].type, TOKEN_STRING);
}

TEST_F(LexerRealCodeTest, ComparisonOperators) {
    auto tokens = tokenize("x == y and a ~= b or c < d and e <= f or g > h and i >= j");
    
    // Find and verify comparison tokens
    bool foundEqualEqual = false, foundTildeEqual = false;
    bool foundLess = false, foundLessEqual = false;
    bool foundGreater = false, foundGreaterEqual = false;
    
    for (const auto& t : tokens) {
        if (t.type == TOKEN_EQUAL_EQUAL) foundEqualEqual = true;
        if (t.type == TOKEN_TILDE_EQUAL) foundTildeEqual = true;
        if (t.type == TOKEN_LESS) foundLess = true;
        if (t.type == TOKEN_LESS_EQUAL) foundLessEqual = true;
        if (t.type == TOKEN_GREATER) foundGreater = true;
        if (t.type == TOKEN_GREATER_EQUAL) foundGreaterEqual = true;
    }
    
    EXPECT_TRUE(foundEqualEqual);
    EXPECT_TRUE(foundTildeEqual);
    EXPECT_TRUE(foundLess);
    EXPECT_TRUE(foundLessEqual);
    EXPECT_TRUE(foundGreater);
    EXPECT_TRUE(foundGreaterEqual);
}
