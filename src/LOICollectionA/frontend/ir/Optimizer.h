#pragma once

#include <cstddef>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend::ir {
    class Optimizer {
    public:
        enum class Pass : unsigned {
            ConstantFold = 1u << 0,
            DeadCode = 1u << 1,
            DeadStore = 1u << 2,
            LICM = 1u << 3,
            CSE = 1u << 4
        };

        static constexpr unsigned allPasses =
            static_cast<unsigned>(Pass::ConstantFold) |
            static_cast<unsigned>(Pass::DeadCode) |
            static_cast<unsigned>(Pass::DeadStore) |
            static_cast<unsigned>(Pass::LICM) |
            static_cast<unsigned>(Pass::CSE);

        struct Stats {
            size_t folded = 0;
            size_t removed = 0;
        };

        LOICOLLECTION_A_API Stats optimize(MirChunk& chunk);

        void setEnabledPasses(unsigned mask) { mEnabledPasses = mask; }

        unsigned enabledPasses() const { return mEnabledPasses; }

    private:
        bool enabled(Pass pass) const { return (mEnabledPasses & static_cast<unsigned>(pass)) != 0u; }

        Stats optimizeChunk(MirChunk& chunk);
        Stats optimizeChunkOnce(MirChunk& chunk);

        unsigned mEnabledPasses = allPasses;
    };
}
