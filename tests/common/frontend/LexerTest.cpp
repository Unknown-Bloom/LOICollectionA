#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "LOICollectionA/frontend/Lexer.h"

using namespace LOICollection::frontend;

TEST(LexerTest, ParseNumbers) {
    DiagnosticEngine diagnostics;
    Lexer lexer("123 3.14", diagnostics);

    Token t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_INT);
    EXPECT_EQ(t1.value, "123");

    Token t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_FLOAT);
    EXPECT_EQ(t2.value, "3.14");

    EXPECT_EQ(lexer.getNextToken().type, TokenType::TOKEN_EOF);
}

TEST(LexerTest, ParseString) {
    DiagnosticEngine diagnostics;
    Lexer lexer("\"hello world\"", diagnostics);

    Token t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_STRING);
    EXPECT_EQ(t.value, "hello world");
}

TEST(LexerTest, ParseSingleQuotedString) {
    DiagnosticEngine diagnostics;
    Lexer lexer("'hello'", diagnostics);

    Token t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_STRING);
    EXPECT_EQ(t.value, "hello");
}

TEST(LexerTest, UnclosedString) {
    DiagnosticEngine diagnostics;
    Lexer lexer("\"abc", diagnostics);

    Token t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_EOF);
    EXPECT_TRUE(diagnostics.hasErrors());
    EXPECT_NE(diagnostics.getErrorMessage().find("Unclosed string"), std::string::npos);
}

TEST(LexerTest, ParseIdentifiersAndKeywords) {
    DiagnosticEngine diagnostics;
    Lexer lexer("if true false myVar", diagnostics);

    auto t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_IF);

    auto t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_BOOL_LIT);
    EXPECT_EQ(t2.value, "true");

    auto t3 = lexer.getNextToken();
    EXPECT_EQ(t3.type, TokenType::TOKEN_BOOL_LIT);
    EXPECT_EQ(t3.value, "false");

    auto t4 = lexer.getNextToken();
    EXPECT_EQ(t4.type, TokenType::TOKEN_IDENT);
    EXPECT_EQ(t4.value, "myVar");
}

TEST(LexerTest, ParseAllKeywords) {
    DiagnosticEngine diagnostics;
    Lexer lexer("class func new this super return public private extends instanceof static", diagnostics);

    std::vector<TokenType> expected = {
        TokenType::TOKEN_CLASS, TokenType::TOKEN_FUNC, TokenType::TOKEN_NEW,
        TokenType::TOKEN_THIS, TokenType::TOKEN_SUPER, TokenType::TOKEN_RETURN,
        TokenType::TOKEN_PUBLIC, TokenType::TOKEN_PRIVATE, TokenType::TOKEN_EXTENDS,
        TokenType::TOKEN_INSTANCEOF, TokenType::TOKEN_STATIC
    };

    for (auto expectedType : expected) {
        auto t = lexer.getNextToken();
        EXPECT_EQ(t.type, expectedType);
    }

    EXPECT_EQ(lexer.getNextToken().type, TokenType::TOKEN_EOF);
}

TEST(LexerTest, ParseNewKeywords) {
    DiagnosticEngine diagnostics;
    Lexer lexer("using None variant optional", diagnostics);

    auto t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_USING);
    EXPECT_EQ(t1.value, "using");

    auto t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_NONE);
    EXPECT_EQ(t2.value, "None");

    auto t3 = lexer.getNextToken();
    EXPECT_EQ(t3.type, TokenType::TOKEN_IDENT);
    EXPECT_EQ(t3.value, "variant");

    auto t4 = lexer.getNextToken();
    EXPECT_EQ(t4.type, TokenType::TOKEN_IDENT);
    EXPECT_EQ(t4.value, "optional");

    EXPECT_EQ(lexer.getNextToken().type, TokenType::TOKEN_EOF);
}

TEST(LexerTest, ParseOperators) {
    DiagnosticEngine diagnostics;
    Lexer lexer("+ - * / % ^ == != > < >= <= && || !", diagnostics);
    std::vector<TokenType> expected = {
        TokenType::TOKEN_PLUS,    TokenType::TOKEN_MINUS,   TokenType::TOKEN_MULTIPLY,
        TokenType::TOKEN_DIVIDE,  TokenType::TOKEN_MOD,     TokenType::TOKEN_POWER,
        TokenType::TOKEN_OP,      TokenType::TOKEN_OP,      TokenType::TOKEN_OP,
        TokenType::TOKEN_OP,      TokenType::TOKEN_OP,      TokenType::TOKEN_OP,
        TokenType::TOKEN_BOOL_OP, TokenType::TOKEN_BOOL_OP, TokenType::TOKEN_OP
    };

    for (auto expectedType : expected) {
        auto t = lexer.getNextToken();
        EXPECT_EQ(t.type, expectedType);
    }
}

