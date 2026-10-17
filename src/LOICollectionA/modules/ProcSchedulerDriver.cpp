#include "LOICollectionA/include/ProcSchedulerDriver.h"

namespace LOICollection::LOICollectionAPI {
    ProcSchedulerDriver& ProcSchedulerDriver::getInstance() {
        static ProcSchedulerDriver instance;
        return instance;
    }
}
