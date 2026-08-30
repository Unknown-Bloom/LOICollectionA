#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ComponentExpander.h"

namespace LOICollection::frontend {
    namespace {
        struct ExpansionContext {
            std::unordered_map<std::string, ComponentNode*> components;
            std::vector<std::string> stack;
            size_t counter = 0;
            DiagnosticEngine& diagnostics;
            bool ok = true;

            explicit ExpansionContext(DiagnosticEngine& diag)
                : diagnostics(diag) {}
        };

        bool isComponentCall(ASTNode& node) {
            return node.getType() == ASTNode::Type::FuncCall &&
                static_cast<FuncCallNode&>(node).isFormReceiverCall;
        }

        std::string renamed(const std::string& name, const std::unordered_set<std::string>& bound, const std::string& prefix) {
            return bound.contains(name) ? prefix + name : name;
        }

        void collectBoundNames(ASTNode& node, std::unordered_set<std::string>& names) {
            switch (node.getType()) {
                case ASTNode::Type::Assignment: {
                    auto& assign = static_cast<AssignmentNode&>(node);
                    if (assign.target && assign.target->getType() == ASTNode::Type::Variable) {
                        auto& var = static_cast<VariableNode&>(*assign.target);
                        if (!var.isStaticField)
                            names.insert(var.name);
                    }
                    if (assign.value)
                        collectBoundNames(*assign.value, names);
                    return;
                }
                case ASTNode::Type::CompoundAssign: {
                    auto& assign = static_cast<CompoundAssignNode&>(node);
                    if (assign.target && assign.target->getType() == ASTNode::Type::Variable) {
                        auto& var = static_cast<VariableNode&>(*assign.target);
                        if (!var.isStaticField)
                            names.insert(var.name);
                    }
                    if (assign.value)
                        collectBoundNames(*assign.value, names);
                    return;
                }
                case ASTNode::Type::ForIn: {
                    auto& forIn = static_cast<ForInNode&>(node);
                    names.insert(forIn.elementVar);
                    if (forIn.hasIndexVar)
                        names.insert(forIn.indexVar);
                    if (forIn.iterable)
                        collectBoundNames(*forIn.iterable, names);
                    if (forIn.body)
                        collectBoundNames(*forIn.body, names);
                    return;
                }
                case ASTNode::Type::Block: {
                    for (auto& part : static_cast<BlockNode&>(node).parts)
                        collectBoundNames(*part, names);
                    return;
                }
                case ASTNode::Type::If: {
                    auto& ifNode = static_cast<IfNode&>(node);
                    if (ifNode.condition)
                        collectBoundNames(*ifNode.condition, names);
                    if (ifNode.trueBranch)
                        collectBoundNames(*ifNode.trueBranch, names);
                    if (ifNode.falseBranch)
                        collectBoundNames(*ifNode.falseBranch, names);
                    return;
                }
                case ASTNode::Type::While: {
                    auto& whileNode = static_cast<WhileNode&>(node);
                    if (whileNode.condition)
                        collectBoundNames(*whileNode.condition, names);
                    if (whileNode.body)
                        collectBoundNames(*whileNode.body, names);
                    return;
                }
                case ASTNode::Type::For: {
                    auto& forNode = static_cast<ForNode&>(node);
                    if (forNode.init)
                        collectBoundNames(*forNode.init, names);
                    if (forNode.condition)
                        collectBoundNames(*forNode.condition, names);
                    if (forNode.step)
                        collectBoundNames(*forNode.step, names);
                    if (forNode.body)
                        collectBoundNames(*forNode.body, names);
                    return;
                }
                case ASTNode::Type::Lambda:
                case ASTNode::Type::FunctionDef:
                case ASTNode::Type::Class:
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                    return;
                default:
                    return;
            }
        }

        std::unique_ptr<ExprNode> cloneExpr(const ExprNode& node, const std::unordered_set<std::string>& bound, const std::string& prefix);
        std::unique_ptr<ASTNode> cloneStmt(const ASTNode& node, const std::unordered_set<std::string>& bound, const std::string& prefix);