TEST(LexerTest, ParseAdvancedOperators) {
    DiagnosticEngine diagnostics;
    Lexer lexer("-> && || == != >= <= = ! & | > <", diagnostics);

    struct Expected { TokenType type; std::string value; };
    std::vector<Expected> expected = {
        { TokenType::TOKEN_ARROW, "->" },
        { TokenType::TOKEN_BOOL_OP, "&&" },
        { TokenType::TOKEN_BOOL_OP, "||" },
        { TokenType::TOKEN_OP, "==" },
        { TokenType::TOKEN_OP, "!=" },
        { TokenType::TOKEN_OP, ">=" },
        { TokenType::TOKEN_OP, "<=" },
        { TokenType::TOKEN_OP, "=" },
        { TokenType::TOKEN_OP, "!" },
        { TokenType::TOKEN_OP, "&" },
        { TokenType::TOKEN_OP, "|" },
        { TokenType::TOKEN_OP, ">" },
        { TokenType::TOKEN_OP, "<" }
    };

    for (const auto& e : expected) {
        auto t = lexer.getNextToken();
        EXPECT_EQ(t.type, e.type);
        EXPECT_EQ(t.value, e.value);
    }
}

TEST(LexerTest, IdentifiersWithAdjacentArithmeticOperators) {
    DiagnosticEngine diagnostics;
    Lexer lexer("a+b a-b a*b a/b a%b a^b a->b", diagnostics);

    struct Expected { TokenType type; std::string value; };
    std::vector<Expected> expected = {
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_PLUS, "+" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_MINUS, "-" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_MULTIPLY, "*" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_DIVIDE, "/" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_MOD, "%" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_POWER, "^" },
        { TokenType::TOKEN_IDENT, "b" },
        { TokenType::TOKEN_IDENT, "a" },
        { TokenType::TOKEN_ARROW, "->" },
        { TokenType::TOKEN_IDENT, "b" },
    };

    for (const auto& e : expected) {
        auto t = lexer.getNextToken();
        EXPECT_EQ(t.type, e.type);
        EXPECT_EQ(t.value, e.value);
    }

    EXPECT_EQ(lexer.getNextToken().type, TokenType::TOKEN_EOF);
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(LexerTest, SkipLineComment) {
    DiagnosticEngine diagnostics;
    Lexer lexer("// comment\n42", diagnostics);

    auto t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_INT);
    EXPECT_EQ(t.value, "42");
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(LexerTest, SkipBlockComment) {
    DiagnosticEngine diagnostics;
    Lexer lexer("/* multi\nline */ 3.14", diagnostics);

    auto t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_FLOAT);
    EXPECT_EQ(t.value, "3.14");
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(LexerTest, UnclosedBlockComment) {
    DiagnosticEngine diagnostics;
    Lexer lexer("/* abc", diagnostics);

    auto t = lexer.getNextToken();
    EXPECT_EQ(t.type, TokenType::TOKEN_EOF);
    EXPECT_TRUE(diagnostics.hasErrors());
    EXPECT_NE(diagnostics.getErrorMessage().find("Unclosed block comment"), std::string::npos);
}

TEST(LexerTest, NumbersWithLeadingAndTrailingDot) {
    DiagnosticEngine diagnostics;
    Lexer lexer(".5 5.", diagnostics);

    auto t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_FLOAT);
    EXPECT_EQ(t1.value, ".5");

    auto t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_FLOAT);
    EXPECT_EQ(t2.value, "5.");
}

TEST(LexerTest, EmptyInput) {
    DiagnosticEngine diagnostics;
    Lexer lexer("", diagnostics);

    EXPECT_EQ(lexer.getNextToken().type, TokenType::TOKEN_EOF);
}

TEST(LexerTest, TrackLineAndColumn) {
    DiagnosticEngine diagnostics;
    Lexer lexer("1\n  2", diagnostics);

    auto t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_INT);
    EXPECT_EQ(t1.loc.line, 1u);
    EXPECT_EQ(t1.loc.column, 1u);

    auto t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_INT);
    EXPECT_EQ(t2.loc.line, 2u);
    EXPECT_EQ(t2.loc.column, 3u);
}

TEST(LexerTest, ParseColonAndNamespace) {
    DiagnosticEngine diagnostics;
    Lexer lexer(": ::", diagnostics);

    auto t1 = lexer.getNextToken();
    EXPECT_EQ(t1.type, TokenType::TOKEN_COLON);

    auto t2 = lexer.getNextToken();
    EXPECT_EQ(t2.type, TokenType::TOKEN_NAMESPACE);
    EXPECT_EQ(t2.value, "::");
}

TEST(LexerTest, PeekDoesNotAdvance) {
    DiagnosticEngine diagnostics;
    Lexer lexer("1 2", diagnostics);

    auto peeked = lexer.peekNextToken();
    EXPECT_EQ(peeked.type, TokenType::TOKEN_INT);

    auto real1 = lexer.getNextToken();
    EXPECT_EQ(real1.type, TokenType::TOKEN_INT);
    
    auto real2 = lexer.getNextToken();
    EXPECT_EQ(real2.type, TokenType::TOKEN_INT);
}

TEST(LexerTest, UnexpectedCharactersAlwaysAdvance) {
    for (const std::string_view src : { "`", "@", "#", "~", "\x80\x81\xff", "let a = `;", "func f() { ` }", "class C { ` }" }) {
        DiagnosticEngine diagnostics;
        Lexer lexer(std::string{src}, diagnostics);

        int count = 0;
        while (lexer.getNextToken().type != TokenType::TOKEN_EOF && count < 1000000)
            ++count;

        EXPECT_LT(count, 1000000) << "lexer did not terminate on input: " << src;
        EXPECT_TRUE(diagnostics.hasErrors());
    }
}
