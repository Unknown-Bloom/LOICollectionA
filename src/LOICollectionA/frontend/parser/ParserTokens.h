#pragma once

#include <string>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Lexer.h"

namespace LOICollection::frontend {
    std::string getCompoundAssignOp(TokenType type);
    bool isAssignableExpr(const ExprNode* expr);
}
