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

            if (this->declarativeDepth > 0 &&
                currentToken.type == TokenType::TOKEN_IDENT && currentToken.value == "on" &&
                peek() == TokenType::TOKEN_COLON) {
                eat(TokenType::TOKEN_IDENT);
                eat(TokenType::TOKEN_COLON);
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
                diagnostics.addError(loc, "'\?\?' cannot be mixed with '&&' or '||' without parentheses");

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

                auto node = std::make_unique<NewNode>(loc, std::move(className), std::move(args));

                if (currentToken.type == TokenType::TOKEN_LBRACE) {
                    if (!eat(TokenType::TOKEN_LBRACE)) return nullptr;

                    ++this->declarativeDepth;
                    node->declarativeBlock = parseBlock(TokenType::TOKEN_RBRACE, false);
                    --this->declarativeDepth;

                    if (!eat(TokenType::TOKEN_RBRACE)) {
                        diagnostics.addError(currentToken.loc, "Expected '}' to close declarative UI block");
                        synchronize({ TokenType::TOKEN_RBRACE, TokenType::TOKEN_SEMICOLON });
                    }
                }

                return node;
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

}