        std::vector<std::unique_ptr<ExprNode>> cloneArgs(const std::vector<std::unique_ptr<ExprNode>>& args, const std::unordered_set<std::string>& bound, const std::string& prefix) {
            std::vector<std::unique_ptr<ExprNode>> result;
            result.reserve(args.size());
            for (auto& arg : args)
                result.push_back(cloneExpr(*arg, bound, prefix));
            return result;
        }

        std::unique_ptr<ExprNode> cloneExpr(const ExprNode& node, const std::unordered_set<std::string>& bound, const std::string& prefix) {
            switch (node.getType()) {
                case ASTNode::Type::Value: {
                    auto& value = static_cast<const ValueNode&>(node);
                    auto copy = std::make_unique<ValueNode>(value.loc, std::monostate{});
                    copy->value = value.value;
                    return copy;
                }
                case ASTNode::Type::Variable: {
                    auto& var = static_cast<const VariableNode&>(node);
                    auto copy = std::make_unique<VariableNode>(var.loc, renamed(var.name, bound, prefix));
                    copy->isStaticField = var.isStaticField;
                    copy->staticClassName = var.staticClassName;
                    return copy;
                }
                case ASTNode::Type::Assignment: {
                    auto& assign = static_cast<const AssignmentNode&>(node);
                    auto copy = std::make_unique<AssignmentNode>(
                        assign.loc,
                        cloneExpr(*assign.target, bound, prefix),
                        assign.value ? cloneExpr(*assign.value, bound, prefix) : nullptr
                    );
                    copy->declaredType = assign.declaredType;
                    copy->hasDeclaredType = assign.hasDeclaredType;
                    copy->isDeclaration = assign.isDeclaration;
                    return copy;
                }
                case ASTNode::Type::CompoundAssign: {
                    auto& assign = static_cast<const CompoundAssignNode&>(node);
                    return std::make_unique<CompoundAssignNode>(
                        assign.loc,
                        cloneExpr(*assign.target, bound, prefix),
                        cloneExpr(*assign.value, bound, prefix),
                        assign.op
                    );
                }
                case ASTNode::Type::Compare: {
                    auto& compare = static_cast<const CompareNode&>(node);
                    return std::make_unique<CompareNode>(
                        compare.loc,
                        cloneExpr(*compare.left, bound, prefix),
                        cloneExpr(*compare.right, bound, prefix),
                        compare.op
                    );
                }
                case ASTNode::Type::Logical: {
                    auto& logical = static_cast<const LogicalNode&>(node);
                    return std::make_unique<LogicalNode>(
                        logical.loc,
                        cloneExpr(*logical.left, bound, prefix),
                        cloneExpr(*logical.right, bound, prefix),
                        logical.op
                    );
                }
                case ASTNode::Type::Arithmetic: {
                    auto& arith = static_cast<const ArithmeticNode&>(node);
                    return std::make_unique<ArithmeticNode>(
                        arith.loc,
                        cloneExpr(*arith.left, bound, prefix),
                        cloneExpr(*arith.right, bound, prefix),
                        arith.op
                    );
                }
                case ASTNode::Type::Unary: {
                    auto& unary = static_cast<const UnaryNode&>(node);
                    return std::make_unique<UnaryNode>(
                        unary.loc,
                        cloneExpr(*unary.operand, bound, prefix),
                        unary.op
                    );
                }
                case ASTNode::Type::Range: {
                    auto& range = static_cast<const RangeNode&>(node);
                    return std::make_unique<RangeNode>(
                        range.loc,
                        cloneExpr(*range.start, bound, prefix),
                        cloneExpr(*range.end, bound, prefix)
                    );
                }
                case ASTNode::Type::Coalesce: {
                    auto& coalesce = static_cast<const CoalesceNode&>(node);
                    return std::make_unique<CoalesceNode>(
                        coalesce.loc,
                        cloneExpr(*coalesce.left, bound, prefix),
                        cloneExpr(*coalesce.right, bound, prefix)
                    );
                }
                case ASTNode::Type::If: {
                    auto& ifNode = static_cast<const IfNode&>(node);
                    return std::make_unique<IfNode>(
                        ifNode.loc,
                        cloneExpr(*ifNode.condition, bound, prefix),
                        cloneStmt(*ifNode.trueBranch, bound, prefix),
                        ifNode.falseBranch ? cloneStmt(*ifNode.falseBranch, bound, prefix) : nullptr
                    );
                }
                case ASTNode::Type::While: {
                    auto& whileNode = static_cast<const WhileNode&>(node);
                    return std::make_unique<WhileNode>(
                        whileNode.loc,
                        cloneExpr(*whileNode.condition, bound, prefix),
                        cloneStmt(*whileNode.body, bound, prefix)
                    );
                }
                case ASTNode::Type::For: {
                    auto& forNode = static_cast<const ForNode&>(node);
                    return std::make_unique<ForNode>(
                        forNode.loc,
                        forNode.init ? cloneExpr(*forNode.init, bound, prefix) : nullptr,
                        forNode.condition ? cloneExpr(*forNode.condition, bound, prefix) : nullptr,
                        forNode.step ? cloneExpr(*forNode.step, bound, prefix) : nullptr,
                        cloneStmt(*forNode.body, bound, prefix)
                    );
                }
                case ASTNode::Type::ForIn: {
                    auto& forIn = static_cast<const ForInNode&>(node);
                    auto copy = std::make_unique<ForInNode>(forIn.loc, renamed(forIn.elementVar, bound, prefix));
                    copy->indexVar = renamed(forIn.indexVar, bound, prefix);
                    copy->hasIndexVar = forIn.hasIndexVar;
                    copy->iterable = cloneExpr(*forIn.iterable, bound, prefix);
                    copy->body = cloneStmt(*forIn.body, bound, prefix);
                    return copy;
                }
                case ASTNode::Type::Function: {
                    auto& call = static_cast<const FunctionNode&>(node);
                    return std::make_unique<FunctionNode>(
                        call.loc,
                        cloneArgs(call.args, bound, prefix),
                        call.namespaces,
                        call.name
                    );
                }
                case ASTNode::Type::Macro: {
                    auto& macro = static_cast<const MacroNode&>(node);
                    return std::make_unique<MacroNode>(
                        macro.loc,
                        cloneArgs(macro.args, bound, prefix),
                        macro.name
                    );
                }
                case ASTNode::Type::FuncCall: {
                    auto& call = static_cast<const FuncCallNode&>(node);
                    auto copy = std::make_unique<FuncCallNode>(
                        call.loc,
                        call.name,
                        cloneArgs(call.args, bound, prefix)
                    );
                    copy->isFormReceiverCall = call.isFormReceiverCall;
                    return copy;
                }
                case ASTNode::Type::MethodCall: {
                    auto& call = static_cast<const MethodCallNode&>(node);
                    return std::make_unique<MethodCallNode>(
                        call.loc,
                        cloneExpr(*call.target, bound, prefix),
                        call.methodName,
                        cloneArgs(call.args, bound, prefix)
                    );
                }
                case ASTNode::Type::MemberAccess: {
                    auto& access = static_cast<const MemberAccessNode&>(node);
                    auto copy = std::make_unique<MemberAccessNode>(
                        access.loc,
                        cloneExpr(*access.target, bound, prefix),
                        access.memberName
                    );
                    copy->isStaticAccess = access.isStaticAccess;
                    copy->staticClassName = access.staticClassName;
                    copy->isSafe = access.isSafe;
                    copy->memberKind = access.memberKind;
                    return copy;
                }
                case ASTNode::Type::Index: {
                    auto& index = static_cast<const IndexAccessNode&>(node);
                    auto copy = std::make_unique<IndexAccessNode>(
                        index.loc,
                        cloneExpr(*index.target, bound, prefix),
                        cloneExpr(*index.index, bound, prefix)
                    );
                    copy->isSafe = index.isSafe;
                    return copy;
                }
                case ASTNode::Type::New: {
                    auto& newExpr = static_cast<const NewNode&>(node);
                    auto copy = std::make_unique<NewNode>(
                        newExpr.loc,
                        newExpr.className,
                        cloneArgs(newExpr.args, bound, prefix)
                    );
                    copy->receiverName = newExpr.receiverName;
                    if (newExpr.declarativeBlock) {
                        copy->declarativeBlock = std::make_unique<BlockNode>();
                        for (auto& part : newExpr.declarativeBlock->parts)
                            copy->declarativeBlock->addPart(cloneStmt(*part, bound, prefix));
                    }
                    return copy;
                }
                case ASTNode::Type::Array: {
                    auto& array = static_cast<const ArrayNode&>(node);
                    return std::make_unique<ArrayNode>(array.loc, cloneArgs(array.elements, bound, prefix));
                }
                case ASTNode::Type::Lambda: {
                    auto& lambda = static_cast<const LambdaNode&>(node);
                    auto copy = std::make_unique<LambdaNode>(lambda.loc);
                    copy->decl.loc = lambda.decl.loc;
                    copy->decl.name = lambda.decl.name;
                    copy->decl.params = lambda.decl.params;
                    copy->decl.returnTypeExpr = lambda.decl.returnTypeExpr;
                    copy->decl.hasReturnType = lambda.decl.hasReturnType;
                    copy->decl.isConstructor = lambda.decl.isConstructor;
                    copy->decl.isPrivate = lambda.decl.isPrivate;
                    copy->decl.isStatic = lambda.decl.isStatic;
                    copy->decl.body = lambda.decl.body ? cloneStmt(*lambda.decl.body, bound, prefix) : nullptr;
                    return copy;
                }
                case ASTNode::Type::This:
                    return std::make_unique<ThisNode>(static_cast<const ThisNode&>(node).loc);
                case ASTNode::Type::Super:
                    return std::make_unique<SuperNode>(static_cast<const SuperNode&>(node).loc);
                case ASTNode::Type::SuperCall: {
                    auto& call = static_cast<const SuperCallNode&>(node);
                    return std::make_unique<SuperCallNode>(call.loc, cloneArgs(call.args, bound, prefix));
                }
                case ASTNode::Type::InstanceOf: {
                    auto& instanceOf = static_cast<const InstanceOfNode&>(node);
                    return std::make_unique<InstanceOfNode>(
                        instanceOf.loc,
                        cloneExpr(*instanceOf.target, bound, prefix),
                        instanceOf.className
                    );
                }
                default:
                    return nullptr;
            }
        }

