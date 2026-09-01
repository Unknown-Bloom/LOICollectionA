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
            } else if (stmtType == ASTNode::Type::Class || stmtType == ASTNode::Type::FunctionDef ||
                       stmtType == ASTNode::Type::Trait || stmtType == ASTNode::Type::Impl) {
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

}
