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
#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"
#include "LOICollectionA/frontend/sandbox/SandboxReport.h"

namespace LOICollection::frontend::ir {
    class VM {
    public:
        LOICOLLECTION_A_NDAPI VM(DiagnosticEngine& diag) : VM(diag, std::make_shared<GlobalsTable>()) {}

        LOICOLLECTION_A_NDAPI VM(DiagnosticEngine& diag, std::shared_ptr<GlobalsTable> globalsTable)
            : diagnostics(diag),
              globals(std::move(globalsTable)) {}

        LOICOLLECTION_A_NDAPI ValueNode::ValueType run(
            const std::shared_ptr<const MirChunk>& chunk,
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
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyUnary(const ValueNode::ValueType& operand, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static ValueNode::ValueType applyUnary(const ValueNode::ValueType& operand, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static bool valueToBool(const ValueNode::ValueType& val);
        LOICOLLECTION_A_NDAPI static bool applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});
        LOICOLLECTION_A_NDAPI static bool applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc = {});

    private:
        struct FieldCacheSlot {
            std::string name;
            const FieldLayout* layout = nullptr;
            int slot = -1;
        };

        struct Frame {
            std::reference_wrapper<const MirChunk> chunk;

            size_t ip = 0;
            size_t localsBase = 0;
            size_t localsSize = 0;

            ValueNode::ValueType thisObj;
            bool hasThis = false;

            ValueNode::ValueType pendingPush;
            bool hasPending = false;

            int returnReg = -1;

            explicit Frame(const MirChunk& chunkRef)
                : chunk(chunkRef), localsSize(chunkRef.slotCount) {}
        };

        DiagnosticEngine& diagnostics;
        SourceLocation currentLoc;

        sandbox::SandboxReport mReport;
        std::shared_ptr<sandbox::SandboxBudget> mBudget = std::make_shared<sandbox::SandboxBudget>();

        std::vector<Frame> frames;
        std::vector<ValueNode::ValueType> localPool;

        ValueNode::ValueType deadReg;

        std::shared_ptr<GlobalsTable> globals;

        std::unordered_map<int, FunctionCallCacheSlot> mFunctionCallSlots;
        std::unordered_map<int, NativeMethodCacheSlot> mNativeMethodSlots;
        std::unordered_map<int, NativeStaticMethodCacheSlot> mNativeStaticMethodSlots;
        std::unordered_map<int, NativeValueMethodCacheSlot> mNativeValueMethodSlots;
        std::unordered_map<int, NativeConstructorCacheSlot> mNativeConstructorSlots;
        std::unordered_map<int, FieldLayoutPtr> mClassLayouts;
        std::unordered_map<const MirInstr*, FieldCacheSlot> mFieldSlots;

        [[nodiscard]] const FieldLayoutPtr& classLayout(const MirChunk& chunk, int classIndex);

        [[nodiscard]] int resolveFieldSlot(Object& obj, const std::string& name, const MirInstr& instr);

        ValueNode::ValueType execute(
            const std::shared_ptr<const MirChunk>& owner,
            const CallbackTypePlaces& placeholders
        );

        [[nodiscard]] ValueNode::ValueType& regOf(Frame& frame, int index);
        [[nodiscard]] const ValueNode::ValueType& regOf(const Frame& frame, int index);

        void setReg(Frame& frame, int index, ValueNode::ValueType value);

        [[nodiscard]] std::vector<ValueNode::ValueType> collectArgs(const Frame& frame, int base, int count);

        void placeArgs(std::vector<ValueNode::ValueType>&& args, size_t offset);

        void storeVariable(
            const MirChunk& chunk,
            Frame& frame,
            const std::string& name,
            const ValueNode::ValueType& val
        );

        bool pushFrame(Frame&& frame);

        [[nodiscard]] bool isDerived(const MirChunk& chunk, int derivedClassIndex, int baseClassIndex) const;

        [[nodiscard]] static ValueNode::ValueType cloneValue(const ValueNode::ValueType& val);

        struct ExecArgs {
            const std::shared_ptr<const MirChunk>& owner;
            const MirChunk& chunk;
            const MirChunk& cur;
            Frame& frame;
            const MirInstr& instr;
            const CallbackTypePlaces& placeholders;
        };

        void failBudget(sandbox::SandboxBudget::Violation violation, const std::string& message);

        void execLoadConst(ExecArgs& s);
        void execMove(ExecArgs& s);
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
