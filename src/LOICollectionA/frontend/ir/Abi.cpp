#include <string_view>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/utils/core/Sha256.h"

#include "LOICollectionA/frontend/ir/Abi.h"

namespace LOICollection::frontend::ir::detail {
    void absorbText(std::string& out, std::string_view value) {
        out += std::to_string(value.size());
        out += ':';
        out += value;
    }

    template <class Integer>
    void absorbNumber(std::string& out, Integer value) {
        out += std::to_string(value);
        out += ',';
    }

    /* Serialize the flat layout of a single chunk: its slot count, classes and
     * methods. Method bodies are walked by the iterative driver below so that an
     * arbitrarily deep body chain cannot blow the native stack. */
    static void absorbChunkBody(std::string& out, const MirChunk& chunk) {
        absorbNumber(out, chunk.slotCount);
        absorbNumber(out, chunk.classes.size());

        for (const auto& cls : chunk.classes) {
            absorbText(out, cls.name);
            absorbNumber(out, cls.baseClassIndex);
            absorbNumber(out, cls.ancestorIndices.size());
            for (int ancestor : cls.ancestorIndices)
                absorbNumber(out, ancestor);

            absorbNumber(out, cls.fieldNames.size());
            for (const auto& field : cls.fieldNames)
                absorbText(out, field);

            for (bool hasDefault : cls.hasDefault)
                out += hasDefault ? '1' : '0';

            absorbNumber(out, cls.staticFieldNames.size());
            for (const auto& field : cls.staticFieldNames)
                absorbText(out, field);

            for (bool hasDefault : cls.staticHasDefault)
                out += hasDefault ? '1' : '0';

            absorbNumber(out, cls.constructorIndex);
            absorbNumber(out, cls.methods.size());
            for (int method : cls.methods)
                absorbNumber(out, method);

            for (const auto& signature : cls.methodSignatures)
                absorbText(out, signature);

            absorbNumber(out, cls.staticMethods.size());
            for (int method : cls.staticMethods)
                absorbNumber(out, method);

            for (const auto& signature : cls.staticMethodSignatures)
                absorbText(out, signature);

            out += '|';
        }

        absorbNumber(out, chunk.methods.size());
        for (const auto& method : chunk.methods) {
            absorbText(out, method.name);
            absorbNumber(out, method.argCount);
            absorbNumber(out, method.classIndex);
            absorbNumber(out, method.bodyIndex);
        }
    }

    /* Iterative driver over the whole chunk tree. Visits each chunk in pre-order
     * (the chunk's own content first, then its method bodies in declaration
     * order) by maintaining an explicit worklist instead of recursing. This keeps
     * the emitted layout byte-identical to the previous recursive traversal while
     * making arbitrarily deep nesting safe. The old recursion overflowed the
     * stack on deeply nested chunks (e.g. MirSerializerTest.RejectsExcessiveNesting
     * builds 4096 nested bodies), which crashed the runtime-test build. */
    void absorbChunk(std::string& out, const MirChunk& root) {
        std::vector<const MirChunk*> worklist;
        worklist.push_back(&root);

        while (!worklist.empty()) {
            const MirChunk& chunk = *worklist.back();
            worklist.pop_back();

            absorbChunkBody(out, chunk);

            // Push children in reverse so they are popped (and therefore visited)
            // in the original forward order, matching the old recursion exactly.
            for (auto it = chunk.methodBodies.rbegin(); it != chunk.methodBodies.rend(); ++it)
                if (*it)
                    worklist.push_back(it->get());
        }
    }
}

namespace LOICollection::frontend::ir {
    using namespace detail;

    std::string abiFingerprint() {
        return utils::Sha256::compute(
            FunctionCall::getInstance().exportShape()
            + MacroCall::getInstance().exportShape()
            + ClassCall::getInstance().exportShape()
        );
    }

    std::string layoutFingerprint(const MirChunk& chunk) {
        std::string shape;
        absorbChunk(shape, chunk);

        return utils::Sha256::compute(shape);
    }
}
