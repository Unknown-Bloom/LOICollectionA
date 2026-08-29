#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Compiler.h"

namespace LOICollection::frontend::ir {
    namespace {
        std::string qualifiedName(const VariableNode& node) {
            return node.isStaticField ? node.staticClassName + "::" + node.name : node.name;
        }

        std::string qualifiedName(const MemberAccessNode& node) {
            return node.isStaticAccess
                ? node.staticClassName + "::" + node.memberName
                : node.memberName;
        }
    }

    Compiler::Compiler(DiagnosticEngine& diag) : current(std::ref(chunk)), diagnostics(diag) {}

    BytecodeChunk Compiler::compile(ASTNode& root) {
        this->pushScope(false, 0);

        if (root.getType() == ASTNode::Type::Program) {
            auto& program = static_cast<ProgramNode&>(root);

            for (auto& part : program.parts) {
                if (part->getType() == ASTNode::Type::Class) {
                    auto& cls = static_cast<ClassNode&>(*part);
                    this->classNodes.insert_or_assign(cls.name, std::ref(cls));
                }
            }

            for (auto& part : program.parts) {
                switch (part->getType()) {
                    case ASTNode::Type::Class:
                        this->registerClassMeta(static_cast<ClassNode&>(*part));
                        break;
                    case ASTNode::Type::FunctionDef:
                        this->registerFunctionMeta(static_cast<FunctionDefNode&>(*part));
                        break;
                    default:
                        break;
                }
            }

            for (auto node : this->bodyOrder) {
                ASTNode& current = node.get();
                switch (current.getType()) {
                    case ASTNode::Type::Class:
                        this->compileClassBodies(static_cast<ClassNode&>(current));
                        break;
                    case ASTNode::Type::FunctionDef:
                        this->compileFunctionBody(static_cast<FunctionDefNode&>(current));
                        break;
                    default:
                        break;
                }
            }

            size_t lastValuePart = SIZE_MAX;
            for (size_t i = 0; i < program.parts.size(); ++i) {
                auto type = program.parts[i]->getType();
                if (type != ASTNode::Type::Class &&
                    type != ASTNode::Type::FunctionDef &&
                    type != ASTNode::Type::Using) {
                    lastValuePart = i;
                }
            }

            for (size_t i = 0; i < program.parts.size(); ++i) {
                auto type = program.parts[i]->getType();
                if (type == ASTNode::Type::Class ||
                    type == ASTNode::Type::FunctionDef ||
                    type == ASTNode::Type::Using) {
                    continue;
                }

                program.parts[i]->accept(*this);

                if (i != lastValuePart)
                    this->current.get().emit(OpCode::POP);
            }
        } else {
            root.accept(*this);
        }

        this->current.get().emit(OpCode::HALT);

        this->chunk.slotCount = this->closeScope();

        return std::move(chunk);
    }

    void Compiler::visit(ValueNode& node) {
        auto val = node.value;

        int idx = this->addConstant(val);
        switch (val.index()) {
            case 0: current.get().emit(OpCode::PUSH_INT, idx, node.loc); break;
            case 1: current.get().emit(OpCode::PUSH_FLOAT, idx, node.loc); break;
            case 2: current.get().emit(OpCode::PUSH_STR, idx, node.loc); break;
            case 3: current.get().emit(OpCode::PUSH_BOOL, idx, node.loc); break;
            case 7: current.get().emit(OpCode::PUSH_NONE, idx, node.loc); break;
            default:
                this->diagnostics.addError(node.loc, "Unsupported constant value type");
                break;
        }
    }

    void Compiler::visit(VariableNode& node) {
        this->emitLoad(qualifiedName(node), node.loc);

        if (node.type.kind == TypeKind::Optional && !node.preserveOptional)
            this->current.get().emit(OpCode::UNWRAP, 0, node.loc);
    }

