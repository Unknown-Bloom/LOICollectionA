#pragma once

#include <cstddef>
#include <optional>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class InlinePass {
    public:
        explicit InlinePass(MirChunk& chunk);

        std::size_t run(int maxBodySize);

    private:
        struct ResolvedCall {
            int bodyIndex;
            int argCount;
        };

        MirChunk& mChunk;

        bool inlinable(const MirChunk& body) const;
        bool isFinal(int classIndex, int ordinal) const;
        bool derivedFrom(const ClassMeta& cls, int ancestor) const;
        std::optional<ResolvedCall> resolveTarget(const MirInstr& call) const;
        int importConstant(const MirChunk& body, int operand);

        static void remap(MirInstr& instr, int base);
    };
}