#include <memory>
#include <string>
#include <charconv>
#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Lexer.h"

#include "LOICollectionA/frontend/Parser.h"

namespace LOICollection::frontend {
    namespace {
        std::string getCompoundAssignOp(TokenType type) {
            switch (type) {
                case TokenType::TOKEN_PLUS_ASSIGN: return "+";
                case TokenType::TOKEN_MINUS_ASSIGN: return "-";
                case TokenType::TOKEN_MULTIPLY_ASSIGN: return "*";
                case TokenType::TOKEN_DIVIDE_ASSIGN: return "/";
                case TokenType::TOKEN_MOD_ASSIGN: return "%";
                default: return {};
            }
        }

        bool isAssignableExpr(const ExprNode* expr) {
            ASTNode::Type type = expr->getType();

            return type == ASTNode::Type::Variable ||
                   type == ASTNode::Type::MemberAccess ||
                   type == ASTNode::Type::Index;
        }
    }

    Parser::Parser(Lexer& l, DiagnosticEngine& diag) : lexer(l), diagnostics(diag) {
        currentToken = lexer.getNextToken();
    }

    std::unique_ptr<ASTNode> Parser::parse() {
        auto program = std::make_unique<ProgramNode>();

        while (currentToken.type != TokenType::TOKEN_EOF) {
            size_t stmtStartLine = currentToken.loc.line;

            auto stmt = parseStatement();
            if (!stmt) {
                synchronize({});
                continue;
            }

            ASTNode::Type stmtType = stmt->getType();

            program->addPart(std::move(stmt));

            if (currentToken.type == TokenType::TOKEN_SEMICOLON) {
                eat(TokenType::TOKEN_SEMICOLON);
            } else if (stmtType == ASTNode::Type::Class || stmtType == ASTNode::Type::FunctionDef) {
            } else if (currentToken.type != TokenType::TOKEN_EOF) {
                if (currentToken.loc.line > stmtStartLine)
                    continue;

                diagnostics.addError(currentToken.loc,
                    "Expected ';' or EOF after statement, got " + getTokenName(currentToken.type) + " (" + currentToken.value + ")");

                synchronize({});
            }
        }

        return program;
    }

