#include <string>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <utility>

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

namespace LOICollection::frontend {
    Lexer::Lexer(std::string str, DiagnosticEngine& diag) : input(std::move(str)), position(0), line(1), column(1),
        currentChar(input.empty() ? static_cast<char>(0) : input[0]), diagnostics(diag) {}

    void Lexer::advance() {
        if (currentChar == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }

        position++;
        currentChar = (position < input.size()) ? input[position] : static_cast<char>(0);
    }

    Token Lexer::getNextToken() {
        while (currentChar != 0) {
            if (std::isspace(static_cast<unsigned char>(currentChar))) {
                skipWhitespace();
                continue;
            }

            if (currentChar == '/' && (peekChar() == '/' || peekChar() == '*')) {
                skipComment();
                continue;
            }

            switch (currentChar) {
                case '"': return parseString('"');
                case '\'': return parseString('\'');
                case '(': return makeToken(TokenType::TOKEN_LPAREN);
                case ')': return makeToken(TokenType::TOKEN_RPAREN);
                case '[': return makeToken(TokenType::TOKEN_LBRCKET);
                case ']': return makeToken(TokenType::TOKEN_RBRCKET);
                case '{': return makeToken(TokenType::TOKEN_LBRACE);
                case '}': return makeToken(TokenType::TOKEN_RBRACE);
                case ':': return parseColon();
                case '+': {
                    if (peekChar() == '=')
                        return makeTwoCharToken(TokenType::TOKEN_PLUS_ASSIGN);
                    if (peekChar() == '+')
                        return makeTwoCharToken(TokenType::TOKEN_INCREMENT);

                    return makeToken(TokenType::TOKEN_PLUS);
                }
                case '*': {
                    if (peekChar() == '=')
                        return makeTwoCharToken(TokenType::TOKEN_MULTIPLY_ASSIGN);

                    return makeToken(TokenType::TOKEN_MULTIPLY);
                }
                case '/': {
                    if (peekChar() == '=')
                        return makeTwoCharToken(TokenType::TOKEN_DIVIDE_ASSIGN);

                    return makeToken(TokenType::TOKEN_DIVIDE);
                }
                case '%': {
                    if (peekChar() == '=')
                        return makeTwoCharToken(TokenType::TOKEN_MOD_ASSIGN);

                    return makeToken(TokenType::TOKEN_MOD);
                }
                case '^': return makeToken(TokenType::TOKEN_POWER);
                case ',': return makeToken(TokenType::TOKEN_COMMA);
                case '$': return makeToken(TokenType::TOKEN_TRANSPILE);
                case ';': return makeToken(TokenType::TOKEN_SEMICOLON);
                case '?': {
                    if (peekChar() == '?')
                        return makeTwoCharToken(TokenType::TOKEN_COALESCE);
                    if (peekChar() == '.')
                        return makeTwoCharToken(TokenType::TOKEN_QUESTION_DOT);

                    diagnostics.addError({line, column, position}, "Unexpected character '?'");
                    return makeToken(TokenType::TOKEN_OP);
                }
                case '.': {
                    if (std::isdigit(static_cast<unsigned char>(peekChar())))
                        return parseNumber();

                    if (peekChar() == '.') {
                        /* "1...10" would otherwise lex as "1", "..", ".10" and
                         * fail later with a misleading message — reject it here. */
                        if (position + 2 < input.size() && input[position + 2] == '.') {
                            diagnostics.addError({line, column, position},
                                "Unexpected '...' (a range uses exactly two dots: '..')");
                            return makeToken(TokenType::TOKEN_OP);
                        }

                        return makeTwoCharToken(TokenType::TOKEN_RANGE);
                    }

                    return makeToken(TokenType::TOKEN_DOT);
                }
            }

            if (std::isdigit(static_cast<unsigned char>(currentChar))) return parseNumber();
            if (std::strchr("=><!&|-", currentChar)) return parseOperator();

            return parseIdentifier();
        }
        
        return { TokenType::TOKEN_EOF, "", {line, column, position} };
    }

    Token Lexer::peekNextToken() {
        size_t savedPos = position;
        size_t savedLine = line;
        size_t savedCol = column;
        char savedChar = currentChar;

        Token t = getNextToken();

        position = savedPos;
        line = savedLine;
        column = savedCol;
        currentChar = savedChar;

        return t;
    }

    Token Lexer::parseString(char delimiter) {
        advance();

        SourceLocation startLoc(line, column, position);
        std::string value;

        while (currentChar != delimiter && currentChar != 0) {
            if (currentChar == '\\') {
                SourceLocation escapeLoc(line, column, position);

                advance();
                if (currentChar == 0) {
                    diagnostics.addError(startLoc, "Unclosed string");
                    return { TokenType::TOKEN_EOF, "", startLoc };
                }

                switch (currentChar) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default:
                        diagnostics.addWarning(escapeLoc, std::string("Unknown escape sequence '\\") + currentChar + "' is kept as literal");
                        value += '\\';
                        value += currentChar;
                        break;
                }

                advance();
                continue;
            }

            value += currentChar;
            advance();
        }

        if (currentChar != delimiter) {
            diagnostics.addError(startLoc, "Unclosed string");
            return { TokenType::TOKEN_EOF, "", startLoc };
        }