    void Compiler::visit(AssignmentNode& node) {
        if (node.value) {
            this->compileValue(*node.value, node.loc);
        } else {
            int emptyIdx = this->addConstant(std::string(""));
            this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
        }

        this->current.get().emit(OpCode::DUP, 0, node.loc);

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                this->emitStore(qualifiedName(var), node.loc);
                break;
            }
            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);
                if (member.isStaticAccess) {
                    this->emitStore(qualifiedName(member), node.loc);
                    break;
                }

                this->compileValue(*member.target, node.loc);

                int idx = this->addConstant(member.memberName);
                this->current.get().emit(OpCode::STORE_FIELD, idx, node.loc);
                break;
            }
            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);
                indexNode.target->accept(*this);
                indexNode.index->accept(*this);
                this->current.get().emit(OpCode::STORE_INDEX, 0, node.loc);
                break;
            }
            default:
                this->diagnostics.addError(node.loc, "Invalid assignment target");
                break;
        }
    }

    void Compiler::visit(CompoundAssignNode& node) {
        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                const std::string name = qualifiedName(var);

                this->emitLoad(name, node.loc);
                if (var.type.kind == TypeKind::Optional && !var.preserveOptional)
                    this->current.get().emit(OpCode::UNWRAP, 0, node.loc);

                this->compileValue(*node.value, node.loc);
                this->emitArithmeticOp(node.op, node.loc);

                this->current.get().emit(OpCode::DUP, 0, node.loc);
                this->emitStore(name, node.loc);
                break;
            }
            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);
                if (member.isStaticAccess) {
                    const std::string name = qualifiedName(member);

                    this->emitLoad(name, node.loc);
                    if (member.type.kind == TypeKind::Optional && !member.preserveOptional)
                        this->current.get().emit(OpCode::UNWRAP, 0, node.loc);

                    this->compileValue(*node.value, node.loc);
                    this->emitArithmeticOp(node.op, node.loc);

                    this->current.get().emit(OpCode::DUP, 0, node.loc);
                    this->emitStore(name, node.loc);
                    break;
                }

                this->compileValue(*member.target, node.loc);
                this->current.get().emit(OpCode::DUP, 0, node.loc);

                int idx = this->addConstant(member.memberName);
                this->current.get().emit(OpCode::LOAD_FIELD, idx, node.loc);
                if (member.type.kind == TypeKind::Optional && !member.preserveOptional)
                    this->current.get().emit(OpCode::UNWRAP, 0, node.loc);

                this->compileValue(*node.value, node.loc);
                this->emitArithmeticOp(node.op, node.loc);

                this->current.get().emit(OpCode::DUP, 0, node.loc);
                this->current.get().emit(OpCode::ROT3, 0, node.loc);
                this->current.get().emit(OpCode::STORE_FIELD, idx, node.loc);
                break;
            }
            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);

                this->compileValue(*indexNode.target, node.loc);
                this->compileValue(*indexNode.index, node.loc);

                this->current.get().emit(OpCode::DUP2, 0, node.loc);
                this->current.get().emit(OpCode::LOAD_INDEX, 0, node.loc);

                this->compileValue(*node.value, node.loc);
                this->emitArithmeticOp(node.op, node.loc);

                this->current.get().emit(OpCode::DUP, 0, node.loc);
                this->current.get().emit(OpCode::SWAP2, 0, node.loc);
                this->current.get().emit(OpCode::STORE_INDEX, 0, node.loc);
                break;
            }
            default:
                this->diagnostics.addError(node.loc, "Invalid compound assignment target");
                break;
        }
    }

    void Compiler::visit(CoalesceNode& node) {
        this->compileValue(*node.left, node.loc);

        this->current.get().emit(OpCode::DUP, 0, node.loc);
        this->current.get().emit(OpCode::IS_NONE, 0, node.loc);

        size_t jmpSkipIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->current.get().emit(OpCode::POP, 0, node.loc);
        this->compileValue(*node.right, node.loc);

        size_t jmpEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

        int skipTarget = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpSkipIdx, skipTarget - static_cast<int>(jmpSkipIdx) - 1);

        int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);
    }

    void Compiler::visit(RangeNode& node) {
        this->diagnostics.addError(node.loc, "Range expression is only allowed as a for-in iterable");
    }

    void Compiler::visit(ForInNode& node) {
        size_t uid = this->forInCounter++;

        if (node.iterable->getType() == ASTNode::Type::Range)
            this->compileForInRange(node, uid);
        else
            this->compileForInArray(node, uid);
    }

    void Compiler::compileForInArray(ForInNode& node, size_t uid) {
        const int seqSlot = this->declareSlot("__forin_seq_" + std::to_string(uid));
        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        this->compileValue(*node.iterable, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, seqSlot, node.loc);

        int zeroIdx = this->addConstant(0);
        this->current.get().emit(OpCode::PUSH_INT, zeroIdx, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        size_t loopStart = this->current.get().currentIP();

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_LEN, 0, node.loc);
        this->current.get().emit(OpCode::CMP_LT, 0, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_INDEX, 0, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, elemSlot, node.loc);

        if (indexSlot >= 0) {
            this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
            this->current.get().emit(OpCode::STORE_SLOT, indexSlot, node.loc);
        }

        int oneIdx = this->addConstant(1);
        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::PUSH_INT, oneIdx, node.loc);
        this->current.get().emit(OpCode::ADD, 0, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        node.body->accept(*this);
        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

    void Compiler::compileForInRange(ForInNode& node, size_t uid) {
        auto& range = static_cast<RangeNode&>(*node.iterable);

        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int endSlot = this->declareSlot("__forin_end_" + std::to_string(uid));
        const int dirSlot = this->declareSlot("__forin_dir_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        this->compileValue(*range.start, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        this->compileValue(*range.end, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, endSlot, node.loc);

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, endSlot, node.loc);
        this->current.get().emit(OpCode::CMP_LE, 0, node.loc);

        size_t jmpDescIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        int ascIdx = this->addConstant(1);
        this->current.get().emit(OpCode::PUSH_INT, ascIdx, node.loc);

        size_t jmpDirEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

        int descStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDescIdx, descStart - static_cast<int>(jmpDescIdx) - 1);

        int descIdx = this->addConstant(-1);
        this->current.get().emit(OpCode::PUSH_INT, descIdx, node.loc);

        int dirEndPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDirEndIdx, dirEndPos - static_cast<int>(jmpDirEndIdx) - 1);

        this->current.get().emit(OpCode::STORE_SLOT, dirSlot, node.loc);

        size_t loopStart = this->current.get().currentIP();

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, endSlot, node.loc);
        this->current.get().emit(OpCode::SUB, 0, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, dirSlot, node.loc);
        this->current.get().emit(OpCode::MUL, 0, node.loc);
        int zeroIdx = this->addConstant(0);
        this->current.get().emit(OpCode::PUSH_INT, zeroIdx, node.loc);
        this->current.get().emit(OpCode::CMP_LT, 0, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, elemSlot, node.loc);

        if (indexSlot >= 0) {
            this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
            this->current.get().emit(OpCode::STORE_SLOT, indexSlot, node.loc);
        }

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, dirSlot, node.loc);
        this->current.get().emit(OpCode::ADD, 0, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        node.body->accept(*this);
        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

    void Compiler::emitArithmeticOp(const std::string& op, const SourceLocation& loc) {
        if (op == "+") this->current.get().emit(OpCode::ADD, 0, loc);
        else if (op == "-") this->current.get().emit(OpCode::SUB, 0, loc);
        else if (op == "*") this->current.get().emit(OpCode::MUL, 0, loc);
        else if (op == "/") this->current.get().emit(OpCode::DIV, 0, loc);
        else if (op == "%") this->current.get().emit(OpCode::MOD, 0, loc);
        else
            this->diagnostics.addError(loc, "Unknown compound assignment op: " + op);
    }

    void Compiler::visit(IfNode& node) {
        this->compileValue(*node.condition, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        node.trueBranch->accept(*this);

        size_t jmpEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

        int falseStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpFalseIdx, falseStart - static_cast<int>(jmpFalseIdx) - 1);

        if (node.falseBranch) {
            node.falseBranch->accept(*this);
        } else {
            int emptyIdx = this->addConstant(std::string(""));
            this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
        }

        int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);
    }

    void Compiler::visit(WhileNode& node) {
        size_t loopStart = this->current.get().currentIP();

        this->compileValue(*node.condition, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        node.body->accept(*this);

        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

    void Compiler::visit(ForNode& node) {
        if (node.init) {
            this->compileValue(*node.init, node.loc);
            this->current.get().emit(OpCode::POP, 0, node.loc);
        }

        size_t loopStart = this->current.get().currentIP();

        size_t jmpFalseIdx = static_cast<size_t>(-1);
        if (node.condition) {
            this->compileValue(*node.condition, node.loc);
            jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);
        }

        this->loopStack.push_back(LoopContext{});

        node.body->accept(*this);

        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t continuePos = this->current.get().currentIP();
        this->loopStack.back().continueTarget = continuePos;

        if (node.step) {
            this->compileValue(*node.step, node.loc);
            this->current.get().emit(OpCode::POP, 0, node.loc);
        }

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        if (jmpFalseIdx != static_cast<size_t>(-1))
            this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(continuePos) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

    void Compiler::visit(BreakNode& node) {
        if (this->loopStack.empty()) {
            this->diagnostics.addError(node.loc, "'break' can only be used inside a loop");
            return;
        }

        size_t idx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->loopStack.back().breakJumps.push_back(idx);
    }

    void Compiler::visit(ContinueNode& node) {
        if (this->loopStack.empty()) {
            this->diagnostics.addError(node.loc, "'continue' can only be used inside a loop");
            return;
        }

        size_t idx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->loopStack.back().continueJumps.push_back(idx);
    }

    void Compiler::visit(CompareNode& node) {
        this->compileValue(*node.left, node.loc);
        this->compileValue(*node.right, node.loc);

        if (node.op == "==") this->current.get().emit(OpCode::CMP_EQ, 0, node.loc);
        else if (node.op == "!=") this->current.get().emit(OpCode::CMP_NE, 0, node.loc);
        else if (node.op == ">") this->current.get().emit(OpCode::CMP_GT, 0, node.loc);
        else if (node.op == "<") this->current.get().emit(OpCode::CMP_LT, 0, node.loc);
        else if (node.op == ">=") this->current.get().emit(OpCode::CMP_GE, 0, node.loc);
        else if (node.op == "<=") this->current.get().emit(OpCode::CMP_LE, 0, node.loc);
        else
            this->diagnostics.addError(node.loc, "Unknown compare op: " + node.op);
    }

    void Compiler::visit(LogicalNode& node) {
        bool isAnd = (node.op == "&&");

        if (node.left->getType() == ASTNode::Type::Value) {
            auto& leftVal = static_cast<const ValueNode&>(*node.left).value;
            if (std::holds_alternative<bool>(leftVal)) {
                bool leftBool = std::get<bool>(leftVal);
                if (isAnd) {
                    if (leftBool) {
                        int idxTrue = this->addConstant(true);
                        this->current.get().emit(OpCode::PUSH_BOOL, idxTrue, node.loc);

                        this->compileValue(*node.right, node.loc);

                        this->current.get().emit(OpCode::LOGIC_AND, 0, node.loc);
                    } else {
                        int idxFalse = this->addConstant(false);
                        this->current.get().emit(OpCode::PUSH_BOOL, idxFalse, node.loc);
                    }
                } else {
                    if (leftBool) {
                        int idxTrue = this->addConstant(true);
                        this->current.get().emit(OpCode::PUSH_BOOL, idxTrue, node.loc);
                    } else {
                        int idxFalse = this->addConstant(false);
                        this->current.get().emit(OpCode::PUSH_BOOL, idxFalse, node.loc);

                        this->compileValue(*node.right, node.loc);
                        
                        this->current.get().emit(OpCode::LOGIC_OR, 0, node.loc);
                    }
                }
                return;
            }
        }

        this->compileValue(*node.left, node.loc);
        this->current.get().emit(OpCode::DUP, 0, node.loc);

        size_t jumpToShort = this->current.get().emit(isAnd ? OpCode::JMP_IF_FALSE : OpCode::JMP_IF_TRUE, 0, node.loc);

        this->compileValue(*node.right, node.loc);
        this->current.get().emit(isAnd ? OpCode::LOGIC_AND : OpCode::LOGIC_OR, 0, node.loc);

        size_t jumpToEnd = this->current.get().emit(OpCode::JMP, 0, node.loc);

        int shortStart = static_cast<int>(this->current.get().currentIP());

        int shortIdx = this->addConstant(isAnd ? false : true);
        this->current.get().emit(OpCode::POP, 0, node.loc);
        this->current.get().emit(OpCode::PUSH_BOOL, shortIdx, node.loc);

        this->current.get().patchJump(jumpToShort, shortStart - static_cast<int>(jumpToShort) - 1);

        int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jumpToEnd, endPos - static_cast<int>(jumpToEnd) - 1);
    }

    void Compiler::visit(FunctionNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        int argCount = static_cast<int>(node.args.size());
        int metaIdx = this->addFunction(node.namespaces + "::" + node.name, argCount);
        this->current.get().emit(OpCode::CALL, metaIdx, node.loc);
    }

    void Compiler::visit(MacroNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        int argCount = static_cast<int>(node.args.size());
        int metaIdx = addMacro(node.name, argCount);
        this->current.get().emit(OpCode::CALL_MACRO, metaIdx, node.loc);
    }

    void Compiler::visit(ArithmeticNode& node) {
        this->compileValue(*node.left, node.loc);
        this->compileValue(*node.right, node.loc);

        if (node.op == "+") this->current.get().emit(OpCode::ADD, 0, node.loc);
        else if (node.op == "-") this->current.get().emit(OpCode::SUB, 0, node.loc);
        else if (node.op == "*") this->current.get().emit(OpCode::MUL, 0, node.loc);
        else if (node.op == "/") this->current.get().emit(OpCode::DIV, 0, node.loc);
        else if (node.op == "%") this->current.get().emit(OpCode::MOD, 0, node.loc);
        else if (node.op == "^") this->current.get().emit(OpCode::POW, 0, node.loc);
        else
            this->diagnostics.addError(node.loc, "Unknown arithmetic op: " + node.op);
    }

    void Compiler::visit(UnaryNode& node) {
        this->compileValue(*node.operand, node.loc);

        if (node.op == "-") this->current.get().emit(OpCode::NEG, 0, node.loc);
        else if (node.op == "!") this->current.get().emit(OpCode::NOT, 0, node.loc);
        else if (node.op == "+") {}
        else
            this->diagnostics.addError(node.loc, "Unknown unary op: " + node.op);
    }

    void Compiler::compileSequence(SequenceNode& node) {
        if (node.parts.empty()) {
            int idx = this->addConstant(std::string(""));
            this->current.get().emit(OpCode::PUSH_STR, idx);

            return;
        }

        for (size_t i = 0; i < node.parts.size(); ++i) {
            node.parts[i]->accept(*this);

            bool producesValue = node.parts[i]->getType() != ASTNode::Type::Class
                && node.parts[i]->getType() != ASTNode::Type::Return
                && node.parts[i]->getType() != ASTNode::Type::Using
                && node.parts[i]->getType() != ASTNode::Type::Import
                && node.parts[i]->getType() != ASTNode::Type::Component;

            if (i != node.parts.size() - 1 && producesValue)
                this->current.get().emit(OpCode::POP);
        }
    }

    void Compiler::visit(UsingNode&) {}

    void Compiler::visit(ImportNode&) {}

    void Compiler::visit(ComponentNode&) {}

    void Compiler::visit(ProgramNode& node) {
        compileSequence(node);
    }

    void Compiler::visit(BlockNode& node) {
        compileSequence(node);
    }

    void Compiler::visit(ClassNode& node) {
        this->registerClassMeta(node);
        this->compileClassBodies(node);
    }

    void Compiler::registerClassMeta(ClassNode& node) {
        if (classIndices.contains(node.name))
            return;

        int baseIdx = -1;
        if (!node.baseClassName.empty()) {
            if (this->registeringClasses.contains(node.name)) {
                this->diagnostics.addError(node.loc,
                    "Circular inheritance involving class '" + node.name + "'");
                return;
            }

            this->registeringClasses.insert(node.name);

            auto baseIt = this->classNodes.find(node.baseClassName);
            if (baseIt == this->classNodes.end()) {
                this->diagnostics.addError(node.loc, "Unknown base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            if (!this->classIndices.contains(node.baseClassName))
                this->registerClassMeta(baseIt->second.get());

            auto registeredIt = this->classIndices.find(node.baseClassName);
            if (registeredIt == this->classIndices.end()) {
                this->diagnostics.addError(node.loc, "Failed to register base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            baseIdx = registeredIt->second;
            this->registeringClasses.erase(node.name);
        }

        int classIdx = static_cast<int>(this->chunk.classes.size());
        this->classIndices[node.name] = classIdx;
        this->registeredClasses.insert(classIdx);

        ir::ClassMeta meta;
        meta.name = node.name;
        meta.baseClassIndex = baseIdx;

        if (baseIdx >= 0) {
            const auto& base = this->chunk.classes[baseIdx];
            meta.fieldNames = base.fieldNames;
            meta.defaults = base.defaults;
            meta.hasDefault = base.hasDefault;
            meta.constructorIndex = base.constructorIndex;
            meta.methods = base.methods;
            meta.methodSignatures = base.methodSignatures;
            meta.staticMethods = base.staticMethods;
            meta.staticMethodSignatures = base.staticMethodSignatures;
            meta.staticFieldNames = base.staticFieldNames;
            meta.staticDefaults = base.staticDefaults;
            meta.staticHasDefault = base.staticHasDefault;
            meta.ancestorIndices.push_back(baseIdx);
            meta.ancestorIndices.insert(meta.ancestorIndices.end(), base.ancestorIndices.begin(), base.ancestorIndices.end());
        }

        for (const auto& member : node.members) {
            auto& names = member.isStatic ? meta.staticFieldNames : meta.fieldNames;
            auto& defaults = member.isStatic ? meta.staticDefaults : meta.defaults;
            auto& hasDefault = member.isStatic ? meta.staticHasDefault : meta.hasDefault;

            auto fieldIt = std::ranges::find(names, member.name);
            if (fieldIt == names.end()) {
                names.push_back(member.name);
                hasDefault.push_back(member.hasDefault);
                defaults.emplace_back();
                fieldIt = std::ranges::find(names, member.name);
            } else {
                auto fieldIdx = static_cast<size_t>(std::distance(names.begin(), fieldIt));
                hasDefault[fieldIdx] = member.hasDefault;
            }

            auto fieldIdx = static_cast<size_t>(std::distance(names.begin(), fieldIt));
            if (member.hasDefault) {
                if (member.defaultExpr) {
                    auto value = this->constantValue(*member.defaultExpr);
                    if (value.has_value()) {
                        defaults[fieldIdx] = *value;
                        continue;
                    }
                }

                this->diagnostics.addError(node.loc,
                    "Member default value of '" + member.name + "' must be a constant literal");
                defaults[fieldIdx] = ValueNode::ValueType{};
            } else {
                defaults[fieldIdx] = ValueNode::ValueType{};
            }
        }

        for (auto & method : node.methods) {
            int methodIdx = static_cast<int>(methodCount++);

            if (method.isConstructor)
                meta.constructorIndex = methodIdx;
            else {
                std::string signature = this->methodSignature(method);
                auto& signatures = method.isStatic ? meta.staticMethodSignatures : meta.methodSignatures;
                auto& methods = method.isStatic ? meta.staticMethods : meta.methods;

                auto sigIt = std::ranges::find(signatures, signature);
                if (sigIt != signatures.end()) {
                    auto ordinal = static_cast<size_t>(std::distance(signatures.begin(), sigIt));
                    methods[ordinal] = methodIdx;
                } else {
                    signatures.push_back(signature);
                    methods.push_back(methodIdx);
                }
            }
        }

        this->classMethodIndices[node.name] = meta.methods;
        this->classStaticMethodIndices[node.name] = meta.staticMethods;
        this->chunk.classes.push_back(std::move(meta));
        this->bodyOrder.emplace_back(std::ref(node));
    }

    void Compiler::compileClassBodies(ClassNode& node) {
        auto classIt = this->classIndices.find(node.name);
        if (classIt == this->classIndices.end() || !this->registeredClasses.contains(classIt->second))
            return;

        int classIdx = classIt->second;

        for (const auto& method : node.methods) {
            ir::MethodMeta mm;
            mm.name = method.name;
            mm.classIndex = classIdx;
            mm.argCount = static_cast<int>(method.params.size());

            int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
            auto body = std::make_unique<BytecodeChunk>();
            BytecodeChunk& bodyChunk = *body;
            this->chunk.methodBodies.push_back(std::move(body));

            std::reference_wrapper<BytecodeChunk> saved = this->current;
            this->current = std::ref(bodyChunk);

            this->pushScope(true, 0);
            for (const auto& param : method.params)
                this->declareSlot(param.name);

            if (method.isConstructor && !node.baseClassName.empty() && !method.hasSuperCall) {
                int ctorIdx = -1;
                auto baseIt = this->classIndices.find(node.baseClassName);
                int walkIdx = baseIt == this->classIndices.end() ? -1 : baseIt->second;
                while (walkIdx >= 0) {
                    const auto& walkCls = this->chunk.classes[walkIdx];
                    if (walkCls.constructorIndex != -1) {
                        ctorIdx = walkCls.constructorIndex;
                        break;
                    }
                    walkIdx = walkCls.baseClassIndex;
                }

                int argCount = ctorIdx >= 0 ? this->chunk.methods[ctorIdx].argCount : 0;
                int superIdx = this->addSuperCall(ctorIdx, argCount);
                this->current.get().emit(OpCode::LOAD_THIS, 0, node.loc);
                this->current.get().emit(OpCode::CALL_SUPER_CTOR, superIdx, node.loc);
                this->current.get().emit(OpCode::POP, 0, node.loc);
            }

            if (method.body)
                method.body->accept(*this);

            this->current.get().emit(OpCode::POP);

            int emptyIdx = this->addConstant(std::string(""));
            this->current.get().emit(OpCode::PUSH_STR, emptyIdx);
            this->current.get().emit(OpCode::RETURN);

            this->current = saved;
            bodyChunk.slotCount = this->closeScope();

            mm.bodyIndex = bodyIdx;

            this->chunk.methods.push_back(std::move(mm));
        }
    }

    void Compiler::visit(ReturnNode& node) {
        if (node.value) {
            node.value->accept(*this);
        } else {
            int idx = this->addConstant(std::string(""));
            this->current.get().emit(OpCode::PUSH_STR, idx);
        }

        this->current.get().emit(OpCode::RETURN);
    }

    void Compiler::visit(NewNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        auto it = this->classIndices.find(node.className);
        if (it == this->classIndices.end()) {
            if (ClassCall::getInstance().isRegistered(node.className)) {
                int argCount = static_cast<int>(node.args.size());
                int metaIdx = this->addNativeCall(node.className, "", argCount);

                this->current.get().emit(OpCode::NEW_NATIVE, metaIdx, node.loc);
                if (node.declarativeBlock)
                    this->compileDeclarativeBlock(*node.declarativeBlock, node.receiverName);
                return;
            }

            this->diagnostics.addError(node.loc, "Unknown class: " + node.className);
            return;
        }

        this->current.get().emit(OpCode::NEW, it->second, node.loc);

        if (node.declarativeBlock)
            this->compileDeclarativeBlock(*node.declarativeBlock, node.receiverName);
    }

    void Compiler::compileDeclarativeBlock(BlockNode& block, const std::string& receiverName) {
        std::string receiver = receiverName;
        int receiverSlot = -1;

        if (receiver.empty()) {
            receiver = ".form" + std::to_string(this->declarativeCounter++);
            receiverSlot = this->declareSlot(receiver);
        }

        if (receiverSlot >= 0)
            this->current.get().emit(OpCode::STORE_SLOT, receiverSlot);
        else
            this->emitStore(receiver, {});

        for (auto& part : block.parts)
            this->desugarDeclarativeStatements(part, receiver);

        for (auto& part : block.parts) {
            part->accept(*this);

            bool producesValue = part->getType() != ASTNode::Type::Class
                && part->getType() != ASTNode::Type::Return
                && part->getType() != ASTNode::Type::Using;

            if (producesValue)
                this->current.get().emit(OpCode::POP);
        }

        if (receiverSlot >= 0)
            this->current.get().emit(OpCode::LOAD_SLOT, receiverSlot);
        else
            this->emitLoad(receiver, {});
    }

    void Compiler::desugarDeclarativeStatements(std::unique_ptr<ASTNode>& node, const std::string& receiver) {
        switch (node->getType()) {
            case ASTNode::Type::Block: {
                auto& block = static_cast<BlockNode&>(*node);
                for (auto& part : block.parts)
                    this->desugarDeclarativeStatements(part, receiver);
                return;
            }
            case ASTNode::Type::If: {
                auto& ifNode = static_cast<IfNode&>(*node);
                if (ifNode.trueBranch)
                    this->desugarDeclarativeStatements(ifNode.trueBranch, receiver);
                if (ifNode.falseBranch)
                    this->desugarDeclarativeStatements(ifNode.falseBranch, receiver);
                return;
            }
            case ASTNode::Type::While: {
                auto& whileNode = static_cast<WhileNode&>(*node);
                if (whileNode.body)
                    this->desugarDeclarativeStatements(whileNode.body, receiver);
                return;
            }
            case ASTNode::Type::For: {
                auto& forNode = static_cast<ForNode&>(*node);
                if (forNode.body)
                    this->desugarDeclarativeStatements(forNode.body, receiver);
                return;
            }
            case ASTNode::Type::ForIn: {
                auto& forIn = static_cast<ForInNode&>(*node);
                if (forIn.body)
                    this->desugarDeclarativeStatements(forIn.body, receiver);
                return;
            }
            case ASTNode::Type::FuncCall: {
                auto& call = static_cast<FuncCallNode&>(*node);
                if (!call.isFormReceiverCall || call.receiverClassName.empty())
                    return;

                auto methodCall = std::make_unique<MethodCallNode>(
                    call.loc,
                    std::make_unique<VariableNode>(call.loc, receiver),
                    call.name,
                    std::move(call.args)
                );
                methodCall->className = call.receiverClassName;
                methodCall->methodOrdinal = -1;

                node = std::move(methodCall);
                return;
            }
            default:
                return;
        }
    }

    void Compiler::visit(MemberAccessNode& node) {
        if (node.isStaticAccess) {
            this->emitLoad(qualifiedName(node), node.loc);

            if (node.type.kind == TypeKind::Optional && !node.preserveOptional)
                this->current.get().emit(OpCode::UNWRAP, 0, node.loc);
            return;
        }

        if (node.isSafe) {
            this->compileValue(*node.target, node.loc);

            this->current.get().emit(OpCode::DUP, 0, node.loc);
            this->current.get().emit(OpCode::IS_NONE, 0, node.loc);

            size_t jmpSkipIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

            this->current.get().emit(OpCode::POP, 0, node.loc);
            int noneIdx = this->addConstant(std::monostate{});
            this->current.get().emit(OpCode::PUSH_NONE, noneIdx, node.loc);

            size_t jmpEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

            int skipTarget = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpSkipIdx, skipTarget - static_cast<int>(jmpSkipIdx) - 1);

            switch (node.memberKind) {
                case MemberAccessNode::MemberKind::TypeOf:
                    this->current.get().emit(OpCode::TYPE_OF, 0, node.loc);
                    break;
                case MemberAccessNode::MemberKind::HasValue:
                    this->current.get().emit(OpCode::HAS_VALUE, 0, node.loc);
                    break;
                case MemberAccessNode::MemberKind::Value:
                    // The target is known to be non-None here: unwrapping is an identity.
                    this->current.get().emit(OpCode::UNWRAP, 0, node.loc);
                    break;
                default: {
                    int idx = this->addConstant(node.memberName);
                    this->current.get().emit(OpCode::LOAD_FIELD, idx, node.loc);
                    break;
                }
            }

            int endPos = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);
            return;
        }

        switch (node.memberKind) {
            case MemberAccessNode::MemberKind::TypeOf:
                node.target->accept(*this);
                this->current.get().emit(OpCode::TYPE_OF, 0, node.loc);
                return;
            case MemberAccessNode::MemberKind::Value:
                node.target->accept(*this);
                if (node.target->type.kind == TypeKind::Optional)
                    this->current.get().emit(OpCode::UNWRAP, 0, node.loc);
                return;
            case MemberAccessNode::MemberKind::HasValue:
                node.target->accept(*this);
                this->current.get().emit(OpCode::HAS_VALUE, 0, node.loc);
                return;
            case MemberAccessNode::MemberKind::Normal:
                break;
        }

        this->compileValue(*node.target, node.loc);

        int idx = this->addConstant(node.memberName);
        this->current.get().emit(OpCode::LOAD_FIELD, idx, node.loc);

        if (node.type.kind == TypeKind::Optional && !node.preserveOptional)
            this->current.get().emit(OpCode::UNWRAP, 0, node.loc);
    }

    void Compiler::visit(ArrayNode& node) {
        for (auto& element : node.elements)
            this->compileValue(*element, node.loc);

        this->current.get().emit(OpCode::MAKE_ARRAY, static_cast<int>(node.elements.size()), node.loc);
    }

    void Compiler::visit(IndexAccessNode& node) {
        this->compileValue(*node.target, node.loc);

        if (node.isSafe) {
            this->current.get().emit(OpCode::DUP, 0, node.loc);
            this->current.get().emit(OpCode::IS_NONE, 0, node.loc);

            size_t jmpSkipIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

            this->current.get().emit(OpCode::POP, 0, node.loc);
            int noneIdx = this->addConstant(std::monostate{});
            this->current.get().emit(OpCode::PUSH_NONE, noneIdx, node.loc);

            size_t jmpEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

            int skipTarget = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpSkipIdx, skipTarget - static_cast<int>(jmpSkipIdx) - 1);

            this->compileValue(*node.index, node.loc);
            this->current.get().emit(OpCode::LOAD_INDEX, 0, node.loc);

            int endPos = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);
            return;
        }

        this->compileValue(*node.index, node.loc);
        this->current.get().emit(OpCode::LOAD_INDEX, 0, node.loc);
    }

    void Compiler::visit(MethodCallNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        if (node.isStaticCall) {
            if (ClassCall::getInstance().isRegistered(node.staticClassName)) {
                int argCount = static_cast<int>(node.args.size());
                int metaIdx = this->addNativeCall(node.staticClassName, node.methodName, argCount, true);

                this->current.get().emit(OpCode::CALL_NATIVE_METHOD, metaIdx, node.loc);
                return;
            }

            auto it = this->classStaticMethodIndices.find(node.staticClassName);
            if (it != this->classStaticMethodIndices.end() && node.methodOrdinal >= 0 &&
                static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
                this->current.get().emit(OpCode::CALL_FUNC, it->second[node.methodOrdinal], node.loc);
            } else {
                this->diagnostics.addError(node.loc, "Unresolved static method call: " + node.methodName);
            }
            return;
        }

        this->compileValue(*node.target, node.loc);

        auto it = classMethodIndices.find(node.className);
        if (it != classMethodIndices.end() && node.methodOrdinal >= 0 &&
            static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
            int argCount = static_cast<int>(node.args.size());

            if (node.target->getType() == ASTNode::Type::Super) {
                this->current.get().emit(OpCode::CALL_METHOD, it->second[node.methodOrdinal], node.loc);
            } else {
                int classIdx = this->classIndices[node.className];
                int metaIdx = this->addVirtualCall(classIdx, node.methodOrdinal, argCount);
                this->current.get().emit(OpCode::CALL_METHOD_VIRTUAL, metaIdx, node.loc);
            }
            return;
        }

        if (ClassCall::getInstance().isRegistered(node.className)) {
            int argCount = static_cast<int>(node.args.size());
            int metaIdx = this->addNativeCall(node.className, node.methodName, argCount);

            this->current.get().emit(OpCode::CALL_NATIVE_METHOD, metaIdx, node.loc);
            return;
        }

        this->diagnostics.addError(node.loc, "Unresolved method call: " + node.methodName);
    }

    void Compiler::visit(ThisNode& node) {
        this->current.get().emit(OpCode::LOAD_THIS, 0, node.loc);
    }

    void Compiler::visit(SuperNode& node) {
        this->current.get().emit(OpCode::LOAD_THIS, 0, node.loc);
    }

    void Compiler::visit(SuperCallNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        this->current.get().emit(OpCode::LOAD_THIS, 0, node.loc);

        int constructorIndex = -1;
        if (node.constructorIndex >= 0) {
            auto classIt = this->classIndices.find(node.className);
            if (classIt != this->classIndices.end())
                constructorIndex = this->chunk.classes[classIt->second].constructorIndex;
        }

        int argCount = static_cast<int>(node.args.size());
        int metaIdx = this->addSuperCall(constructorIndex, argCount);
        this->current.get().emit(OpCode::CALL_SUPER_CTOR, metaIdx, node.loc);
    }

    void Compiler::visit(InstanceOfNode& node) {
        this->compileValue(*node.target, node.loc);

        int nameIdx = this->addConstant(node.className);
        this->current.get().emit(OpCode::INSTANCEOF, nameIdx, node.loc);
    }

    void Compiler::visit(FunctionDefNode& node) {
        this->registerFunctionMeta(node);
        this->compileFunctionBody(node);
    }

    void Compiler::registerFunctionMeta(FunctionDefNode& node) {
        int funcIdx = static_cast<int>(methodCount++);
        this->functionIndices[node.name].push_back(funcIdx);
        this->bodyOrder.emplace_back(std::ref(node));
    }

    void Compiler::compileFunctionBody(FunctionDefNode& node) {
        ir::MethodMeta mm;
        mm.name = node.name;
        mm.classIndex = -1;
        mm.argCount = static_cast<int>(node.decl.params.size());

        int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
        auto body = std::make_unique<BytecodeChunk>();
        BytecodeChunk& bodyChunk = *body;
        this->chunk.methodBodies.push_back(std::move(body));

        std::reference_wrapper<BytecodeChunk> saved = this->current;
        this->current = std::ref(bodyChunk);

        this->pushScope(false, 0);
        for (const auto& param : node.decl.params)
            this->declareSlot(param.name);

        if (node.decl.body)
            node.decl.body->accept(*this);

        this->current.get().emit(OpCode::POP);

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx);
        this->current.get().emit(OpCode::RETURN);

        this->current = saved;
        bodyChunk.slotCount = this->closeScope();

        mm.bodyIndex = bodyIdx;

        this->chunk.methods.push_back(std::move(mm));
    }

    void Compiler::visit(FuncCallNode& node) {
        for (auto& arg : node.args)
            this->compileValue(*arg, node.loc);

        if (node.isStaticCall) {
            if (ClassCall::getInstance().isRegistered(node.staticClassName)) {
                int argCount = static_cast<int>(node.args.size());
                int metaIdx = this->addNativeCall(node.staticClassName, node.name, argCount, true);

                this->current.get().emit(OpCode::CALL_NATIVE_METHOD, metaIdx, node.loc);
                return;
            }

            auto it = this->classStaticMethodIndices.find(node.staticClassName);
            if (it != this->classStaticMethodIndices.end() && node.methodOrdinal >= 0 &&
                static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
                this->current.get().emit(OpCode::CALL_FUNC, it->second[node.methodOrdinal], node.loc);
            } else {
                this->diagnostics.addError(node.loc, "Unresolved static method call: " + node.name);
            }
            return;
        }

        if (node.isCallable) {
            this->emitLoad(node.resolvedName, node.loc);

            int argCount = static_cast<int>(node.args.size());
            this->current.get().emit(OpCode::CALL_LAMBDA, argCount, node.loc);
            return;
        }

        auto it = this->functionIndices.find(node.resolvedName);
        if (it == this->functionIndices.end() || node.functionOrdinal < 0 ||
            static_cast<size_t>(node.functionOrdinal) >= it->second.size()) {
            this->diagnostics.addError(node.loc, "Unresolved function call: " + node.name);
            return;
        }

        this->current.get().emit(OpCode::CALL_FUNC, it->second[node.functionOrdinal], node.loc);
    }

    void Compiler::visit(LambdaNode& node) {
        int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
        auto body = std::make_unique<BytecodeChunk>();
        BytecodeChunk& bodyChunk = *body;
        this->chunk.methodBodies.push_back(std::move(body));

        std::reference_wrapper<BytecodeChunk> saved = this->current;
        this->current = std::ref(bodyChunk);

        this->pushScope(false, this->scopes.back().next, true);
        for (const auto& param : node.decl.params)
            this->declareSlot(param.name);

        if (node.decl.body)
            node.decl.body->accept(*this);

        this->current.get().emit(OpCode::POP);

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx);
        this->current.get().emit(OpCode::RETURN);

        this->current = saved;

        const int captureCount = this->scopes.back().base;
        bodyChunk.slotCount = this->closeScope();

        int lambdaIdx = this->addLambda(bodyIdx, static_cast<int>(node.decl.params.size()), captureCount);
        this->current.get().emit(OpCode::MAKE_LAMBDA, lambdaIdx, node.loc);
    }

    int Compiler::addNativeCall(const std::string& className, const std::string& name, int argCount, bool isStatic) {
        for (size_t i = 0; i < this->chunk.nativeCalls.size(); ++i) {
            const auto& meta = this->chunk.nativeCalls[i];
            if (meta.className == className && meta.name == name && meta.argCount == argCount && meta.isStatic == isStatic)
                return static_cast<int>(i);
        }

        this->chunk.nativeCalls.push_back({ className, name, argCount, isStatic });
        return static_cast<int>(this->chunk.nativeCalls.size() - 1);
    }

    int Compiler::addConstant(const ValueNode::ValueType& val) {
        this->current.get().constants.push_back(val);
        return static_cast<int>(this->current.get().constants.size() - 1);
    }

    int Compiler::addFunction(const std::string& name, int argCount) {
        this->current.get().functions.push_back({name, argCount});
        return static_cast<int>(this->current.get().functions.size() - 1);
    }

    int Compiler::addMacro(const std::string& name, int argCount) {
        this->current.get().macros.push_back({name, argCount});
        return static_cast<int>(this->current.get().macros.size() - 1);
    }

    int Compiler::addLambda(int bodyIndex, int argCount, int captureCount) {
        this->current.get().lambdas.push_back({bodyIndex, argCount, captureCount});
        return static_cast<int>(this->current.get().lambdas.size() - 1);
    }

    int Compiler::addVirtualCall(int classIndex, int ordinal, int argCount) {
        this->current.get().virtualCalls.push_back({classIndex, ordinal, argCount});
        return static_cast<int>(this->current.get().virtualCalls.size() - 1);
    }

    int Compiler::addSuperCall(int constructorIndex, int argCount) {
        this->current.get().superCalls.push_back({constructorIndex, argCount});
        return static_cast<int>(this->current.get().superCalls.size() - 1);
    }

    void Compiler::pushScope(bool hasThis, int base, bool inherits) {
        Scope scope;
        scope.base = base;
        scope.next = base;
        scope.hasThis = hasThis || (!this->scopes.empty() && this->scopes.back().hasThis);
        scope.inherits = inherits;
        this->scopes.push_back(std::move(scope));
    }

    int Compiler::closeScope() {
        const int slotCount = this->scopes.back().next;
        this->scopes.pop_back();
        return slotCount;
    }

    int Compiler::declareSlot(const std::string& name) {
        Scope& scope = this->scopes.back();
        auto [it, inserted] = scope.slots.emplace(name, scope.next);
        if (inserted)
            ++scope.next;

        return it->second;
    }

    std::optional<int> Compiler::resolveSlot(const std::string& name) const {
        for (auto scope = this->scopes.rbegin(); scope != this->scopes.rend(); ++scope) {
            if (auto it = scope->slots.find(name); it != scope->slots.end())
                return it->second;

            if (!scope->inherits)
                break;
        }

        return std::nullopt;
    }

    void Compiler::emitLoad(const std::string& name, const SourceLocation& loc) {
        if (auto slot = this->resolveSlot(name)) {
            this->current.get().emit(OpCode::LOAD_SLOT, *slot, loc);
            return;
        }

        this->current.get().emit(OpCode::LOAD_VAR, this->addConstant(name), loc);
    }

    void Compiler::emitStore(const std::string& name, const SourceLocation& loc) {
        if (auto slot = this->resolveSlot(name)) {
            this->current.get().emit(OpCode::STORE_SLOT, *slot, loc);
            return;
        }

        this->current.get().emit(OpCode::STORE_VAR, this->addConstant(name), loc);
    }

    std::string Compiler::methodSignature(const MethodDecl& method) const {
        std::string signature = method.name + "(";
        for (size_t i = 0; i < method.paramTypes.size(); ++i) {
            if (i != 0)
                signature += ",";

            signature += typeInfoToString(method.paramTypes[i]);
        }
        signature += ")";
        return signature;
    }

    void Compiler::compileValue(ExprNode& node, const SourceLocation& loc) {
        node.accept(*this);

        if (node.type.kind == TypeKind::Optional && !node.preserveOptional)
            this->current.get().emit(OpCode::UNWRAP, 0, loc);
    }

    std::optional<ValueNode::ValueType> Compiler::constantValue(ExprNode& node) const {
        if (node.getType() == ASTNode::Type::Value)
            return static_cast<ValueNode&>(node).value;

        if (node.getType() == ASTNode::Type::Array) {
            auto& array = static_cast<ArrayNode&>(node);
            auto result = std::make_shared<ArrayValue>();
            result->elements.reserve(array.elements.size());

            for (auto& element : array.elements) {
                auto value = this->constantValue(*element);
                if (!value.has_value())
                    return std::nullopt;

                result->elements.push_back(*value);
            }

            return result;
        }

        return std::nullopt;
    }
}