    std::unique_ptr<IfNode> Parser::parseIfStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_IF)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_COLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto cond = parseBoolExpression();
        if (!cond || !eat(TokenType::TOKEN_RPAREN) || !eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_COLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }
        
        auto truePart = parseBlock(TokenType::TOKEN_COLON, true);
        if (currentToken.type == TokenType::TOKEN_RBRCKET) {
            if (!eat(TokenType::TOKEN_RBRCKET)) return nullptr;
            return std::make_unique<IfNode>(
                loc,
                std::move(cond),
                std::move(truePart),
                nullptr
            );
        }

        if (!eat(TokenType::TOKEN_COLON)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto falsePart = parseBlock(TokenType::TOKEN_RBRCKET, false);
        if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }
        
        return std::make_unique<IfNode>(
            loc,
            std::move(cond),
            std::move(truePart),
            std::move(falsePart)
        );
    }

    std::unique_ptr<WhileNode> Parser::parseWhileStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_WHILE)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto cond = parseBoolExpression();
        if (!cond || !eat(TokenType::TOKEN_RPAREN) || !eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto body = parseBlock(TokenType::TOKEN_RBRCKET, false);
        if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        return std::make_unique<WhileNode>(loc, std::move(cond), std::move(body));
    }

    std::unique_ptr<ASTNode> Parser::parseForStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_FOR)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_IDENT) {
            Token first = peekToken(0);

            if (first.type == TokenType::TOKEN_IDENT && first.value == "in")
                return parseForInStatement(loc, false);

            Token second = peekToken(1);
            Token third = peekToken(2);

            if (first.type == TokenType::TOKEN_COMMA &&
                second.type == TokenType::TOKEN_IDENT &&
                third.type == TokenType::TOKEN_IDENT && third.value == "in")
                return parseForInStatement(loc, true);
        }

        std::unique_ptr<ExprNode> init;
        if (currentToken.type != TokenType::TOKEN_SEMICOLON) {
            init = parseForClause();
            if (!init) {
                synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RPAREN });
                return nullptr;
            }
        }

        if (!eat(TokenType::TOKEN_SEMICOLON)) {
            synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET });
            return nullptr;
        }

        std::unique_ptr<ExprNode> cond;
        if (currentToken.type != TokenType::TOKEN_SEMICOLON) {
            cond = parseBoolExpression();
            if (!cond) {
                synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RPAREN });
                return nullptr;
            }
        }

        if (!eat(TokenType::TOKEN_SEMICOLON)) {
            synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET });
            return nullptr;
        }

        std::unique_ptr<ExprNode> step;
        if (currentToken.type != TokenType::TOKEN_RPAREN) {
            step = parseForClause();
            if (!step) {
                synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET });
                return nullptr;
            }
        }

        if (!eat(TokenType::TOKEN_RPAREN) || !eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto body = parseBlock(TokenType::TOKEN_RBRCKET, false);
        if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        return std::make_unique<ForNode>(loc, std::move(init), std::move(cond), std::move(step), std::move(body));
    }

    std::unique_ptr<ForInNode> Parser::parseForInStatement(SourceLocation loc, bool hasIndex) {
        auto node = std::make_unique<ForInNode>(loc, currentToken.value);

        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        if (hasIndex) {
            if (!eat(TokenType::TOKEN_COMMA)) {
                synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
                return nullptr;
            }

            node->indexVar = node->elementVar;
            node->elementVar = currentToken.value;
            node->hasIndexVar = true;

            if (!eat(TokenType::TOKEN_IDENT)) {
                synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
                return nullptr;
            }
        }

        if (currentToken.type != TokenType::TOKEN_IDENT || currentToken.value != "in") {
            diagnostics.addError(currentToken.loc, "Expected 'in' in for-in loop");
            synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }
        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        auto iterable = parseBaseExpression();
        if (!iterable) {
            synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_RANGE) {
            SourceLocation rangeLoc = currentToken.loc;

            if (!eat(TokenType::TOKEN_RANGE)) return nullptr;

            auto end = parseBaseExpression();
            if (!end) {
                synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
                return nullptr;
            }

            iterable = std::make_unique<RangeNode>(rangeLoc, std::move(iterable), std::move(end));
        }

        if (!eat(TokenType::TOKEN_RPAREN) || !eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        node->iterable = std::move(iterable);
        node->body = parseBlock(TokenType::TOKEN_RBRCKET, false);

        if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        return node;
    }

    std::unique_ptr<BreakNode> Parser::parseBreakStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_BREAK)) return nullptr;

        return std::make_unique<BreakNode>(loc);
    }

    std::unique_ptr<ContinueNode> Parser::parseContinueStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_CONTINUE)) return nullptr;

        return std::make_unique<ContinueNode>(loc);
    }

    std::unique_ptr<ExprNode> Parser::parseForClause() {
        SourceLocation loc = currentToken.loc;

        auto expr = parseBaseExpression();
        if (!expr)
            return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            return std::make_unique<AssignmentNode>(loc, std::move(expr), std::move(right));
        }

        std::string op = getCompoundAssignOp(currentToken.type);
        if (!op.empty()) {
            if (!eat(currentToken.type)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            return std::make_unique<CompoundAssignNode>(loc, std::move(expr), std::move(right), op);
        }

        return expr;
    }

    std::unique_ptr<UsingNode> Parser::parseUsing() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_USING)) return nullptr;
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected alias name after 'using'");
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_EOF });
            return nullptr;
        }

        auto usingNode = std::make_unique<UsingNode>(loc, currentToken.value);
        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        if (currentToken.type != TokenType::TOKEN_OP || currentToken.value != "=") {
            diagnostics.addError(currentToken.loc,
                "Expected '=' in using declaration, got " + getTokenName(currentToken.type));
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_EOF });
            return nullptr;
        }
        if (!eat(TokenType::TOKEN_OP)) return nullptr;

        auto type = parseTypeExpr();
        if (!type) {
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_EOF });
            return nullptr;
        }

        usingNode->type = std::move(*type);
        return usingNode;
    }

    std::unique_ptr<TypeExpr> Parser::parseTypeExpr() {
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected type name");
            return nullptr;
        }

        auto type = std::make_unique<TypeExpr>();
        type->loc = currentToken.loc;
        type->name = currentToken.value;

        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "<") {
            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            while (currentToken.type != TokenType::TOKEN_EOF &&
                   currentToken.type != TokenType::TOKEN_RBRACE &&
                   currentToken.type != TokenType::TOKEN_SEMICOLON) {
                auto arg = parseTypeExpr();
                if (!arg) return nullptr;

                type->args.push_back(std::move(*arg));

                if (currentToken.type == TokenType::TOKEN_COMMA) {
                    if (!eat(TokenType::TOKEN_COMMA)) return nullptr;
                    continue;
                }

                break;
            }

            if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == ">") {
                if (!eat(TokenType::TOKEN_OP)) return nullptr;
            } else if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == ">=") {
                SourceLocation opLoc = currentToken.loc;
                if (!eat(TokenType::TOKEN_OP)) return nullptr;

                currentToken = { TokenType::TOKEN_OP, "=", opLoc };
            } else {
                diagnostics.addError(currentToken.loc,
                    "Expected '>' to close type '" + type->name + "'");
                return nullptr;
            }
        }

        return type;
    }

    std::unique_ptr<FunctionNode> Parser::parseFunction() {
        SourceLocation loc = currentToken.loc;

        std::string namespaces = currentToken.value;

        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
        if (!eat(TokenType::TOKEN_NAMESPACE)) return nullptr;

        std::string name = currentToken.value;

        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

        auto args = parseArgs();

        if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;

        return std::make_unique<FunctionNode>(
            loc,
            std::move(args),
            std::move(namespaces),
            std::move(name)
        );
    }

    std::unique_ptr<MacroNode> Parser::parseMacro() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_LBRACE)) return nullptr;

        std::string name = currentToken.value;

        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        std::vector<std::unique_ptr<ExprNode>> args;
        if (currentToken.type == TokenType::TOKEN_LPAREN) {
            if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

            args = parseArgs();

            if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;
        }

        if (!eat(TokenType::TOKEN_RBRACE)) return nullptr;

        return std::make_unique<MacroNode>(
            loc,
            std::move(args),
            std::move(name)
        );
    }

    std::unique_ptr<ClassNode> Parser::parseClass() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_CLASS)) return nullptr;
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected class name");
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        std::string name = currentToken.value;
        if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

        auto cls = std::make_unique<ClassNode>(loc, name);

        if (currentToken.type == TokenType::TOKEN_EXTENDS) {
            if (!eat(TokenType::TOKEN_EXTENDS)) return nullptr;
            if (currentToken.type != TokenType::TOKEN_IDENT) {
                diagnostics.addError(currentToken.loc, "Expected base class name after 'extends'");
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            cls->baseClassName = currentToken.value;
            if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        bool privateSection = false;

        while (currentToken.type != TokenType::TOKEN_RBRACE && currentToken.type != TokenType::TOKEN_EOF) {
            bool isStatic = false;
            if (currentToken.type == TokenType::TOKEN_STATIC) {
                if (!eat(TokenType::TOKEN_STATIC)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                isStatic = true;
            }

            if (currentToken.type == TokenType::TOKEN_PUBLIC) {
                if (isStatic) {
                    diagnostics.addError(currentToken.loc, "'static' cannot be used with an access section");
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                if (!eat(TokenType::TOKEN_PUBLIC) || !eat(TokenType::TOKEN_COLON)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                privateSection = false;
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_PRIVATE) {
                if (isStatic) {
                    diagnostics.addError(currentToken.loc, "'static' cannot be used with an access section");
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                if (!eat(TokenType::TOKEN_PRIVATE) || !eat(TokenType::TOKEN_COLON)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                privateSection = true;
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_FUNC) {
                auto method = parseMethod(privateSection);
                if (!method) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                method->isStatic = isStatic;
                cls->methods.push_back(std::move(*method));
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_IDENT) {
                Token next = lexer.peekNextToken();

                if (currentToken.value == cls->name && next.type == TokenType::TOKEN_LPAREN) {
                    auto method = parseConstructor(cls->name, privateSection);
                    if (!method) {
                        synchronize({ TokenType::TOKEN_RBRACE });
                        continue;
                    }

                    if (cls->constructorIndex != -1) {
                        diagnostics.addError(currentToken.loc, "Duplicate constructor in class '" + cls->name + "'");
                        continue;
                    }

                    if (isStatic) {
                        diagnostics.addError(method->loc, "Constructor cannot be static");
                        continue;
                    }

                    cls->constructorIndex = static_cast<int>(cls->methods.size());
                    cls->methods.push_back(std::move(*method));
                    continue;
                }

                ClassMember member;
                member.loc = currentToken.loc;
                member.name = currentToken.value;
                member.isPrivate = privateSection;
                member.isStatic = isStatic;

                if (!eat(TokenType::TOKEN_IDENT)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                if (currentToken.type == TokenType::TOKEN_COLON) {
                    if (!eat(TokenType::TOKEN_COLON)) {
                        synchronize({ TokenType::TOKEN_RBRACE });
                        continue;
                    }

                    auto type = parseTypeExpr();
                    if (!type) {
                        synchronize({ TokenType::TOKEN_RBRACE });
                        continue;
                    }

                    member.typeExpr = std::move(*type);
                    member.hasTypeExpr = true;
                }

                if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
                    if (!eat(TokenType::TOKEN_OP)) {
                        synchronize({ TokenType::TOKEN_RBRACE });
                        continue;
                    }

                    member.defaultExpr = parseBaseExpression();
                    if (!member.defaultExpr) {
                        synchronize({ TokenType::TOKEN_RBRACE });
                        continue;
                    }

                    member.hasDefault = true;
                }

                if (!eat(TokenType::TOKEN_SEMICOLON)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                cls->members.push_back(std::move(member));
                continue;
            }

            diagnostics.addError(currentToken.loc,
                "Unexpected token in class body: " + getTokenName(currentToken.type) + " (" + currentToken.value + ")");
            synchronize({ TokenType::TOKEN_RBRACE });
        }

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return cls;
    }

    std::unique_ptr<FunctionDefNode> Parser::parseFunctionDefinition() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_FUNC)) return nullptr;
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected function name");
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto fn = std::make_unique<FunctionDefNode>(loc, currentToken.value);
        fn->decl.loc = loc;
        fn->decl.name = currentToken.value;

        if (!eat(TokenType::TOKEN_IDENT) || !eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        fn->decl.params = parseParams();

        if (!eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_ARROW) {
            if (!eat(TokenType::TOKEN_ARROW)) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            auto returnType = parseTypeExpr();
            if (!returnType) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            fn->decl.returnTypeExpr = std::move(*returnType);
            fn->decl.hasReturnType = true;
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        fn->decl.body = parseBlock(TokenType::TOKEN_RBRACE, false);

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return fn;
    }

    std::unique_ptr<LambdaNode> Parser::parseLambda() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_FUNC)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto lambda = std::make_unique<LambdaNode>(loc);
        lambda->decl.loc = loc;
        lambda->decl.name = "lambda";
        lambda->decl.params = parseParams();

        if (!eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_ARROW) {
            if (!eat(TokenType::TOKEN_ARROW)) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            auto returnType = parseTypeExpr();
            if (!returnType) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            lambda->decl.returnTypeExpr = std::move(*returnType);
            lambda->decl.hasReturnType = true;
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        lambda->decl.body = parseBlock(TokenType::TOKEN_RBRACE, false);

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return lambda;
    }

    std::unique_ptr<MethodDecl> Parser::parseMethod(bool isPrivate) {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_FUNC)) return nullptr;
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected method name");
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto method = std::make_unique<MethodDecl>();
        method->loc = loc;
        method->name = currentToken.value;
        method->isPrivate = isPrivate;

        if (!eat(TokenType::TOKEN_IDENT) || !eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        method->params = parseParams();

        if (!eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_ARROW) {
            if (!eat(TokenType::TOKEN_ARROW)) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            auto returnType = parseTypeExpr();
            if (!returnType) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }

            method->returnTypeExpr = std::move(*returnType);
            method->hasReturnType = true;
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        method->body = parseBlock(TokenType::TOKEN_RBRACE, false);

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return method;
    }

    std::unique_ptr<MethodDecl> Parser::parseConstructor(const std::string& className, bool isPrivate) {
        SourceLocation loc = currentToken.loc;

        if (currentToken.type != TokenType::TOKEN_IDENT || currentToken.value != className) {
            diagnostics.addError(currentToken.loc, "Expected constructor name '" + className + "'");
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto method = std::make_unique<MethodDecl>();
        method->loc = loc;
        method->name = className;
        method->isConstructor = true;
        method->isPrivate = isPrivate;

        if (!eat(TokenType::TOKEN_IDENT) || !eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        method->params = parseParams();

        if (!eat(TokenType::TOKEN_RPAREN) || !eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        method->body = parseBlock(TokenType::TOKEN_RBRACE, false);

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return method;
    }

    std::vector<MethodParam> Parser::parseParams() {
        std::vector<MethodParam> params;

        while (currentToken.type != TokenType::TOKEN_RPAREN &&
               currentToken.type != TokenType::TOKEN_EOF &&
               currentToken.type != TokenType::TOKEN_RBRACE &&
               currentToken.type != TokenType::TOKEN_SEMICOLON) {
            if (currentToken.type != TokenType::TOKEN_IDENT) {
                diagnostics.addError(currentToken.loc, "Expected parameter name");
                synchronize({ TokenType::TOKEN_COMMA, TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRACE });
                if (currentToken.type == TokenType::TOKEN_COMMA)
                    eat(TokenType::TOKEN_COMMA);

                continue;
            }

            MethodParam param;
            param.name = currentToken.value;

            if (!eat(TokenType::TOKEN_IDENT)) continue;

            if (currentToken.type == TokenType::TOKEN_COLON) {
                if (!eat(TokenType::TOKEN_COLON)) {
                    synchronize({ TokenType::TOKEN_COMMA, TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRACE });
                    if (currentToken.type == TokenType::TOKEN_COMMA)
                        eat(TokenType::TOKEN_COMMA);

                    continue;
                }

                auto type = parseTypeExpr();
                if (!type) {
                    synchronize({ TokenType::TOKEN_COMMA, TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRACE });
                    if (currentToken.type == TokenType::TOKEN_COMMA)
                        eat(TokenType::TOKEN_COMMA);

                    continue;
                }

                param.typeExpr = std::move(*type);
                param.hasType = true;
            }

            params.push_back(std::move(param));

            if (currentToken.type == TokenType::TOKEN_COMMA) {
                eat(TokenType::TOKEN_COMMA);
            } else if (currentToken.type != TokenType::TOKEN_RPAREN) {
                diagnostics.addError(currentToken.loc,
                    "Expected ',' or ')' in parameter list, got " + getTokenName(currentToken.type));
                synchronize({ TokenType::TOKEN_COMMA, TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRACE });
                if (currentToken.type == TokenType::TOKEN_COMMA)
                    eat(TokenType::TOKEN_COMMA);
            }
        }

        return params;
    }

    std::unique_ptr<ReturnNode> Parser::parseReturn() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_RETURN)) return nullptr;

        std::unique_ptr<ExprNode> value;
        if (currentToken.type != TokenType::TOKEN_SEMICOLON &&
            currentToken.type != TokenType::TOKEN_RBRACE &&
            currentToken.type != TokenType::TOKEN_RBRCKET &&
            currentToken.type != TokenType::TOKEN_COLON &&
            currentToken.type != TokenType::TOKEN_EOF) {
            value = parseBaseExpression();
            if (!value)
                return nullptr;
        }

        return std::make_unique<ReturnNode>(loc, std::move(value));
    }

    std::unique_ptr<BlockNode> Parser::parseBlock(TokenType stopToken, bool stopOnColon) {
        auto block = std::make_unique<BlockNode>();

        int bracketDepth = 0;
        while (currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type == TokenType::TOKEN_LBRCKET) {
                bracketDepth++;
            } else if (currentToken.type == TokenType::TOKEN_RBRCKET && bracketDepth > 0) {
                bracketDepth--;
            }

            if (bracketDepth == 0) {
                if (currentToken.type == TokenType::TOKEN_RBRACE)
                    break;

                if (currentToken.type == TokenType::TOKEN_RBRCKET &&
                    stopToken != TokenType::TOKEN_RBRCKET)
                    break;

                if (stopOnColon && currentToken.type == TokenType::TOKEN_COLON)
                    break;

                if (!stopOnColon && currentToken.type == stopToken)
                    break;
            }

            if (currentToken.type == TokenType::TOKEN_CLASS ||
                (currentToken.type == TokenType::TOKEN_FUNC && peek() != TokenType::TOKEN_LPAREN)) {
                diagnostics.addError(currentToken.loc,
                    "Class and function definitions are only allowed at top level");

                synchronize(stopOnColon
                    ? std::initializer_list<TokenType>{ TokenType::TOKEN_COLON, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE }
                    : std::initializer_list<TokenType>{ stopToken, TokenType::TOKEN_RBRACE });
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_USING) {
                diagnostics.addError(currentToken.loc,
                    "Using declarations are only allowed at top level");

                synchronize(stopOnColon
                    ? std::initializer_list<TokenType>{ TokenType::TOKEN_COLON, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE }
                    : std::initializer_list<TokenType>{ stopToken, TokenType::TOKEN_RBRACE });
                continue;
            }

            size_t stmtStartLine = currentToken.loc.line;
            auto stmt = parseStatement();
            if (!stmt) {
                synchronize(stopOnColon
                    ? std::initializer_list<TokenType>{ TokenType::TOKEN_COLON, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE }
                    : std::initializer_list<TokenType>{ stopToken, TokenType::TOKEN_RBRACE });
                continue;
            }

            block->addPart(std::move(stmt));

            if (currentToken.type == TokenType::TOKEN_SEMICOLON) {
                eat(TokenType::TOKEN_SEMICOLON);
            } else if (currentToken.type != TokenType::TOKEN_COLON &&
                currentToken.type != TokenType::TOKEN_RBRCKET &&
                currentToken.type != TokenType::TOKEN_RBRACE) {
                if (currentToken.loc.line > stmtStartLine)
                    continue;

                if (!eat(TokenType::TOKEN_SEMICOLON)) {
                    synchronize(stopOnColon
                        ? std::initializer_list<TokenType>{ TokenType::TOKEN_COLON, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE }
                        : std::initializer_list<TokenType>{ stopToken, TokenType::TOKEN_RBRACE });
                }
            }
        }

        return block;
    }

    std::unique_ptr<ValueNode> Parser::parseTranspile() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_TRANSPILE)) return nullptr;

        std::string buffer;
        while (currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type == TokenType::TOKEN_RBRACE || currentToken.type == TokenType::TOKEN_SEMICOLON) {
                buffer += currentToken.value;

                eat(currentToken.type);

                break;
            }

            buffer += currentToken.value;
            eat(currentToken.type);
        }

        return std::make_unique<ValueNode>(loc, std::move(buffer));
    }

    std::vector<std::unique_ptr<ExprNode>> Parser::parseArgs(TokenType delimiterToken, TokenType stopToken) {
        std::vector<std::unique_ptr<ExprNode>> args;

        while (currentToken.type != stopToken &&
               currentToken.type != TokenType::TOKEN_EOF &&
               currentToken.type != TokenType::TOKEN_RBRACE &&
               currentToken.type != TokenType::TOKEN_SEMICOLON) {
            if (args.size() >= 100) {
                diagnostics.addError(currentToken.loc, "Too many args in function call");
                synchronize({ stopToken });
                break;
            }

            if (currentToken.type == delimiterToken) {
                eat(delimiterToken);
                continue;
            }

            size_t exprOffset = currentToken.loc.offset;
            auto expr = parseBaseExpression();
            if (!expr) {
                synchronize({ delimiterToken, stopToken, TokenType::TOKEN_RBRACE });
                if (currentToken.type == delimiterToken)
                    eat(delimiterToken);
                
                continue;
            }

            args.push_back(std::move(expr));

            if (currentToken.type == delimiterToken) {
                eat(delimiterToken);
            } else if (currentToken.loc.offset == exprOffset && currentToken.type != stopToken) {
                synchronize({ delimiterToken, stopToken, TokenType::TOKEN_RBRACE });
                if (currentToken.type == delimiterToken)
                    eat(delimiterToken);
            }
        }

        return args;
    }

    std::unique_ptr<ASTNode> Parser::parseStatement() {
        if (currentToken.type == TokenType::TOKEN_FUNC && peek() != TokenType::TOKEN_LPAREN)
            return parseFunctionDefinition();

        if (currentToken.type == TokenType::TOKEN_CLASS)
            return parseClass();

        if (currentToken.type == TokenType::TOKEN_WHILE)
            return parseWhileStatement();

        if (currentToken.type == TokenType::TOKEN_FOR)
            return parseForStatement();

        if (currentToken.type == TokenType::TOKEN_BREAK)
            return parseBreakStatement();

        if (currentToken.type == TokenType::TOKEN_CONTINUE)
            return parseContinueStatement();

        if (currentToken.type == TokenType::TOKEN_RETURN)
            return parseReturn();

        if (currentToken.type == TokenType::TOKEN_USING)
            return parseUsing();

        if (currentToken.type == TokenType::TOKEN_IDENT && peek() == TokenType::TOKEN_COLON) {
            SourceLocation declLoc = currentToken.loc;
            std::string name = currentToken.value;

            if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
            if (!eat(TokenType::TOKEN_COLON)) return nullptr;

            auto type = parseTypeExpr();
            if (!type)
                return nullptr;

            std::unique_ptr<ExprNode> value;
            if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
                if (!eat(TokenType::TOKEN_OP)) return nullptr;

                value = parseBaseExpression();
                if (!value)
                    return nullptr;
            }

            auto decl = std::make_unique<AssignmentNode>(
                declLoc,
                std::make_unique<VariableNode>(declLoc, std::move(name)),
                std::move(value)
            );
            decl->hasDeclaredType = true;
            decl->declaredType = std::move(*type);
            return decl;
        }

        SourceLocation exprLoc = currentToken.loc;
        
        auto expr = parseBaseExpression();
        if (!expr)
            return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            return std::make_unique<AssignmentNode>(exprLoc, std::move(expr), std::move(right));
        }

        std::string op = getCompoundAssignOp(currentToken.type);
        if (!op.empty()) {
            if (!eat(currentToken.type)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            return std::make_unique<CompoundAssignNode>(exprLoc, std::move(expr), std::move(right), op);
        }

        return expr;
    }

    std::unique_ptr<ExprNode> Parser::parseBaseExpression() {
        return parseCoalesceExpression();
    }

    std::unique_ptr<ExprNode> Parser::parseBoolExpression() {
        return parseCoalesceExpression();
    }

    std::unique_ptr<ExprNode> Parser::parseCoalesceExpression() {
        auto left = parseOrExpression();
        if (!left)
            return nullptr;

        while (currentToken.type == TokenType::TOKEN_COALESCE) {
            SourceLocation loc = currentToken.loc;

            if (!eat(TokenType::TOKEN_COALESCE)) return nullptr;

            auto right = parseOrExpression();
            if (!right)
                return nullptr;

            auto isBareLogical = [](const ExprNode& expr) -> bool {
                return !expr.parenthesized && expr.getType() == ASTNode::Type::Logical;
            };

            if (isBareLogical(*left) || isBareLogical(*right))
                diagnostics.addError(loc, "'??' cannot be mixed with '&&' or '||' without parentheses");

            left = std::make_unique<CoalesceNode>(loc, std::move(left), std::move(right));
        }

        return left;
    }

    std::unique_ptr<ExprNode> Parser::parseOrExpression() {
        auto left = parseAndExpression();
        if (!left)
            return nullptr;

        while (currentToken.type == TokenType::TOKEN_BOOL_OP && currentToken.value == "||") {
            SourceLocation loc = currentToken.loc;

            if (!eat(TokenType::TOKEN_BOOL_OP)) return nullptr;

            auto right = parseAndExpression();
            if (!right)
                return nullptr;

            left = std::make_unique<LogicalNode>(loc, std::move(left), std::move(right), "||");
        }
        
        return left;
    }
    
    std::unique_ptr<ExprNode> Parser::parseAndExpression() {
        auto left = parseComparison();
        if (!left)
            return nullptr;
        
        while (currentToken.type == TokenType::TOKEN_BOOL_OP && currentToken.value == "&&") {
            SourceLocation loc = currentToken.loc;

            if (!eat(TokenType::TOKEN_BOOL_OP)) return nullptr;

            auto right = parseComparison();
            if (!right)
                return nullptr;

            left = std::make_unique<LogicalNode>(loc, std::move(left), std::move(right), "&&");
        }

        return left;
    }

    std::unique_ptr<ExprNode> Parser::parseComparison() {
        auto left = parseAdditiveExpression();
        if (!left)
            return nullptr;

        if (currentToken.type == TokenType::TOKEN_INSTANCEOF) {
            SourceLocation loc = currentToken.loc;

            if (!eat(TokenType::TOKEN_INSTANCEOF)) return nullptr;
            if (currentToken.type != TokenType::TOKEN_IDENT) {
                diagnostics.addError(currentToken.loc, "Expected class name after 'instanceof'");
                return nullptr;
            }

            std::string className = currentToken.value;
            if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

            return std::make_unique<InstanceOfNode>(loc, std::move(left), std::move(className));
        }
        
        static const std::unordered_set<std::string> comparisonOps = {
            "==", "!=", ">", "<", ">=", "<="
        };
        
        if (currentToken.type == TokenType::TOKEN_OP && comparisonOps.find(currentToken.value) != comparisonOps.end()) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.value;

            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            auto right = parseAdditiveExpression();
            if (!right)
                return nullptr;

            return std::make_unique<CompareNode>(loc, std::move(left), std::move(right), op);
        }

        return left;
    }

    std::unique_ptr<ExprNode> Parser::parseAdditiveExpression() {
        auto left = parseMultiplicativeExpression();
        if (!left)
            return nullptr;
        
        while (currentToken.type == TokenType::TOKEN_PLUS || currentToken.type == TokenType::TOKEN_MINUS) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.value;
            
            if (currentToken.type == TokenType::TOKEN_PLUS) {
                if (!eat(TokenType::TOKEN_PLUS))  return nullptr;
            } else {
                if (!eat(TokenType::TOKEN_MINUS)) return nullptr;
            }
            
            auto right = parseMultiplicativeExpression();
            if (!right)
                return nullptr;

            left = std::make_unique<ArithmeticNode>(loc, std::move(left), std::move(right), op);
        }
        
        return left;
    }

    std::unique_ptr<ExprNode> Parser::parseMultiplicativeExpression() {
        auto left = parsePowerExpression();
        if (!left)
            return nullptr;
        
        while (currentToken.type == TokenType::TOKEN_MULTIPLY || currentToken.type == TokenType::TOKEN_DIVIDE || currentToken.type == TokenType::TOKEN_MOD) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.value;
            
            if (currentToken.type == TokenType::TOKEN_MULTIPLY) {
                if (!eat(TokenType::TOKEN_MULTIPLY)) return nullptr;
            } else if (currentToken.type == TokenType::TOKEN_DIVIDE) {
                if (!eat(TokenType::TOKEN_DIVIDE)) return nullptr;
            } else {
                if (!eat(TokenType::TOKEN_MOD)) return nullptr;
            }
            
            auto right = parsePowerExpression();
            if (!right)
                return nullptr;

            left = std::make_unique<ArithmeticNode>(loc, std::move(left), std::move(right), op);
        }
        
        return left;
    }

    std::unique_ptr<ExprNode> Parser::parsePowerExpression() {
        auto left = parseUnaryExpression();
        if (!left)
            return nullptr;
        
        if (currentToken.type == TokenType::TOKEN_POWER) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.value;

            if (!eat(TokenType::TOKEN_POWER)) return nullptr;

            auto right = parsePowerExpression();
            if (!right)
                return nullptr;

            return std::make_unique<ArithmeticNode>(loc, std::move(left), std::move(right), op);
        }
        
        return left;
    }

    std::unique_ptr<ExprNode> Parser::parseUnaryExpression() {
        if (currentToken.type == TokenType::TOKEN_INCREMENT || currentToken.type == TokenType::TOKEN_DECREMENT) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.type == TokenType::TOKEN_INCREMENT ? "+" : "-";

            advance();

            auto operand = parseUnaryExpression();
            if (!operand)
                return nullptr;

            if (!isAssignableExpr(operand.get())) {
                diagnostics.addError(loc, "Operand of prefix '++'/'--' must be a variable, field or index");
                return nullptr;
            }

            return std::make_unique<CompoundAssignNode>(
                loc, std::move(operand), std::make_unique<ValueNode>(loc, 1), op
            );
        }

        if ((currentToken.type == TokenType::TOKEN_OP && currentToken.value == "!") ||
            currentToken.type == TokenType::TOKEN_PLUS || currentToken.type == TokenType::TOKEN_MINUS) {
            SourceLocation loc = currentToken.loc;
            std::string op = currentToken.value;
            
            if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "!") {
                if (!eat(TokenType::TOKEN_OP)) return nullptr;
            } else if (currentToken.type == TokenType::TOKEN_PLUS) {
                if (!eat(TokenType::TOKEN_PLUS))  return nullptr;
            } else {
                if (!eat(TokenType::TOKEN_MINUS)) return nullptr;
            }
            
            auto operand = parseUnaryExpression();
            if (!operand)
                return nullptr;

            return std::make_unique<UnaryNode>(loc, std::move(operand), op);
        }
        
        return parsePostfix();
    }

    std::unique_ptr<ExprNode> Parser::parsePostfix() {
        auto expr = parsePrimary();
        if (!expr)
            return nullptr;

        while (true) {
            if (currentToken.type == TokenType::TOKEN_DOT) {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_DOT)) return nullptr;
                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected member name after '.'");
                    return nullptr;
                }

                std::string member = currentToken.value;
                if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

                if (currentToken.type == TokenType::TOKEN_LPAREN) {
                    if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

                    auto args = parseArgs();

                    if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;

                    expr = std::make_unique<MethodCallNode>(
                        loc, std::move(expr), std::move(member), std::move(args)
                    );
                } else {
                    expr = std::make_unique<MemberAccessNode>(
                        loc, std::move(expr), std::move(member)
                    );
                }

                continue;
            }

            if (currentToken.type == TokenType::TOKEN_QUESTION_DOT) {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_QUESTION_DOT)) return nullptr;

                if (currentToken.type == TokenType::TOKEN_LBRCKET) {
                    if (!eat(TokenType::TOKEN_LBRCKET)) return nullptr;

                    auto index = parseBaseExpression();
                    if (!index)
                        return nullptr;

                    if (!eat(TokenType::TOKEN_RBRCKET)) return nullptr;

                    auto access = std::make_unique<IndexAccessNode>(
                        loc, std::move(expr), std::move(index)
                    );
                    access->isSafe = true;
                    expr = std::move(access);

                    continue;
                }

                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected member name after '?.'");
                    return nullptr;
                }

                std::string member = currentToken.value;
                if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

                auto access = std::make_unique<MemberAccessNode>(
                    loc, std::move(expr), std::move(member)
                );
                access->isSafe = true;
                expr = std::move(access);

                continue;
            }

            if (currentToken.type == TokenType::TOKEN_LBRCKET) {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_LBRCKET)) return nullptr;

                auto index = parseBaseExpression();
                if (!index)
                    return nullptr;

                if (!eat(TokenType::TOKEN_RBRCKET)) return nullptr;

                expr = std::make_unique<IndexAccessNode>(
                    loc, std::move(expr), std::move(index)
                );
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_INCREMENT || currentToken.type == TokenType::TOKEN_DECREMENT) {
                SourceLocation loc = currentToken.loc;
                std::string op = currentToken.type == TokenType::TOKEN_INCREMENT ? "+" : "-";

                advance();

                if (!isAssignableExpr(expr.get())) {
                    diagnostics.addError(loc, "Operand of '++'/'--' must be a variable, field or index");
                    return nullptr;
                }

                expr = std::make_unique<CompoundAssignNode>(
                    loc, std::move(expr), std::make_unique<ValueNode>(loc, 1), op
                );

                continue;
            }

            break;
        }

        return expr;
    }

    std::unique_ptr<ExprNode> Parser::parsePrimary() {
        switch (currentToken.type) {
            case TokenType::TOKEN_IF:
                return parseIfStatement();
            case TokenType::TOKEN_NEW: {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_NEW)) return nullptr;
                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected class name after 'new'");
                    return nullptr;
                }

                std::string className = currentToken.value;
                if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
                if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

                auto args = parseArgs();

                if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;

                return std::make_unique<NewNode>(loc, std::move(className), std::move(args));
            }
            case TokenType::TOKEN_LBRCKET: {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_LBRCKET)) return nullptr;

                auto elements = parseArgs(TokenType::TOKEN_COMMA, TokenType::TOKEN_RBRCKET);

                if (!eat(TokenType::TOKEN_RBRCKET)) return nullptr;

                return std::make_unique<ArrayNode>(loc, std::move(elements));
            }
            case TokenType::TOKEN_THIS: {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_THIS)) return nullptr;
                return std::make_unique<ThisNode>(loc);
            }
            case TokenType::TOKEN_SUPER: {
                SourceLocation loc = currentToken.loc;

                if (!eat(TokenType::TOKEN_SUPER)) return nullptr;

                if (currentToken.type == TokenType::TOKEN_LPAREN) {
                    if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

                    auto args = parseArgs();

                    if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;

                    return std::make_unique<SuperCallNode>(loc, std::move(args));
                }

                return std::make_unique<SuperNode>(loc);
            }
            case TokenType::TOKEN_FUNC:
                return parseLambda();
            case TokenType::TOKEN_LPAREN: {
                if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

                auto expr = parseBaseExpression();
                if (!expr)
                    return nullptr;

                if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;
                expr->parenthesized = true;
                return expr;
            }
            case TokenType::TOKEN_IDENT: {
                SourceLocation loc = currentToken.loc;

                if (peek() == TokenType::TOKEN_NAMESPACE)
                    return parseFunction();

                if (peek() == TokenType::TOKEN_LPAREN) {
                    std::string name = currentToken.value;

                    if (!eat(TokenType::TOKEN_IDENT)) return nullptr;
                    if (!eat(TokenType::TOKEN_LPAREN)) return nullptr;

                    auto args = parseArgs();

                    if (!eat(TokenType::TOKEN_RPAREN)) return nullptr;

                    return std::make_unique<FuncCallNode>(loc, std::move(name), std::move(args));
                }

                std::string name = currentToken.value;

                if (!eat(TokenType::TOKEN_IDENT)) return nullptr;

                return std::make_unique<VariableNode>(loc, std::move(name));
            }
            case TokenType::TOKEN_TRANSPILE:
                return parseTranspile();
            case TokenType::TOKEN_LBRACE:
                return parseMacro();
            default:
                break;
        }

        return parseValue();
    }

    std::unique_ptr<ValueNode> Parser::parseValue() {
        SourceLocation loc = currentToken.loc;

        switch (currentToken.type) {
            case TokenType::TOKEN_INT: {
                int value;
                auto [ptr, ec] = std::from_chars(currentToken.value.data(), currentToken.value.data() + currentToken.value.size(), value);

                if (ec != std::errc() || ptr != currentToken.value.data() + currentToken.value.size()) {
                    diagnostics.addError(currentToken.loc, "Invalid integer literal: " + currentToken.value);

                    eat(TokenType::TOKEN_INT);
                    return std::make_unique<ValueNode>(loc, 0);
                }

                eat(TokenType::TOKEN_INT);
                return std::make_unique<ValueNode>(loc, value);
            }
            case TokenType::TOKEN_FLOAT: {
                float value;
                auto [ptr, ec] = std::from_chars(currentToken.value.data(), currentToken.value.data() + currentToken.value.size(), value);

                if (ec != std::errc() || ptr != currentToken.value.data() + currentToken.value.size()) {
                    diagnostics.addError(currentToken.loc, "Invalid float literal: " + currentToken.value);
                    
                    eat(TokenType::TOKEN_FLOAT);
                    return std::make_unique<ValueNode>(loc, 0.0f);
                }

                eat(TokenType::TOKEN_FLOAT);
                return std::make_unique<ValueNode>(loc, value);
            }
            case TokenType::TOKEN_STRING: {
                std::string str = std::move(currentToken.value);

                eat(TokenType::TOKEN_STRING);
                return std::make_unique<ValueNode>(loc, std::move(str));
            }
            case TokenType::TOKEN_BOOL_LIT: {
                bool val = (currentToken.value == "true");
                
                eat(TokenType::TOKEN_BOOL_LIT);
                return std::make_unique<ValueNode>(loc, val);
            }
            case TokenType::TOKEN_NONE:
                eat(TokenType::TOKEN_NONE);
                return std::make_unique<ValueNode>(loc, std::monostate{});
            default:
                diagnostics.addError(currentToken.loc, "Unexpected value type: " + currentToken.value);
                return std::make_unique<ValueNode>(loc, 0);
        }
    }

    TokenType Parser::peek() {
        return peekToken(0).type;
    }

    Token Parser::peekToken(size_t offset) {
        while (lookaheadBuffer.size() <= offset)
            lookaheadBuffer.push_back(lexer.getNextToken());

        return lookaheadBuffer[offset];
    }

    void Parser::advance() {
        if (!lookaheadBuffer.empty()) {
            currentToken = std::move(lookaheadBuffer.front());
            lookaheadBuffer.erase(lookaheadBuffer.begin());
            return;
        }

        currentToken = lexer.getNextToken();
    }

    bool Parser::eat(TokenType expected) {
        if (currentToken.type != expected) {
            diagnostics.addError(currentToken.loc,
                "Syntax error: Expected " + getTokenName(expected) + ", got " + getTokenName(currentToken.type));
            return false;
        }

        advance();
        return true;
    }

    void Parser::synchronize(std::initializer_list<TokenType> stopTokens) {
        while (currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type == TokenType::TOKEN_SEMICOLON) {
                advance();
                return;
            }

            if (std::ranges::find(stopTokens, currentToken.type) != stopTokens.end())
                return;

            advance();
        }
    }

    void Parser::skipBalancedBraces() {
        if (currentToken.type != TokenType::TOKEN_LBRACE)
            return;

        int depth = 0;
        while (currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type == TokenType::TOKEN_LBRACE) {
                depth++;
            } else if (currentToken.type == TokenType::TOKEN_RBRACE) {
                depth--;
                advance();
                if (depth == 0)
                    return;
                continue;
            }

            advance();
        }
    }
    
    std::string Parser::getTokenName(TokenType type) {
        switch (type) {
            case TokenType::TOKEN_IF: return "IF";
            case TokenType::TOKEN_WHILE: return "WHILE";
            case TokenType::TOKEN_FOR: return "FOR";
            case TokenType::TOKEN_BREAK: return "BREAK";
            case TokenType::TOKEN_CONTINUE: return "CONTINUE";
            case TokenType::TOKEN_LPAREN: return "(";
            case TokenType::TOKEN_RPAREN: return ")";
            case TokenType::TOKEN_LBRCKET: return "[";
            case TokenType::TOKEN_RBRCKET: return "]";
            case TokenType::TOKEN_LBRACE: return "{";
            case TokenType::TOKEN_RBRACE: return "}";
            case TokenType::TOKEN_IDENT: return "IDENT";
            case TokenType::TOKEN_INT: return "NUMBER";
            case TokenType::TOKEN_FLOAT: return "FLOAT";
            case TokenType::TOKEN_STRING: return "STRING";
            case TokenType::TOKEN_OP: return "OP";
            case TokenType::TOKEN_BOOL_OP: return "BOOL_OP";
            case TokenType::TOKEN_COLON: return ":";
            case TokenType::TOKEN_BOOL_LIT: return "BOOL_LIT";
            case TokenType::TOKEN_PLUS: return "+";
            case TokenType::TOKEN_MINUS: return "-";
            case TokenType::TOKEN_MULTIPLY: return "*";
            case TokenType::TOKEN_DIVIDE: return "/";
            case TokenType::TOKEN_MOD: return "%";
            case TokenType::TOKEN_POWER: return "^";
            case TokenType::TOKEN_NAMESPACE: return "NAMESPACE";
            case TokenType::TOKEN_COMMA: return ",";
            case TokenType::TOKEN_TRANSPILE: return "TRANSPILE";
            case TokenType::TOKEN_SEMICOLON: return ";";
            case TokenType::TOKEN_DOT: return ".";
            case TokenType::TOKEN_ARROW: return "->";
            case TokenType::TOKEN_CLASS: return "CLASS";
            case TokenType::TOKEN_FUNC: return "FUNC";
            case TokenType::TOKEN_NEW: return "NEW";
            case TokenType::TOKEN_THIS: return "THIS";
            case TokenType::TOKEN_SUPER: return "SUPER";
            case TokenType::TOKEN_RETURN: return "RETURN";
            case TokenType::TOKEN_PUBLIC: return "PUBLIC";
            case TokenType::TOKEN_PRIVATE: return "PRIVATE";
            case TokenType::TOKEN_EXTENDS: return "EXTENDS";
            case TokenType::TOKEN_INSTANCEOF: return "INSTANCEOF";
            case TokenType::TOKEN_STATIC: return "STATIC";
            case TokenType::TOKEN_USING: return "USING";
            case TokenType::TOKEN_NONE: return "NONE";
            case TokenType::TOKEN_EOF: return "EOF";
            case TokenType::TOKEN_PLUS_ASSIGN: return "+=";
            case TokenType::TOKEN_MINUS_ASSIGN: return "-=";
            case TokenType::TOKEN_MULTIPLY_ASSIGN: return "*=";
            case TokenType::TOKEN_DIVIDE_ASSIGN: return "/=";
            case TokenType::TOKEN_MOD_ASSIGN: return "%=";
            case TokenType::TOKEN_INCREMENT: return "++";
            case TokenType::TOKEN_DECREMENT: return "--";
            case TokenType::TOKEN_RANGE: return "..";
            case TokenType::TOKEN_COALESCE: return "??";
            case TokenType::TOKEN_QUESTION_DOT: return "?.";
            default: return "UNKNOWN";
        }
    }
}