        advance();
        return { TokenType::TOKEN_STRING, std::move(value), startLoc };
    }

    Token Lexer::parseIdentifier() {
        size_t start = position;

        SourceLocation startLoc(line, column, start);
        while (currentChar != 0 &&
               !std::isspace(static_cast<unsigned char>(currentChar)) &&
               !std::strchr("()[]{}=><!&|.:;,+-*/%^$\"'`?", currentChar))
            advance();

        std::string id = input.substr(start, position - start);

        if (id == "if") return { TokenType::TOKEN_IF, std::move(id), startLoc };
        if (id == "true" || id == "false") return { TokenType::TOKEN_BOOL_LIT, std::move(id), startLoc };
        if (id == "class") return { TokenType::TOKEN_CLASS, std::move(id), startLoc };
        if (id == "func") return { TokenType::TOKEN_FUNC, std::move(id), startLoc };
        if (id == "new") return { TokenType::TOKEN_NEW, std::move(id), startLoc };
        if (id == "this") return { TokenType::TOKEN_THIS, std::move(id), startLoc };
        if (id == "super") return { TokenType::TOKEN_SUPER, std::move(id), startLoc };
        if (id == "return") return { TokenType::TOKEN_RETURN, std::move(id), startLoc };
        if (id == "public") return { TokenType::TOKEN_PUBLIC, std::move(id), startLoc };
        if (id == "private") return { TokenType::TOKEN_PRIVATE, std::move(id), startLoc };
        if (id == "extends") return { TokenType::TOKEN_EXTENDS, std::move(id), startLoc };
        if (id == "instanceof") return { TokenType::TOKEN_INSTANCEOF, std::move(id), startLoc };
        if (id == "static") return { TokenType::TOKEN_STATIC, std::move(id), startLoc };
        if (id == "using") return { TokenType::TOKEN_USING, std::move(id), startLoc };
        if (id == "None") return { TokenType::TOKEN_NONE, std::move(id), startLoc };
        if (id == "while") return { TokenType::TOKEN_WHILE, std::move(id), startLoc };
        if (id == "for") return { TokenType::TOKEN_FOR, std::move(id), startLoc };
        if (id == "break") return { TokenType::TOKEN_BREAK, std::move(id), startLoc };
        if (id == "continue") return { TokenType::TOKEN_CONTINUE, std::move(id), startLoc };
        if (id == "import") return { TokenType::TOKEN_IMPORT, std::move(id), startLoc };
        if (id == "component") return { TokenType::TOKEN_COMPONENT, std::move(id), startLoc };
        if (id == "let") return { TokenType::TOKEN_LET, std::move(id), startLoc };
        if (id == "trait") return { TokenType::TOKEN_TRAIT, std::move(id), startLoc };

        return { TokenType::TOKEN_IDENT, std::move(id), startLoc };
    }

    Token Lexer::parseNumber() {
        size_t start = position;

        SourceLocation startLoc(line, column, start);

        bool hasDot = false;
        while (currentChar != 0) {
            if (std::isdigit(static_cast<unsigned char>(currentChar))) {
                advance();
            } else if (currentChar == '.' && !hasDot && peekChar() != '.') {
                hasDot = true;
                advance();
            } else {
                break;
            }
        }
        
        std::string num = input.substr(start, position - start);
        if (num.empty() || num == ".") {
            diagnostics.addError(startLoc, "Invalid numeric literal: '" + num + "'");
            return { TokenType::TOKEN_EOF, "", startLoc };
        }
        
        if (hasDot)
            return { TokenType::TOKEN_FLOAT, num, startLoc };

        return { TokenType::TOKEN_INT, num, startLoc };
    }

    Token Lexer::parseColon() {
        SourceLocation startLoc(line, column, position);

        advance();

        if (currentChar == ':') {
            advance();

            return { TokenType::TOKEN_NAMESPACE, "::", startLoc };
        }

        return { TokenType::TOKEN_COLON, ":", startLoc };
    }

    Token Lexer::parseOperator() {
        SourceLocation startLoc(line, column, position);

        char first = currentChar;

        advance();

        if (first == '-' && currentChar == '>') {
            advance();

            return { TokenType::TOKEN_ARROW, "->", startLoc };
        }

        if (first == '-' && currentChar == '-') {
            advance();

            return { TokenType::TOKEN_DECREMENT, "--", startLoc };
        }

        if (first == '-' && currentChar == '=') {
            advance();

            return { TokenType::TOKEN_MINUS_ASSIGN, "-=", startLoc };
        }

        if ((first == '&' && currentChar == '&') || (first == '|' && currentChar == '|')) {
            std::string op(1, first);
            op += currentChar;

            advance();
            return { TokenType::TOKEN_BOOL_OP, op, startLoc };
        }

        if (currentChar == '=') {
            std::string op(1, first);
            op += '=';

            advance();
            return { TokenType::TOKEN_OP, op, startLoc };
        }

        if (first == '-')
            return { TokenType::TOKEN_MINUS, "-", startLoc };

        return { TokenType::TOKEN_OP, std::string(1, first), startLoc };
    }

    void Lexer::skipWhitespace() {
        while (std::isspace(static_cast<unsigned char>(currentChar)))
            advance();
    }

    void Lexer::skipComment() {
        SourceLocation startLoc(line, column, position);

        if (peekChar() == '/') {
            while (currentChar != '\n' && currentChar != 0)
                advance();

            return;
        }

        advance();
        advance();

        while (currentChar != 0 && !(currentChar == '*' && peekChar() == '/'))
            advance();

        if (currentChar == 0) {
            diagnostics.addError(startLoc, "Unclosed block comment");
            return;
        }

        advance();
        advance();
    }

    char Lexer::peekChar() const {
        return (position + 1 < input.size()) ? input[position + 1] : static_cast<char>(0);
    }

    Token Lexer::makeToken(TokenType type) {
        Token t{ type, std::string(1, currentChar), {line, column, position} };

        advance();

        return t;
    }

    Token Lexer::makeTwoCharToken(TokenType type) {
        Token t{ type, std::string(1, currentChar) + peekChar(), {line, column, position} };

        advance();
        advance();

        return t;
    }
}
