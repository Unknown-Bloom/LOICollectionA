#include <memory>
#include <string>
#include <charconv>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"

#include "LOICollectionA/frontend/parser/ParserTokens.h"


namespace LOICollection::frontend {

    std::unique_ptr<IfNode> Parser::parseIfStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_IF)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_COLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto cond = parseBoolExpression();
        if (!cond || !eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_COLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        bool braced = currentToken.type == TokenType::TOKEN_LBRACE;
        if (!braced && !eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_COLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto truePart = parseBlock(braced ? TokenType::TOKEN_RBRACE : TokenType::TOKEN_COLON, !braced);
        if (braced) {
            if (!eat(TokenType::TOKEN_RBRACE)) {
                synchronize({ TokenType::TOKEN_RBRACE });
                return nullptr;
            }

            if (currentToken.type != TokenType::TOKEN_COLON)
                return std::make_unique<IfNode>(loc, std::move(cond), std::move(truePart), nullptr);
        } else if (currentToken.type == TokenType::TOKEN_RBRCKET) {
            if (!eat(TokenType::TOKEN_RBRCKET)) return nullptr;
            return std::make_unique<IfNode>(loc, std::move(cond), std::move(truePart), nullptr);
        }

        if (!eat(TokenType::TOKEN_COLON)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        bool elseBraced = currentToken.type == TokenType::TOKEN_LBRACE;
        if (elseBraced && !eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto falsePart = parseBlock(
            elseBraced ? TokenType::TOKEN_RBRACE : TokenType::TOKEN_RBRCKET, false
        );

        if (elseBraced) {
            if (!eat(TokenType::TOKEN_RBRACE)) {
                synchronize({ TokenType::TOKEN_RBRACE });
                return nullptr;
            }
        } else if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        return std::make_unique<IfNode>(loc, std::move(cond), std::move(truePart), std::move(falsePart));
    }

    std::unique_ptr<BlockNode> Parser::parseControlBody() {
        if (currentToken.type == TokenType::TOKEN_LBRACE) {
            if (!eat(TokenType::TOKEN_LBRACE)) return nullptr;

            auto body = parseBlock(TokenType::TOKEN_RBRACE, false);
            if (!eat(TokenType::TOKEN_RBRACE)) {
                synchronize({ TokenType::TOKEN_RBRACE });
                return nullptr;
            }

            return body;
        }

        if (!eat(TokenType::TOKEN_LBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto body = parseBlock(TokenType::TOKEN_RBRCKET, false);
        if (!eat(TokenType::TOKEN_RBRCKET)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        return body;
    }

    std::unique_ptr<WhileNode> Parser::parseWhileStatement() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_WHILE)) return nullptr;
        if (!eat(TokenType::TOKEN_LPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto cond = parseBoolExpression();
        if (!cond || !eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto body = parseControlBody();
        if (!body)
            return nullptr;

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

        if (!eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto body = parseControlBody();
        if (!body)
            return nullptr;

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

        if (!eat(TokenType::TOKEN_RPAREN)) {
            synchronize({ TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        node->iterable = std::move(iterable);
        node->body = parseControlBody();

        if (!node->body)
            return nullptr;

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

        if (currentToken.type == TokenType::TOKEN_LET) {
            auto decl = parseVariableDeclaration();
            return decl ? std::unique_ptr<ExprNode>(std::move(decl)) : nullptr;
        }

        auto expr = parseBaseExpression();
        if (!expr)
            return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            auto assignment = std::make_unique<AssignmentNode>(loc, std::move(expr), std::move(right));
            this->bindDeclarativeReceiver(*assignment);
            return assignment;
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

            if (currentToken.type == TokenType::TOKEN_IMPORT) {
                diagnostics.addError(currentToken.loc,
                    "Import declarations are only allowed at top level");

                synchronize(stopOnColon
                    ? std::initializer_list<TokenType>{ TokenType::TOKEN_COLON, TokenType::TOKEN_RBRCKET, TokenType::TOKEN_RBRACE }
                    : std::initializer_list<TokenType>{ stopToken, TokenType::TOKEN_RBRACE });
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_COMPONENT) {
                diagnostics.addError(currentToken.loc,
                    "Component definitions are only allowed at top level");

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

    void Parser::bindDeclarativeReceiver(AssignmentNode& node) {
        if (node.target->getType() != ASTNode::Type::Variable || !node.value)
            return;
        if (node.value->getType() != ASTNode::Type::New)
            return;

        auto& newExpr = static_cast<NewNode&>(*node.value);
        if (newExpr.declarativeBlock)
            newExpr.receiverName = static_cast<VariableNode&>(*node.target).name;
    }

    std::unique_ptr<AssignmentNode> Parser::parseVariableDeclaration() {
        const bool hasLet = currentToken.type == TokenType::TOKEN_LET;
        SourceLocation declLoc = currentToken.loc;

        if (hasLet && !eat(TokenType::TOKEN_LET))
            return nullptr;

        if (currentToken.type != TokenType::TOKEN_IDENT) {
            this->diagnostics.addError(currentToken.loc,
                hasLet ? "Expected a variable name after 'let'" : "Expected a variable name");
            return nullptr;
        }

        std::string name = currentToken.value;
        if (!eat(TokenType::TOKEN_IDENT))
            return nullptr;

        std::optional<TypeExpr> type;
        if (currentToken.type == TokenType::TOKEN_COLON) {
            if (!eat(TokenType::TOKEN_COLON))
                return nullptr;

            auto parsed = parseTypeExpr();
            if (!parsed)
                return nullptr;

            type = std::move(*parsed);
        }

        std::unique_ptr<ExprNode> value;
        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
            if (!eat(TokenType::TOKEN_OP))
                return nullptr;

            value = parseBaseExpression();
            if (!value)
                return nullptr;
        }

        auto decl = std::make_unique<AssignmentNode>(
            declLoc,
            std::make_unique<VariableNode>(declLoc, std::move(name)),
            std::move(value)
        );
        this->bindDeclarativeReceiver(*decl);
        decl->isDeclaration = hasLet;

        if (type) {
            decl->hasDeclaredType = true;
            decl->declaredType = std::move(*type);
        }

        return decl;
    }

    std::unique_ptr<ASTNode> Parser::parseStatement() {
        if (currentToken.type == TokenType::TOKEN_FUNC && peek() != TokenType::TOKEN_LPAREN)
            return parseFunctionDefinition();

        if (currentToken.type == TokenType::TOKEN_CLASS)
            return parseClass();

        if (currentToken.type == TokenType::TOKEN_TRAIT)
            return parseTrait();

        if (currentToken.type == TokenType::TOKEN_IMPL)
            return parseImpl();

        if (currentToken.type == TokenType::TOKEN_IMPORT)
            return parseImport();

        if (currentToken.type == TokenType::TOKEN_COMPONENT)
            return parseComponent();

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

        if (currentToken.type == TokenType::TOKEN_LET ||
            (currentToken.type == TokenType::TOKEN_IDENT && peek() == TokenType::TOKEN_COLON))
            return parseVariableDeclaration();

        SourceLocation exprLoc = currentToken.loc;

        auto expr = parseBaseExpression();
        if (!expr)
            return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "=") {
            if (!eat(TokenType::TOKEN_OP)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            auto assignment = std::make_unique<AssignmentNode>(exprLoc, std::move(expr), std::move(right));
            this->bindDeclarativeReceiver(*assignment);
            return assignment;
        }

        std::string op = getCompoundAssignOp(currentToken.type);
        if (!op.empty()) {
            if (!eat(currentToken.type)) return nullptr;

            auto right = parseBaseExpression();
            if (!right)
                return nullptr;

            return std::make_unique<CompoundAssignNode>(exprLoc, std::move(expr), std::move(right), op);
        }

        if (this->declarativeDepth > 0 && expr->getType() == ASTNode::Type::FuncCall)
            static_cast<FuncCallNode&>(*expr).isFormReceiverCall = true;

        return expr;
    }

}
