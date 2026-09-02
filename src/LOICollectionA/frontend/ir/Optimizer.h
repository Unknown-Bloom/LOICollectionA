#pragma once

#include <cstddef>

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend::ir {
    class Optimizer {
    public:
        enum class Pass : unsigned {
            ConstantFold = 1u << 0,
            DeadCode = 1u << 1,
            DeadStore = 1u << 2
        };

        static constexpr unsigned allPasses =
            static_cast<unsigned>(Pass::ConstantFold) |
            static_cast<unsigned>(Pass::DeadCode) |
            static_cast<unsigned>(Pass::DeadStore);

        struct Stats {
            size_t folded = 0;
            size_t removed = 0;
        };

        LOICOLLECTION_A_API Stats optimize(BytecodeChunk& chunk);

        void setEnabledPasses(unsigned mask) { mEnabledPasses = mask; }

        unsigned enabledPasses() const { return mEnabledPasses; }

    private:
        bool enabled(Pass pass) const { return (mEnabledPasses & static_cast<unsigned>(pass)) != 0u; }

        Stats optimizeChunk(BytecodeChunk& chunk);
        Stats optimizeChunkOnce(BytecodeChunk& chunk);

        unsigned mEnabledPasses = allPasses;
    };
}
