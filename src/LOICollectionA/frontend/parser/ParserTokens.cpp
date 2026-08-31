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

        std::string getCompoundAssignOp(TokenType type) {
            static const std::unordered_map<TokenType, std::string> ops = {
                { TokenType::TOKEN_PLUS_ASSIGN, "+" },
                { TokenType::TOKEN_MINUS_ASSIGN, "-" },
                { TokenType::TOKEN_MULTIPLY_ASSIGN, "*" },
                { TokenType::TOKEN_DIVIDE_ASSIGN, "/" },
                { TokenType::TOKEN_MOD_ASSIGN, "%" },
            };
            auto it = ops.find(type);
            return it == ops.end() ? std::string{} : it->second;
        }

        bool isAssignableExpr(const ExprNode* expr) {
            ASTNode::Type type = expr->getType();

            return type == ASTNode::Type::Variable ||
                   type == ASTNode::Type::MemberAccess ||
                   type == ASTNode::Type::Index;
        }

    std::string Parser::getTokenName(TokenType type) {
        static const std::unordered_map<TokenType, std::string> names = {
            { TokenType::TOKEN_IF, "IF" },
            { TokenType::TOKEN_WHILE, "WHILE" },
            { TokenType::TOKEN_FOR, "FOR" },
            { TokenType::TOKEN_BREAK, "BREAK" },
            { TokenType::TOKEN_CONTINUE, "CONTINUE" },
            { TokenType::TOKEN_LPAREN, "(" },
            { TokenType::TOKEN_RPAREN, ")" },
            { TokenType::TOKEN_LBRCKET, "[" },
            { TokenType::TOKEN_RBRCKET, "]" },
            { TokenType::TOKEN_LBRACE, "{" },
            { TokenType::TOKEN_RBRACE, "}" },
            { TokenType::TOKEN_IDENT, "IDENT" },
            { TokenType::TOKEN_INT, "NUMBER" },
            { TokenType::TOKEN_FLOAT, "FLOAT" },
            { TokenType::TOKEN_STRING, "STRING" },
            { TokenType::TOKEN_OP, "OP" },
            { TokenType::TOKEN_BOOL_OP, "BOOL_OP" },
            { TokenType::TOKEN_COLON, ":" },
            { TokenType::TOKEN_BOOL_LIT, "BOOL_LIT" },
            { TokenType::TOKEN_PLUS, "+" },
            { TokenType::TOKEN_MINUS, "-" },
            { TokenType::TOKEN_MULTIPLY, "*" },
            { TokenType::TOKEN_DIVIDE, "/" },
            { TokenType::TOKEN_MOD, "%" },
            { TokenType::TOKEN_POWER, "^" },
            { TokenType::TOKEN_NAMESPACE, "NAMESPACE" },
            { TokenType::TOKEN_COMMA, "," },
            { TokenType::TOKEN_TRANSPILE, "TRANSPILE" },
            { TokenType::TOKEN_SEMICOLON, ";" },
            { TokenType::TOKEN_DOT, "." },
            { TokenType::TOKEN_ARROW, "->" },
            { TokenType::TOKEN_CLASS, "CLASS" },
            { TokenType::TOKEN_FUNC, "FUNC" },
            { TokenType::TOKEN_NEW, "NEW" },
            { TokenType::TOKEN_THIS, "THIS" },
            { TokenType::TOKEN_SUPER, "SUPER" },
            { TokenType::TOKEN_RETURN, "RETURN" },
            { TokenType::TOKEN_PUBLIC, "PUBLIC" },
            { TokenType::TOKEN_PRIVATE, "PRIVATE" },
            { TokenType::TOKEN_EXTENDS, "EXTENDS" },
            { TokenType::TOKEN_INSTANCEOF, "INSTANCEOF" },
            { TokenType::TOKEN_STATIC, "STATIC" },
            { TokenType::TOKEN_USING, "USING" },
            { TokenType::TOKEN_NONE, "NONE" },
            { TokenType::TOKEN_EOF, "EOF" },
            { TokenType::TOKEN_PLUS_ASSIGN, "+=" },
            { TokenType::TOKEN_MINUS_ASSIGN, "-=" },
            { TokenType::TOKEN_MULTIPLY_ASSIGN, "*=" },
            { TokenType::TOKEN_DIVIDE_ASSIGN, "/=" },
            { TokenType::TOKEN_MOD_ASSIGN, "%=" },
            { TokenType::TOKEN_INCREMENT, "++" },
            { TokenType::TOKEN_DECREMENT, "--" },
            { TokenType::TOKEN_RANGE, ".." },
            { TokenType::TOKEN_COALESCE, "??" },
            { TokenType::TOKEN_QUESTION_DOT, "?." },
            { TokenType::TOKEN_IMPORT, "IMPORT" },
            { TokenType::TOKEN_COMPONENT, "COMPONENT" },
            { TokenType::TOKEN_LET, "LET" },
        };
        auto it = names.find(type);
        return it == names.end() ? "UNKNOWN" : it->second;
    }

}
