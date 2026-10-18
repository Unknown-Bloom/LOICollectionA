#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/ChunkCache.h"
#include "LOICollectionA/frontend/ir/CycleCollector.h"
#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"
#include "LOICollectionA/frontend/sandbox/SandboxReport.h"

namespace LOICollection::frontend::ir {
    class VM;
}

namespace LOICollection::frontend::ir::detail {
    extern thread_local VM* tlsCurrentVM;
}

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

        enum class RunOutcome { Completed, Yielded, SliceExpired, Blocked, Failed };
        enum class BlockKind { None, Sleep, Recv, Join };

        struct TaskEvent {
            BlockKind kind = BlockKind::None;
            ValueNode::ValueType arg;
            uint64_t joinTarget = 0;
            std::chrono::steady_clock::time_point wakeupAt;
        };

        LOICOLLECTION_A_NDAPI RunOutcome runTask(
            const FunctionRefPtr& func,
            const CallbackTypeValues& args,
            size_t sliceInstructions
        );

        LOICOLLECTION_A_API   void setSchedulerManaged(bool managed) { mSchedulerManaged = managed; }
        LOICOLLECTION_A_NDAPI bool isSchedulerManaged() const { return mSchedulerManaged; }

        LOICOLLECTION_A_API   void requestYield() { if (mSchedulerManaged) mStopRequest = StopKind::Yield; }
        LOICOLLECTION_A_API   void requestSleep(std::chrono::steady_clock::time_point at) { if (mSchedulerManaged) { mStopRequest = StopKind::Sleep; mWakeupAt = at; } }
        LOICOLLECTION_A_API   void requestRecv() { if (mSchedulerManaged) mStopRequest = StopKind::Recv; }
        LOICOLLECTION_A_API   void requestJoin(uint64_t target) { if (mSchedulerManaged) { mStopRequest = StopKind::Join; mJoinTarget = target; } }

        LOICOLLECTION_A_NDAPI const TaskEvent& blockInfo() const { return mBlockInfo; }
        LOICOLLECTION_A_NDAPI const ValueNode::ValueType& taskResult() const { return mTaskResult; }

        LOICOLLECTION_A_API   static VM* current() { return detail::tlsCurrentVM; }

        LOICOLLECTION_A_API   void setBudget(const sandbox::SandboxBudget& budget) { *mBudget = budget; }
        LOICOLLECTION_A_NDAPI const sandbox::SandboxBudget& budget() const { return *mBudget; }
        LOICOLLECTION_A_NDAPI const sandbox::SandboxReport& report() const { return mReport; }

        LOICOLLECTION_A_API void reset();

        LOICOLLECTION_A_API void setGlobals(std::shared_ptr<GlobalsTable> globalsTable) {
            globals = std::move(globalsTable);
        }

        LOICOLLECTION_A_API void setHeap(std::shared_ptr<CycleCollector> heap) {
            if (heap)
                this->mHeap = std::move(heap);
        }

        [[nodiscard]] LOICOLLECTION_A_NDAPI CycleCollector& heap() const { return *mHeap; }

        LOICOLLECTION_A_NDAPI DiagnosticEngine& diagnosticsEngine() { return diagnostics; }

        LOICOLLECTION_A_NDAPI static ValueNode::ValueType callFunctionRef(
            const FunctionRefPtr& func,
            const CallbackTypeValues& args,
            const CallbackTypePlaces& placeholders,
            DiagnosticEngine& diagnostics,
            const sandbox::SandboxBudget* budget = nullptr
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

        std::shared_ptr<ChunkCache> mCache = std::make_shared<ChunkCache>();
        const MirChunk* mCacheChunk = nullptr;

        struct FieldCacheEntry {
            const MirInstr* instr = nullptr;
            const FieldLayout* layout = nullptr;
            int slot = -1;
        };

        struct ByNameCacheEntry {
            const MirInstr* instr = nullptr;
            int classIndex = -1;
            int ordinal = -1;
        };

        struct HostCallCacheEntry {
            const MirInstr* instr = nullptr;
            std::string namespaces;
            std::string function;
        };

        static constexpr std::size_t kInlineCacheSize = 128;
        static constexpr std::size_t kHostCallCacheSize = 32;

        std::array<FieldCacheEntry, kInlineCacheSize> mFieldCache{};
        std::array<ByNameCacheEntry, kInlineCacheSize> mByNameCache{};
        std::array<HostCallCacheEntry, kHostCallCacheSize> mHostCallCache{};
        std::uint64_t mInlineEpoch = 0;

        std::shared_ptr<CycleCollector> mHeap = std::make_shared<CycleCollector>();

        [[nodiscard]] const FieldLayoutPtr& classLayout(const MirChunk& chunk, int classIndex);

        template <std::size_t Size>
        [[nodiscard]] static std::size_t inlineCacheSlot(const MirInstr* instr) {
            return (reinterpret_cast<std::uintptr_t>(instr) >> 4) & (Size - 1);
        }

        [[nodiscard]] int resolveFieldSlot(Object& obj, const std::string& name, const MirInstr& instr);

        ValueNode::ValueType execute(
            const std::shared_ptr<const MirChunk>& owner,
            const CallbackTypePlaces& placeholders
        );

        template <bool Managed>
        ValueNode::ValueType executeLoop(
            const std::shared_ptr<const MirChunk>& owner,
            const CallbackTypePlaces& placeholders
        );

        [[nodiscard]] ValueNode::ValueType& regOf(Frame& frame, int index);
        [[nodiscard]] const ValueNode::ValueType& regOf(const Frame& frame, int index);

        void setReg(Frame& frame, int index, ValueNode::ValueType value);

        [[nodiscard]] std::vector<ValueNode::ValueType> collectArgs(const Frame& frame, int base, int count);

        void placeArgs(std::size_t sourceBase, int count, std::size_t offset = 0);

        void storeVariable(
            const MirChunk& chunk,
            Frame& frame,
            const std::string& name,
            const ValueNode::ValueType& val
        );

        bool pushFrame(Frame&& frame);

        [[nodiscard]] bool isDerived(const MirChunk& chunk, int derivedClassIndex, int baseClassIndex) const;

        [[nodiscard]] static ValueNode::ValueType cloneValue(const ValueNode::ValueType& val);

        void setupRootFrame(const FunctionRefPtr& func, const CallbackTypeValues& args);

        enum class StopKind { None, Yield, Slice, Sleep, Recv, Join };

        StopKind mStopRequest = StopKind::None;
        bool mSchedulerManaged = false;
        RunOutcome mOutcome = RunOutcome::Completed;
        TaskEvent mBlockInfo;
        ValueNode::ValueType mTaskResult;
        std::chrono::steady_clock::time_point mWakeupAt{};
        uint64_t mJoinTarget = 0;
        size_t mSliceRemaining = 0;

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
