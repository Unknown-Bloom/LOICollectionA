#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend {
    class OwnershipChecker {
    public:
        LOICOLLECTION_A_API explicit OwnershipChecker(DiagnosticEngine& diag);

        LOICOLLECTION_A_API void check(ProgramNode& root);

    private:
        struct Binding {
            std::string name;
            TypeInfo type;
            int lambdaDepth = 0;
            bool global = false;
            bool moved = false;
            bool loopReported = false;
            SourceLocation moveLoc;
        };

        struct Flow {
            std::vector<bool> moved;
            bool reachable = true;
        };

        DiagnosticEngine& diagnostics;
        std::vector<Binding> bindings;
        std::vector<std::unordered_map<std::string, int>> scopes;
        std::unordered_map<std::string, int> globalBindings;
        std::vector<int> borrows;
        std::unordered_map<int, int> loanTarget;
        std::vector<int> temporaries;
        std::vector<int> captures;
        int lambdaDepth = 0;
        int functionDepth = 0;
        bool reachable = true;

        [[nodiscard]] int findBinding(const std::string& name) const;
        int declare(const std::string& name, const TypeInfo& type, SourceLocation loc, bool global);
        void pushScope();
        void popScope();
        void releaseTemporaryBorrows();
        void addBorrow(int target);
        void releaseLoans(int refBinding);
        void acquireLoans(int refBinding, const ExprNode* rhs);
        void useBinding(int id, SourceLocation loc);
        void moveBinding(int id, SourceLocation loc);
        void movePlace(ExprNode& expr, SourceLocation loc);
        [[nodiscard]] int rootBinding(const ExprNode& expr) const;
        [[nodiscard]] bool isBorrowed(int id) const;
        [[nodiscard]] Flow snapshot() const;
        void restore(const Flow& flow);
        void mergeBranches(const Flow& before, const Flow& first, const Flow& second);
        void mergeLoopExit(const Flow& before);
        void checkMovedInsideLoop(const Flow& before);

        void checkSequence(SequenceNode& sequence);
        void checkStatement(ASTNode& node);
        void checkExpr(ExprNode& expr);
        void checkIf(IfNode& node);
        void checkMatch(MatchNode& node);
        void checkWhile(WhileNode& node);
        void checkFor(ForNode& node);
        void checkForIn(ForInNode& node);
        void checkLambda(LambdaNode& node);
        void checkAssignment(AssignmentNode& node);
        void checkCallParams(
            const std::vector<TypeInfo>& paramTypes,
            std::vector<std::unique_ptr<ExprNode>>& args,
            SourceLocation loc,
            const std::string& callee);
        void checkFunction(MethodDecl& method);
        void checkClass(ClassNode& cls);
    };
}
