#pragma once

#include <cstddef>
#include <optional>

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    class InlinePass {
    public:
        explicit InlinePass(Chunk& chunk);

        std::size_t run(int maxBodySize);

    private:
        struct ResolvedCall {
            int bodyIndex;
            int argCount;
        };

        Chunk& mChunk;

        bool inlinable(const Chunk& body) const;
        bool isFinal(int classIndex, int ordinal) const;
        bool derivedFrom(const ClassMeta& cls, int ancestor) const;
        std::optional<ResolvedCall> resolveTarget(const Instr& call) const;
        int importConstant(const Chunk& body, int operand);

        static void remap(Instr& instr, int base);
    };
}