#pragma once

#include <cstddef>
#include <string>

#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"

namespace LOICollection::frontend::sandbox {
    struct SandboxReport {
        std::size_t executedInstructions = 0;
        std::size_t nativeCallCount = 0;
        std::size_t objectCount = 0;
        std::size_t allocatedBytes = 0;
        std::chrono::milliseconds wallTime{ 0 };

        SandboxBudget::Violation violation = SandboxBudget::Violation::None;
        bool hasErrors = false;
        std::string errorMessage;
    };
}