        std::unique_ptr<ASTNode> cloneStmt(const ASTNode& node, const std::unordered_set<std::string>& bound, const std::string& prefix) {
            switch (node.getType()) {
                case ASTNode::Type::Block: {
                    auto copy = std::make_unique<BlockNode>();
                    for (auto& part : static_cast<const BlockNode&>(node).parts)
                        copy->addPart(cloneStmt(*part, bound, prefix));
                    return copy;
                }
                case ASTNode::Type::Return: {
                    auto& ret = static_cast<const ReturnNode&>(node);
                    return std::make_unique<ReturnNode>(
                        ret.loc,
                        ret.value ? cloneExpr(*ret.value, bound, prefix) : nullptr
                    );
                }
                case ASTNode::Type::Break:
                    return std::make_unique<BreakNode>(static_cast<const BreakNode&>(node).loc);
                case ASTNode::Type::Continue:
                    return std::make_unique<ContinueNode>(static_cast<const ContinueNode&>(node).loc);
                default:
                    return cloneExpr(static_cast<const ExprNode&>(node), bound, prefix);
            }
        }

        void expandExpr(ExprNode& node, ExpansionContext& ctx);

        void expandBlockParts(std::vector<std::unique_ptr<ASTNode>>& parts, ExpansionContext& ctx);

