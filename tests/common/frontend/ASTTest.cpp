#include <gtest/gtest.h>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"

using namespace LOICollection::frontend;

TEST(ASTTest, NodeTypesFromParsing) {
    DiagnosticEngine diagnostics;

    {
        Lexer lex("42", diagnostics);
        Parser parser(lex, diagnostics);

        auto ast = parser.parse();

        ASSERT_EQ(ast->getType(), ASTNode::Type::Program);

        auto& program = static_cast<ProgramNode&>(*ast);
        ASSERT_EQ(program.parts.size(), 1u);
        EXPECT_EQ(program.parts[0]->getType(), ASTNode::Type::Value);
    }
    {
        Lexer lex("1+2", diagnostics);
        Parser parser(lex, diagnostics);

        auto ast = parser.parse();

        auto& program = static_cast<ProgramNode&>(*ast);
        EXPECT_EQ(program.parts[0]->getType(), ASTNode::Type::Arithmetic);
    }
    {
        Lexer lex("if(true)['a':'b']", diagnostics);
        Parser parser(lex, diagnostics);

        auto ast = parser.parse();
        
        auto& program = static_cast<ProgramNode&>(*ast);
        EXPECT_EQ(program.parts[0]->getType(), ASTNode::Type::If);
    }
}

    namespace {
        ASTNode::Type firstPartType(const std::string& source) {
            DiagnosticEngine diagnostics;
            Lexer lexer(source, diagnostics);
            Parser parser(lexer, diagnostics);

            auto ast = parser.parse();
            auto& program = static_cast<ProgramNode&>(*ast);
            return program.parts[0]->getType();
        }
}

TEST(ASTTest, ExpressionNodeTypes) {
    EXPECT_EQ(firstPartType("x"), ASTNode::Type::Variable);
    EXPECT_EQ(firstPartType("a = 1"), ASTNode::Type::Assignment);
    EXPECT_EQ(firstPartType("1 > 2"), ASTNode::Type::Compare);
    EXPECT_EQ(firstPartType("true && false"), ASTNode::Type::Logical);
    EXPECT_EQ(firstPartType("1 + 2"), ASTNode::Type::Arithmetic);
    EXPECT_EQ(firstPartType("-1"), ASTNode::Type::Unary);
    EXPECT_EQ(firstPartType("{greet}"), ASTNode::Type::Macro);
    EXPECT_EQ(firstPartType("math::abs(1)"), ASTNode::Type::Function);
    EXPECT_EQ(firstPartType("foo(1)"), ASTNode::Type::FuncCall);
    EXPECT_EQ(firstPartType("new Foo()"), ASTNode::Type::New);
    EXPECT_EQ(firstPartType("obj.x"), ASTNode::Type::MemberAccess);
    EXPECT_EQ(firstPartType("obj.m()"), ASTNode::Type::MethodCall);
    EXPECT_EQ(firstPartType("this"), ASTNode::Type::This);
    EXPECT_EQ(firstPartType("func (a) { return a; }"), ASTNode::Type::Lambda);
    EXPECT_EQ(firstPartType("[1, 2]"), ASTNode::Type::Array);
    EXPECT_EQ(firstPartType("a[0]"), ASTNode::Type::Index);
}

TEST(ASTTest, StatementNodeTypes) {
    DiagnosticEngine diagnostics;
    Lexer lexer("class Foo { } func bar() { } return 1;", diagnostics);
    Parser parser(lexer, diagnostics);

    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    auto& program = static_cast<ProgramNode&>(*ast);
    ASSERT_EQ(program.parts.size(), 3u);
    EXPECT_EQ(program.parts[0]->getType(), ASTNode::Type::Class);
    EXPECT_EQ(program.parts[1]->getType(), ASTNode::Type::FunctionDef);
    EXPECT_EQ(program.parts[2]->getType(), ASTNode::Type::Return);
}

TEST(ASTTest, ClassBodyParsing) {
    DiagnosticEngine diagnostics;
    Lexer lexer(
        "class Foo { "
        "public: "
        "x = 1; "
        "Foo(a) { this.a = a; } "
        "func get() -> int { return x; } "
        "}",
        diagnostics);
    Parser parser(lexer, diagnostics);

    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    auto& program = static_cast<ProgramNode&>(*ast);
    ASSERT_EQ(program.parts.size(), 1u);

    auto& cls = static_cast<ClassNode&>(*program.parts[0]);
    EXPECT_EQ(cls.name, "Foo");

    ASSERT_EQ(cls.members.size(), 1u);
    EXPECT_EQ(cls.members[0].name, "x");
    EXPECT_TRUE(cls.members[0].hasDefault);

    ASSERT_EQ(cls.methods.size(), 2u);
    EXPECT_TRUE(cls.methods[0].isConstructor);
    EXPECT_EQ(cls.constructorIndex, 0);
    EXPECT_EQ(cls.methods[0].params.size(), 1u);

    EXPECT_EQ(cls.methods[1].name, "get");
    EXPECT_TRUE(cls.methods[1].hasReturnType);
    EXPECT_EQ(cls.methods[1].returnTypeExpr.name, "int");
}

TEST(ASTTest, FunctionDefinitionParsing) {
    DiagnosticEngine diagnostics;
    Lexer lexer("func add(a: int, b) -> string { return a; }", diagnostics);
    Parser parser(lexer, diagnostics);

    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    auto& program = static_cast<ProgramNode&>(*ast);
    ASSERT_EQ(program.parts.size(), 1u);

    auto& fn = static_cast<FunctionDefNode&>(*program.parts[0]);
    EXPECT_EQ(fn.name, "add");
    EXPECT_EQ(fn.decl.params.size(), 2u);
    EXPECT_EQ(fn.decl.params[0].name, "a");
    EXPECT_TRUE(fn.decl.params[0].hasType);
    EXPECT_EQ(fn.decl.params[0].typeExpr.name, "int");
    EXPECT_EQ(fn.decl.params[1].name, "b");
    EXPECT_FALSE(fn.decl.params[1].hasType);
    EXPECT_TRUE(fn.decl.hasReturnType);
    EXPECT_EQ(fn.decl.returnTypeExpr.name, "string");
}
