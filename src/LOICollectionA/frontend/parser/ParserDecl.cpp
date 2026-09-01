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

    std::vector<TypeParam> Parser::parseTypeParams() {
        std::vector<TypeParam> params;

        if (currentToken.type != TokenType::TOKEN_OP || currentToken.value != "<")
            return params;
        eat(TokenType::TOKEN_OP);

        while ((currentToken.type != TokenType::TOKEN_OP || currentToken.value != ">") &&
               currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type != TokenType::TOKEN_IDENT) {
                diagnostics.addError(currentToken.loc, "Expected type parameter name");
                synchronize({ TokenType::TOKEN_COMMA, TokenType::TOKEN_RPAREN, TokenType::TOKEN_LBRACE });
                if (currentToken.type != TokenType::TOKEN_IDENT)
                    break;
                continue;
            }

            TypeParam param;
            param.name = currentToken.value;
            eat(TokenType::TOKEN_IDENT);

            for (const auto& existing : params) {
                if (existing.name == param.name) {
                    diagnostics.addError(currentToken.loc,
                        "Duplicate type parameter '" + param.name + "'");
                    break;
                }
            }

            while (currentToken.type == TokenType::TOKEN_COLON) {
                eat(TokenType::TOKEN_COLON);
                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected trait name after ':'");
                    break;
                }
                param.bounds.push_back(currentToken.value);
                eat(TokenType::TOKEN_IDENT);
            }

            params.push_back(std::move(param));

            if (currentToken.type == TokenType::TOKEN_COMMA)
                eat(TokenType::TOKEN_COMMA);
            else
                break;
        }

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == ">")
            eat(TokenType::TOKEN_OP);

        return params;
    }

    std::unique_ptr<TraitNode> Parser::parseTrait() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_TRAIT)) return nullptr;
        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected trait name");
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto trait = std::make_unique<TraitNode>(loc, currentToken.value);
        eat(TokenType::TOKEN_IDENT);

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        while (currentToken.type != TokenType::TOKEN_RBRACE &&
               currentToken.type != TokenType::TOKEN_EOF) {
            if (currentToken.type == TokenType::TOKEN_FUNC) {
                auto method = parseMethod(false, false);
                if (method)
                    trait->methods.push_back(std::move(*method));
                continue;
            }

            diagnostics.addError(currentToken.loc, "Expected method declaration in trait");
            synchronize({ TokenType::TOKEN_FUNC, TokenType::TOKEN_RBRACE });
        }

        if (currentToken.type == TokenType::TOKEN_RBRACE)
            eat(TokenType::TOKEN_RBRACE);

        return trait;
    }

    std::unique_ptr<ImplNode> Parser::parseImpl() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_IMPL)) return nullptr;

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "<") {
            diagnostics.addError(currentToken.loc,
                "Generic impl blocks require generic classes, which are not part of the language yet");
            skipBalancedBraces();
            return nullptr;
        }

        auto first = parseTypeExpr();
        if (!first) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        auto impl = std::make_unique<ImplNode>(loc);

        if (currentToken.type == TokenType::TOKEN_FOR) {
            if (!eat(TokenType::TOKEN_FOR)) return nullptr;
            impl->trait = std::move(*first);
            auto target = parseTypeExpr();
            if (!target) {
                synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
                skipBalancedBraces();
                return nullptr;
            }
            impl->target = std::move(*target);
        } else {
            impl->target = std::move(*first);
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        while (currentToken.type != TokenType::TOKEN_RBRACE &&
               currentToken.type != TokenType::TOKEN_EOF) {
            bool isStatic = false;
            if (currentToken.type == TokenType::TOKEN_STATIC) {
                if (!eat(TokenType::TOKEN_STATIC)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }
                isStatic = true;
            }

            if (currentToken.type == TokenType::TOKEN_FUNC) {
                auto method = parseMethod(false);
                if (!method) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }
                method->isStatic = isStatic;
                impl->methods.push_back(std::move(*method));
                continue;
            }

            if (currentToken.type == TokenType::TOKEN_CONST) {
                if (!eat(TokenType::TOKEN_CONST)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected associated constant name");
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                ClassMember member;
                member.loc = currentToken.loc;
                member.name = currentToken.value;
                member.isStatic = true;

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

                if (currentToken.type != TokenType::TOKEN_OP || currentToken.value != "=") {
                    diagnostics.addError(currentToken.loc,
                        "Expected '=' after associated constant '" + member.name + "'");
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

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

                if (!eat(TokenType::TOKEN_SEMICOLON)) {
                    synchronize({ TokenType::TOKEN_RBRACE });
                    continue;
                }

                impl->consts.push_back(std::move(member));
                continue;
            }

            diagnostics.addError(currentToken.loc,
                "Unexpected token in impl block: " + getTokenName(currentToken.type) + " (" + currentToken.value + ")");
            synchronize({ TokenType::TOKEN_RBRACE });
        }

        if (currentToken.type == TokenType::TOKEN_RBRACE)
            eat(TokenType::TOKEN_RBRACE);

        return impl;
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

        if (!eat(TokenType::TOKEN_IDENT)) {
            synchronize({ TokenType::TOKEN_LBRACE, TokenType::TOKEN_RBRACE });
            skipBalancedBraces();
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_OP && currentToken.value == "<")
            fn->decl.typeParams = parseTypeParams();

        if (!eat(TokenType::TOKEN_LPAREN)) {
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

        size_t savedDeclarativeDepth = this->declarativeDepth;
        this->declarativeDepth = 0;
        lambda->decl.body = parseBlock(TokenType::TOKEN_RBRACE, false);
        this->declarativeDepth = savedDeclarativeDepth;

        if (!eat(TokenType::TOKEN_RBRACE)) {
            synchronize({ TokenType::TOKEN_RBRACE });
            if (currentToken.type == TokenType::TOKEN_RBRACE)
                eat(TokenType::TOKEN_RBRACE);
        }

        return lambda;
    }

    std::unique_ptr<MethodDecl> Parser::parseMethod(bool isPrivate, bool requireBody) {
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

        if (!requireBody) {
            if (currentToken.type == TokenType::TOKEN_SEMICOLON)
                eat(TokenType::TOKEN_SEMICOLON);
            else if (currentToken.type != TokenType::TOKEN_RBRACE)
                diagnostics.addError(currentToken.loc,
                    "Expected ';' after trait method declaration");
            return method;
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

    std::unique_ptr<ImportNode> Parser::parseImport() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_IMPORT)) return nullptr;

        if (currentToken.type != TokenType::TOKEN_STRING) {
            diagnostics.addError(currentToken.loc, "Expected a string path after 'import'");
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        std::string path = std::move(currentToken.value);
        eat(TokenType::TOKEN_STRING);

        return std::make_unique<ImportNode>(loc, std::move(path));
    }

    std::unique_ptr<ComponentNode> Parser::parseComponent() {
        SourceLocation loc = currentToken.loc;

        if (!eat(TokenType::TOKEN_COMPONENT)) return nullptr;

        if (currentToken.type != TokenType::TOKEN_IDENT) {
            diagnostics.addError(currentToken.loc, "Expected a component name after 'component'");
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        auto node = std::make_unique<ComponentNode>(loc, std::move(currentToken.value));
        eat(TokenType::TOKEN_IDENT);

        if (!eat(TokenType::TOKEN_LPAREN)) {
            diagnostics.addError(currentToken.loc, "Expected '(' after component name");
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        if (currentToken.type == TokenType::TOKEN_IDENT) {
            node->params.push_back(std::move(currentToken.value));
            eat(TokenType::TOKEN_IDENT);

            while (currentToken.type == TokenType::TOKEN_COMMA) {
                eat(TokenType::TOKEN_COMMA);

                if (currentToken.type != TokenType::TOKEN_IDENT) {
                    diagnostics.addError(currentToken.loc, "Expected a parameter name");
                    synchronize({ TokenType::TOKEN_RPAREN, TokenType::TOKEN_RBRACE });
                    return nullptr;
                }

                node->params.push_back(std::move(currentToken.value));
                eat(TokenType::TOKEN_IDENT);
            }
        }

        if (!eat(TokenType::TOKEN_RPAREN)) {
            diagnostics.addError(currentToken.loc, "Expected ')' after component parameters");
            synchronize({ TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        if (!eat(TokenType::TOKEN_LBRACE)) {
            diagnostics.addError(currentToken.loc, "Expected '{' to open the component body");
            synchronize({ TokenType::TOKEN_SEMICOLON, TokenType::TOKEN_RBRACE });
            return nullptr;
        }

        ++this->declarativeDepth;
        node->body = parseBlock(TokenType::TOKEN_RBRACE, false);
        --this->declarativeDepth;

        if (!eat(TokenType::TOKEN_RBRACE)) {
            diagnostics.addError(currentToken.loc, "Expected '}' to close the component body");
            synchronize({ TokenType::TOKEN_RBRACE, TokenType::TOKEN_SEMICOLON });
        }

        return node;
    }

}