        void expandStatement(std::unique_ptr<ASTNode>& node, ExpansionContext& ctx) {
            switch (node->getType()) {
                case ASTNode::Type::Block:
                    expandBlockParts(static_cast<BlockNode&>(*node).parts, ctx);
                    return;
                case ASTNode::Type::If: {
                    auto& ifNode = static_cast<IfNode&>(*node);
                    if (ifNode.condition)
                        expandExpr(*ifNode.condition, ctx);
                    if (ifNode.trueBranch)
                        expandStatement(ifNode.trueBranch, ctx);
                    if (ifNode.falseBranch)
                        expandStatement(ifNode.falseBranch, ctx);
                    return;
                }
                case ASTNode::Type::While: {
                    auto& whileNode = static_cast<WhileNode&>(*node);
                    if (whileNode.condition)
                        expandExpr(*whileNode.condition, ctx);
                    if (whileNode.body)
                        expandStatement(whileNode.body, ctx);
                    return;
                }
                case ASTNode::Type::For: {
                    auto& forNode = static_cast<ForNode&>(*node);
                    if (forNode.init)
                        expandExpr(*forNode.init, ctx);
                    if (forNode.condition)
                        expandExpr(*forNode.condition, ctx);
                    if (forNode.step)
                        expandExpr(*forNode.step, ctx);
                    if (forNode.body)
                        expandStatement(forNode.body, ctx);
                    return;
                }
                case ASTNode::Type::ForIn: {
                    auto& forIn = static_cast<ForInNode&>(*node);
                    if (forIn.iterable)
                        expandExpr(*forIn.iterable, ctx);
                    if (forIn.body)
                        expandStatement(forIn.body, ctx);
                    return;
                }
                case ASTNode::Type::Return: {
                    auto& ret = static_cast<ReturnNode&>(*node);
                    if (ret.value)
                        expandExpr(*ret.value, ctx);
                    return;
                }
                case ASTNode::Type::FunctionDef: {
                    auto& func = static_cast<FunctionDefNode&>(*node);
                    if (func.decl.body)
                        expandStatement(func.decl.body, ctx);
                    return;
                }
                case ASTNode::Type::Class: {
                    auto& cls = static_cast<ClassNode&>(*node);
                    for (auto& method : cls.methods) {
                        if (method.body)
                            expandStatement(method.body, ctx);
                    }
                    return;
                }
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                case ASTNode::Type::Trait:
                    return;
                default:
                    expandExpr(static_cast<ExprNode&>(*node), ctx);
            }
        }

