#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"
#include "LOICollectionA/frontend/sandbox/SandboxReport.h"

namespace LOICollection::frontend::ir {
    class VM {
    public:
        LOICOLLECTION_A_NDAPI VM(DiagnosticEngine& diag) : VM(diag, std::make_shared<GlobalsTable>()) {}

        LOICOLLECTION_A_NDAPI VM(DiagnosticEngine& diag, std::shared_ptr<GlobalsTable> globals)
            : diagnostics(diag),
              globals(std::move(globals)) {}

        LOICOLLECTION_A_NDAPI ValueNode::ValueType run(
            const std::shared_ptr<const BytecodeChunk>& chunk,
            const Context& ctx
        );

        LOICOLLECTION_A_API   void setBudget(const sandbox::SandboxBudget& budget) { *mBudget = budget; }
        [[nodiscard]] LOICOLLECTION_A_NDAPI const sandbox::SandboxBudget& budget() const { return *mBudget; }
        [[nodiscard]] LOICOLLECTION_A_NDAPI const sandbox::SandboxReport& report() const { return mReport; }

        LOICOLLECTION_A_NDAPI static ValueNode::ValueType callFunctionRef(
            const FunctionRefPtr& func,
            const CallbackTypeValues& args,
            const CallbackTypePlaces& placeholders,
            DiagnosticEngine& diagnostics
        );

        LOICOLLECTION_A_NDAPI static std::string valueToString(const ValueNode::ValueType& val);
        LOICOLLECTION_A_NDAPI static std::string typeNameOf(const ValueNode::ValueType& val);
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyUnary(const ValueNode::ValueType& operand, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static bool valueToBool(const ValueNode::ValueType& val);
        LOICOLLECTION_A_NDAPI static bool applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});

    private:
        struct Frame {
            std::reference_wrapper<const BytecodeChunk> chunk;

            size_t ip = 0;
            std::vector<ValueNode::ValueType> locals;

            ValueNode::ValueType thisObj;
            bool hasThis = false;

            ValueNode::ValueType pendingPush;
            bool hasPending = false;

            explicit Frame(const BytecodeChunk& chunkRef)
                : chunk(chunkRef), locals(chunkRef.slotCount) {}
        };

        DiagnosticEngine& diagnostics;
        SourceLocation currentLoc;

        sandbox::SandboxReport mReport;
        std::shared_ptr<sandbox::SandboxBudget> mBudget = std::make_shared<sandbox::SandboxBudget>();

        std::vector<Frame> frames;
        std::vector<ValueNode::ValueType> stack;

        std::shared_ptr<GlobalsTable> globals;

        std::unordered_map<int, FunctionCallCacheSlot> mFunctionCallSlots;
        std::unordered_map<int, NativeMethodCacheSlot> mNativeMethodSlots;
        std::unordered_map<int, NativeStaticMethodCacheSlot> mNativeStaticMethodSlots;
        std::unordered_map<int, NativeValueMethodCacheSlot> mNativeValueMethodSlots;
        std::unordered_map<int, NativeConstructorCacheSlot> mNativeConstructorSlots;
        std::unordered_map<int, FieldLayoutPtr> mClassLayouts;

        [[nodiscard]] const FieldLayoutPtr& classLayout(const BytecodeChunk& chunk, int classIndex);

        ValueNode::ValueType execute(
            const std::shared_ptr<const BytecodeChunk>& owner,
            const CallbackTypePlaces& placeholders
        );

        void push(const ValueNode::ValueType& v);
        ValueNode::ValueType pop();

        void storeVariable(
            const BytecodeChunk& chunk,
            Frame& frame,
            const std::string& name,
            const ValueNode::ValueType& val
        );

        bool pushFrame(Frame&& frame);

        [[nodiscard]] bool isDerived(const BytecodeChunk& chunk, int derivedClassIndex, int baseClassIndex) const;

        [[nodiscard]] static ValueNode::ValueType cloneValue(const ValueNode::ValueType& val);

        struct ExecArgs {
            const std::shared_ptr<const BytecodeChunk>& owner;
            const BytecodeChunk& chunk;
            const BytecodeChunk& cur;
            Frame& frame;
            const Instruction& instr;
            const CallbackTypePlaces& placeholders;
        };

        void failBudget(sandbox::SandboxBudget::Violation violation, const std::string& message);

        void execPushConst(ExecArgs& s);
        void execStackManip(ExecArgs& s);
        void execOptional(ExecArgs& s);
        void execLocalSlot(ExecArgs& s);
        void execVariable(ExecArgs& s);
        void execFieldAccess(ExecArgs& s);
        void execArray(ExecArgs& s);
        void execClosure(ExecArgs& s);
        void execInstanceof(ExecArgs& s);
        void execObjectCreate(ExecArgs& s);
        void execMethodDispatch(ExecArgs& s);
        void execNativeCall(ExecArgs& s);
        void execFunctionCall(ExecArgs& s);
        void execArithmetic(ExecArgs& s);
        void execComparison(ExecArgs& s);
        void execLogic(ExecArgs& s);
        void execHostCall(ExecArgs& s);
        void execBranch(ExecArgs& s);
    };
}
