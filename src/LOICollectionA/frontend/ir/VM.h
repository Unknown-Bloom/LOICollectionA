#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"
#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

namespace LOICollection::frontend::ir {
    class VM {
    public:
        LOICOLLECTION_A_NDAPI VM(DiagnosticEngine& diag) : diagnostics(diag) {}

        LOICOLLECTION_A_NDAPI ValueNode::ValueType run(
            const std::shared_ptr<const BytecodeChunk>& chunk,
            const Context& ctx
        );

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
            std::unordered_map<std::string, ValueNode::ValueType> locals;

            ValueNode::ValueType thisObj;
            bool hasThis = false;
            
            ValueNode::ValueType pendingPush;
            bool hasPending = false;

            explicit Frame(const BytecodeChunk& chunkRef) : chunk(chunkRef) {}
        };

        DiagnosticEngine& diagnostics;
        SourceLocation currentLoc;

        std::vector<Frame> frames;
        std::vector<ValueNode::ValueType> stack;
        std::unordered_map<std::string, ValueNode::ValueType> variables;

        ValueNode::ValueType execute(
            const std::shared_ptr<const BytecodeChunk>& owner,
            const CallbackTypePlaces& placeholders
        );

        void push(const ValueNode::ValueType& v);
        ValueNode::ValueType pop();

        bool pushFrame(Frame&& frame);

        [[nodiscard]] bool isDerived(const BytecodeChunk& chunk, int derivedClassIndex, int baseClassIndex) const;

        [[nodiscard]] static ValueNode::ValueType cloneValue(const ValueNode::ValueType& val);

        /* Execution budget and call depth limits for third-party scripts (§7.2):
         * a runaway script must abort with a diagnostic instead of hanging the
         * server or exhausting the native stack. */
        static constexpr size_t MAX_INSTRUCTIONS = 1'000'000;
        static constexpr size_t MAX_FRAMES = 256;
    };
}