        void expandExpr(ExprNode& node, ExpansionContext& ctx) {
            switch (node.getType()) {
                case ASTNode::Type::FuncCall: {
                    auto& call = static_cast<FuncCallNode&>(node);
                    auto it = ctx.components.find(call.name);
                    if (it != ctx.components.end() && !call.isFormReceiverCall) {
                        ctx.diagnostics.addError(call.loc,
                            "Component '" + call.name + "' can only be used as a statement inside a declarative UI block");
                        ctx.ok = false;
                    }
                    for (auto& arg : call.args)
                        expandExpr(*arg, ctx);
                    return;
                }
                case ASTNode::Type::New: {
                    auto& newExpr = static_cast<NewNode&>(node);
                    for (auto& arg : newExpr.args)
                        expandExpr(*arg, ctx);
                    if (newExpr.declarativeBlock)
                        expandBlockParts(newExpr.declarativeBlock->parts, ctx);
                    return;
                }
                case ASTNode::Type::Assignment: {
                    auto& assign = static_cast<AssignmentNode&>(node);
                    if (assign.target)
                        expandExpr(*assign.target, ctx);
                    if (assign.value)
                        expandExpr(*assign.value, ctx);
                    return;
                }
                case ASTNode::Type::CompoundAssign: {
                    auto& assign = static_cast<CompoundAssignNode&>(node);
                    if (assign.target)
                        expandExpr(*assign.target, ctx);
                    if (assign.value)
                        expandExpr(*assign.value, ctx);
                    return;
                }
                case ASTNode::Type::Compare: {
                    auto& compare = static_cast<CompareNode&>(node);
                    expandExpr(*compare.left, ctx);
                    expandExpr(*compare.right, ctx);
                    return;
                }
                case ASTNode::Type::Logical: {
                    auto& logical = static_cast<LogicalNode&>(node);
                    expandExpr(*logical.left, ctx);
                    expandExpr(*logical.right, ctx);
                    return;
                }
                case ASTNode::Type::Arithmetic: {
                    auto& arith = static_cast<ArithmeticNode&>(node);
                    expandExpr(*arith.left, ctx);
                    expandExpr(*arith.right, ctx);
                    return;
                }
                case ASTNode::Type::Unary: {
                    expandExpr(*static_cast<UnaryNode&>(node).operand, ctx);
                    return;
                }
                case ASTNode::Type::Range: {
                    auto& range = static_cast<RangeNode&>(node);
                    expandExpr(*range.start, ctx);
                    expandExpr(*range.end, ctx);
                    return;
                }
                case ASTNode::Type::Coalesce: {
                    auto& coalesce = static_cast<CoalesceNode&>(node);
                    expandExpr(*coalesce.left, ctx);
                    expandExpr(*coalesce.right, ctx);
                    return;
                }
                case ASTNode::Type::If: {
                    auto& ifNode = static_cast<IfNode&>(node);
                    expandExpr(*ifNode.condition, ctx);
                    if (ifNode.trueBranch)
                        expandStatement(ifNode.trueBranch, ctx);
                    if (ifNode.falseBranch)
                        expandStatement(ifNode.falseBranch, ctx);
                    return;
                }
                case ASTNode::Type::While: {
                    auto& whileNode = static_cast<WhileNode&>(node);
                    expandExpr(*whileNode.condition, ctx);
                    if (whileNode.body)
                        expandStatement(whileNode.body, ctx);
                    return;
                }
                case ASTNode::Type::For: {
                    auto& forNode = static_cast<ForNode&>(node);
                    if (forNode.init)
                        expandExpr(*forNode.init, ctx);
                    if (forNode.condition)
                        expandExpr(*forNode.condition, ctx);
                    if (forNode.step)
                        expandExpr(*forNode.step, ctx);
                    if (forNode.body)
                        expandStatement(forNode.body, ctx);
                    return;
                }
                case ASTNode::Type::ForIn: {
                    auto& forIn = static_cast<ForInNode&>(node);
                    expandExpr(*forIn.iterable, ctx);
                    if (forIn.body)
                        expandStatement(forIn.body, ctx);
                    return;
                }
                case ASTNode::Type::Function: {
                    for (auto& arg : static_cast<FunctionNode&>(node).args)
                        expandExpr(*arg, ctx);
                    return;
                }
                case ASTNode::Type::Macro: {
                    for (auto& arg : static_cast<MacroNode&>(node).args)
                        expandExpr(*arg, ctx);
                    return;
                }
                case ASTNode::Type::MethodCall: {
                    auto& call = static_cast<MethodCallNode&>(node);
                    expandExpr(*call.target, ctx);
                    for (auto& arg : call.args)
                        expandExpr(*arg, ctx);
                    return;
                }
                case ASTNode::Type::MemberAccess: {
                    expandExpr(*static_cast<MemberAccessNode&>(node).target, ctx);
                    return;
                }
                case ASTNode::Type::Index: {
                    auto& index = static_cast<IndexAccessNode&>(node);
                    expandExpr(*index.target, ctx);
                    expandExpr(*index.index, ctx);
                    return;
                }
                case ASTNode::Type::Array: {
                    for (auto& element : static_cast<ArrayNode&>(node).elements)
                        expandExpr(*element, ctx);
                    return;
                }
                case ASTNode::Type::Lambda: {
                    auto& lambda = static_cast<LambdaNode&>(node);
                    if (lambda.decl.body)
                        expandStatement(lambda.decl.body, ctx);
                    return;
                }
                case ASTNode::Type::SuperCall: {
                    for (auto& arg : static_cast<SuperCallNode&>(node).args)
                        expandExpr(*arg, ctx);
                    return;
                }
                case ASTNode::Type::InstanceOf: {
                    expandExpr(*static_cast<InstanceOfNode&>(node).target, ctx);
                    return;
                }
                default:
                    return;
            }
        }

        bool expandComponentCall(std::unique_ptr<ASTNode>& node, ExpansionContext& ctx) {
            auto& call = static_cast<FuncCallNode&>(*node);
            auto it = ctx.components.find(call.name);
            if (it == ctx.components.end())
                return false;

            ComponentNode& component = *it->second;

            if (std::find(ctx.stack.begin(), ctx.stack.end(), component.name) != ctx.stack.end()) {
                ctx.diagnostics.addError(call.loc, "Recursive component '" + component.name + "'");
                ctx.ok = false;
                node = std::make_unique<BlockNode>();
                return true;
            }

            if (call.args.size() != component.params.size()) {
                ctx.diagnostics.addError(call.loc,
                    "Component '" + component.name + "' expects " +
                    std::to_string(component.params.size()) + " argument(s), got " +
                    std::to_string(call.args.size()));
                ctx.ok = false;
                node = std::make_unique<BlockNode>();
                return true;
            }

            const std::string prefix = ".c" + std::to_string(ctx.counter++) + ".";
            std::unordered_set<std::string> bound(component.params.begin(), component.params.end());
            if (component.body)
                collectBoundNames(*component.body, bound);

            auto expanded = std::make_unique<BlockNode>();
            for (size_t i = 0; i < component.params.size(); ++i) {
                auto binding = std::make_unique<AssignmentNode>(
                    call.loc,
                    std::make_unique<VariableNode>(call.loc, prefix + component.params[i]),
                    std::move(call.args[i])
                );
                binding->isDeclaration = true;

                expanded->addPart(std::move(binding));
            }

            ctx.stack.push_back(component.name);
            if (component.body) {
                for (auto& part : component.body->parts) {
                    if (auto cloned = cloneStmt(*part, bound, prefix))
                        expanded->addPart(std::move(cloned));
                }
            }

            node = std::move(expanded);
            expandBlockParts(static_cast<BlockNode&>(*node).parts, ctx);
            ctx.stack.pop_back();
            return true;
        }

        void expandBlockParts(std::vector<std::unique_ptr<ASTNode>>& parts, ExpansionContext& ctx) {
            for (auto& part : parts) {
                if (isComponentCall(*part)) {
                    if (expandComponentCall(part, ctx))
                        continue;
                }
                expandStatement(part, ctx);
            }
        }
    }

    bool ComponentExpander::expand(ProgramNode& program, DiagnosticEngine& diagnostics) {
        ExpansionContext ctx(diagnostics);

        std::vector<std::unique_ptr<ASTNode>> kept;
        std::vector<std::unique_ptr<ASTNode>> ownedComponents;
        kept.reserve(program.parts.size());
        for (auto& part : program.parts) {
            if (part->getType() != ASTNode::Type::Component) {
                kept.push_back(std::move(part));
                continue;
            }

            auto& component = static_cast<ComponentNode&>(*part);
            if (ctx.components.contains(component.name)) {
                diagnostics.addError(component.loc, "Duplicate component '" + component.name + "'");
                ctx.ok = false;
                continue;
            }

            ctx.components.emplace(component.name, &component);
            ownedComponents.push_back(std::move(part));
        }
        program.parts = std::move(kept);

        expandBlockParts(program.parts, ctx);
        return ctx.ok;
    }
}
