#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <cstring>

#include "LOICollectionA/frontend/ir/MirSerializer.h"

#include "LOICollectionA/frontend/ir/Abi.h"

#include "LOICollectionA/frontend/ir/opt/analysis/RefStoreAnalysis.h"

#include "LOICollectionA/utils/core/Sha256.h"

namespace LOICollection::frontend::ir::detail {
    class Writer {
    public:
        void raw(const void* data, size_t size) {
            const auto* bytes = static_cast<const char*>(data);
            this->buffer.insert(this->buffer.end(), bytes, bytes + size);
        }

        void u8(uint8_t value) { this->raw(&value, 1); }

        void u32(uint32_t value) { this->raw(&value, sizeof(value)); }

        void i32(int32_t value) { this->raw(&value, sizeof(value)); }

        void f32(float value) { this->raw(&value, sizeof(value)); }

        void u64(uint64_t value) { this->raw(&value, sizeof(value)); }

        void str(const std::string& value) {
            this->u32(static_cast<uint32_t>(value.size()));
            this->raw(value.data(), value.size());
        }

        std::string take() { return std::move(this->buffer); }

    private:
        std::string buffer;
    };

    class Reader {
    public:
        explicit Reader(const std::string& data) : blob(data) {}

        bool raw(void* out, size_t size) {
            if (this->pos + size > this->blob.size())
                return false;

            std::memcpy(out, this->blob.data() + this->pos, size);
            this->pos += size;
            return true;
        }

        bool u8(uint8_t& value) { return this->raw(&value, 1); }

        bool u32(uint32_t& value) { return this->raw(&value, sizeof(value)); }

        bool i32(int32_t& value) { return this->raw(&value, sizeof(value)); }

        bool f32(float& value) { return this->raw(&value, sizeof(value)); }

        bool u64(uint64_t& value) { return this->raw(&value, sizeof(value)); }

        bool str(std::string& value) {
            uint32_t size = 0;
            if (!this->u32(size) || this->pos + size > this->blob.size())
                return false;

            value.assign(this->blob, this->pos, size);
            this->pos += size;
            return true;
        }

        bool consumeChunk() {
            if (this->chunksRemaining == 0)
                return false;

            --this->chunksRemaining;
            return true;
        }

        [[nodiscard]] bool done() const { return this->pos == this->blob.size(); }

        [[nodiscard]] size_t position() const { return this->pos; }

        [[nodiscard]] size_t remaining() const { return this->blob.size() - this->pos; }

    private:
        const std::string& blob;
        size_t pos = 0;
        size_t chunksRemaining = 65536;
    };

    constexpr uint8_t CONST_INT = 0;
    constexpr uint8_t CONST_FLOAT = 1;
    constexpr uint8_t CONST_STRING = 2;
    constexpr uint8_t CONST_BOOL = 3;
    constexpr uint8_t CONST_NONE = 4;
    constexpr uint8_t CONST_ARRAY = 5;

    constexpr size_t kDigestSize = 32;
    constexpr size_t kMaxDepth = 64;
    constexpr size_t kMaxOptionCount = 1024;
    constexpr size_t kMaxConstantElements = 1'000'000;
    constexpr size_t kMaxSlotCount = 1 << 20;
    constexpr size_t kMaxBodyCount = 65536;
    constexpr size_t kMinChunkBytes = 56;

    void writeTypeInfo(Writer& writer, const TypeInfo& type) {
        writer.u8(static_cast<uint8_t>(type.kind));
        writer.str(type.className);
        writer.str(type.typeVar);

        writer.u32(static_cast<uint32_t>(type.variantOptions.size()));
        for (const auto& option : type.variantOptions)
            writeTypeInfo(writer, option);

        writer.u8(type.optionalInner ? 1 : 0);
        if (type.optionalInner)
            writeTypeInfo(writer, *type.optionalInner);
    }

    bool readTypeInfo(Reader& reader, TypeInfo& type, size_t depth = 0) {
        if (depth > kMaxDepth)
            return false;

        uint8_t kind = 0;
        if (!reader.u8(kind) || kind >= static_cast<uint8_t>(TypeKind::COUNT))
            return false;

        type.kind = static_cast<TypeKind>(kind);
        if (!reader.str(type.className) || !reader.str(type.typeVar))
            return false;

        uint32_t optionCount = 0;
        if (!reader.u32(optionCount) || optionCount > reader.remaining() || optionCount > kMaxOptionCount)
            return false;

        type.variantOptions.clear();
        type.variantOptions.reserve(optionCount);
        for (uint32_t i = 0; i < optionCount; ++i) {
            TypeInfo option;
            if (!readTypeInfo(reader, option, depth + 1))
                return false;

            type.variantOptions.push_back(std::move(option));
        }

        uint8_t hasInner = 0;
        if (!reader.u8(hasInner))
            return false;

        if (hasInner != 0) {
            type.optionalInner = std::make_shared<TypeInfo>();
            if (!readTypeInfo(reader, *type.optionalInner, depth + 1))
                return false;
        } else {
            type.optionalInner.reset();
        }

        return true;
    }

    bool writeValue(Writer& writer, const ValueNode::ValueType& value) {
        struct Frame {
            const std::vector<ValueNode::ValueType>* items;

            size_t index = 0;
        };

        std::vector<Frame> stack;

        auto emit = [&writer, &stack](const ValueNode::ValueType& item) -> bool {
            switch (item.index()) {
                case CONST_INT:
                    writer.u8(CONST_INT);
                    writer.i32(std::get<int>(item));
                    return true;
                case CONST_FLOAT:
                    writer.u8(CONST_FLOAT);
                    writer.f32(std::get<float>(item));
                    return true;
                case CONST_STRING:
                    writer.u8(CONST_STRING);
                    writer.str(std::get<std::string>(item));
                    return true;
                case CONST_BOOL:
                    writer.u8(CONST_BOOL);
                    writer.u8(std::get<bool>(item) ? 1 : 0);
                    return true;
                case 6: {
                    const auto& array = std::get<ArrayRef>(item);
                    if (!array)
                        return false;

                    const auto& elements = array->elements;
                    if (elements.size() > 0xFFFFFFFFull)
                        return false;

                    writer.u8(CONST_ARRAY);
                    writer.u32(static_cast<uint32_t>(elements.size()));
                    if (!elements.empty())
                        stack.push_back(Frame{ &elements, 0 });

                    return true;
                }
                case 7:
                    writer.u8(CONST_NONE);
                    return true;
                default:
                    return false;
            }
        };

        if (!emit(value))
            return false;

        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();

            if (frame.index >= frame.items->size())
                continue;

            if (frame.index + 1 < frame.items->size())
                stack.push_back(Frame{ frame.items, frame.index + 1 });

            if (!emit((*frame.items)[frame.index]))
                return false;
        }

        return true;
    }

    bool readValue(Reader& reader, ValueNode::ValueType& value, size_t depth = 0) {
        if (depth > kMaxDepth)
            return false;

        uint8_t tag = 0;
        if (!reader.u8(tag))
            return false;

        switch (tag) {
            case CONST_INT: {
                int32_t raw = 0;
                if (!reader.i32(raw)) return false;

                value = static_cast<int>(raw);
                return true;
            }
            case CONST_FLOAT: {
                float raw = 0;
                if (!reader.f32(raw)) return false;

                value = raw;
                return true;
            }
            case CONST_STRING: {
                std::string raw;
                if (!reader.str(raw)) return false;

                value = std::move(raw);
                return true;
            }
            case CONST_BOOL: {
                uint8_t raw = 0;
                if (!reader.u8(raw)) return false;

                value = raw != 0;
                return true;
            }
            case CONST_NONE:
                value = std::monostate{};
                return true;
            case CONST_ARRAY: {
                uint32_t count = 0;
                if (!reader.u32(count) || count > reader.remaining() || count > kMaxConstantElements)
                    return false;

                auto elements = std::make_shared<ArrayValue>();
                elements->elements.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    ValueNode::ValueType element;
                    if (!readValue(reader, element, depth + 1)) return false;

                    elements->elements.push_back(std::move(element));
                }

                value = std::move(elements);
                return true;
            }
            default:
                return false;
        }
    }

    template <typename T>
    void writeVector(Writer& writer, const std::vector<T>& items, auto&& write) {
        writer.u32(static_cast<uint32_t>(items.size()));
        for (const auto& item : items)
            write(writer, item);
    }

    template <typename T>
    bool readVector(Reader& reader, std::vector<T>& items, auto&& read) {
        uint32_t count = 0;
        if (!reader.u32(count) || count > reader.remaining())
            return false;

        items.clear();
        items.reserve(std::min<size_t>(count, 256));
        for (uint32_t i = 0; i < count; ++i) {
            T item{};
            if (!read(reader, item))
                return false;

            items.push_back(std::move(item));
        }

        return true;
    }

    bool writeChunk(Writer& writer, const MirChunk& chunk);

    bool readChunk(Reader& reader, MirChunk& chunk, size_t depth = 0);

    void writeInstruction(Writer& writer, const MirInstr& instr) {
        writer.u8(static_cast<uint8_t>(instr.op));
        writer.i32(instr.operand);
        writer.i32(instr.dst);
        writer.i32(instr.src1);
        writer.i32(instr.src2);
        writer.i32(instr.src3);
        writer.i32(instr.imm);
        writeTypeInfo(writer, instr.type);
    }

    bool readInstruction(Reader& reader, MirInstr& instr) {
        uint8_t op = 0;
        if (!reader.u8(op) || op >= static_cast<uint8_t>(MirOp::COUNT))
            return false;

        instr.op = static_cast<MirOp>(op);
        if (!reader.i32(instr.operand) || !reader.i32(instr.dst)
            || !reader.i32(instr.src1) || !reader.i32(instr.src2)
            || !reader.i32(instr.src3) || !reader.i32(instr.imm))
            return false;

        return readTypeInfo(reader, instr.type);
    }

    void writeDebugInfo(Writer& writer, const MirChunk& chunk) {
        std::vector<const MirChunk*> pending{&chunk};
        while (!pending.empty()) {
            const MirChunk* current = pending.back();
            pending.pop_back();

            for (const auto& instr : current->code) {
                writer.u64(instr.loc.line);
                writer.u64(instr.loc.column);
                writer.u64(instr.loc.offset);
            }

            for (const auto & methodBodie : std::ranges::reverse_view(current->methodBodies))
                if (methodBodie)
                    pending.push_back(methodBodie.get());
        }
    }

    bool readDebugInfo(Reader& reader, MirChunk& chunk, size_t depth = 0) {
        if (depth > kMaxDepth)
            return false;

        for (auto& instr : chunk.code) {
            uint64_t line = 0;
            uint64_t column = 0;
            uint64_t offset = 0;

            if (!reader.u64(line) || !reader.u64(column) || !reader.u64(offset))
                return false;

            instr.loc = SourceLocation(line, column, offset);
        }

        for (auto& body : chunk.methodBodies)
            if (!readDebugInfo(reader, *body, depth + 1))
                return false;

        return true;
    }

    size_t countInstructions(const MirChunk& chunk) {
        size_t total = 0;
        std::vector<const MirChunk*> pending{&chunk};
        while (!pending.empty()) {
            const MirChunk* current = pending.back();
            pending.pop_back();

            total += current->code.size();
            for (const auto& body : current->methodBodies)
                if (body)
                    pending.push_back(body.get());
        }

        return total;
    }

    void writeFuncMeta(Writer& writer, const FuncMeta& meta) {
        writer.str(meta.name);
        writer.i32(meta.argCount);
    }

    bool readFuncMeta(Reader& reader, FuncMeta& meta) {
        return reader.str(meta.name) && reader.i32(meta.argCount);
    }

    void writeMacroMeta(Writer& writer, const MacroMeta& meta) {
        writer.str(meta.name);
        writer.i32(meta.argCount);
    }

    bool readMacroMeta(Reader& reader, MacroMeta& meta) {
        return reader.str(meta.name) && reader.i32(meta.argCount);
    }

    void writeStrings(Writer& writer, const std::vector<std::string>& items) {
        writer.u32(static_cast<uint32_t>(items.size()));
        for (const auto& item : items)
            writer.str(item);
    }

    bool readStrings(Reader& reader, std::vector<std::string>& items) {
        return readVector<std::string>(reader, items, [](Reader& r, std::string& s) -> bool {
            return r.str(s);
        });
    }

    bool writeValues(Writer& writer, const std::vector<ValueNode::ValueType>& items) {
        writer.u32(static_cast<uint32_t>(items.size()));
        for (const auto& item : items)
            if (!writeValue(writer, item))
                return false;

        return true;
    }

    bool readValues(Reader& reader, std::vector<ValueNode::ValueType>& items) {
        return readVector<ValueNode::ValueType>(reader, items, [](Reader& r, ValueNode::ValueType& v) -> bool {
            return readValue(r, v);
        });
    }

    void writeBools(Writer& writer, const std::vector<bool>& items) {
        writer.u32(static_cast<uint32_t>(items.size()));
        for (bool item : items)
            writer.u8(item ? 1 : 0);
    }

    bool readBools(Reader& reader, std::vector<bool>& items) {
        return readVector<bool>(reader, items, [](Reader& r, bool& b) -> bool {
            uint8_t raw = 0;
            if (!r.u8(raw)) return false;

            b = raw != 0;
            return true;
        });
    }

    void writeInts(Writer& writer, const std::vector<int>& items) {
        writer.u32(static_cast<uint32_t>(items.size()));
        for (int item : items)
            writer.i32(item);
    }

    bool readInts(Reader& reader, std::vector<int>& items) {
        return readVector<int>(reader, items, [](Reader& r, int& v) -> bool {
            int32_t raw = 0;
            if (!r.i32(raw)) return false;

            v = raw;
            return true;
        });
    }

    bool writeClassMeta(Writer& writer, const ClassMeta& meta) {
        writer.str(meta.name);
        writer.i32(meta.baseClassIndex);

        writeStrings(writer, meta.fieldNames);
        if (!writeValues(writer, meta.defaults))
            return false;

        writeBools(writer, meta.hasDefault);

        writeStrings(writer, meta.staticFieldNames);
        if (!writeValues(writer, meta.staticDefaults))
            return false;

        writeBools(writer, meta.staticHasDefault);

        writer.i32(meta.constructorIndex);
        writeInts(writer, meta.methods);
        writeStrings(writer, meta.methodSignatures);
        writeInts(writer, meta.staticMethods);
        writeStrings(writer, meta.staticMethodSignatures);
        writeInts(writer, meta.ancestorIndices);
        return true;
    }

    bool readClassMeta(Reader& reader, ClassMeta& meta) {
        return reader.str(meta.name) && reader.i32(meta.baseClassIndex)
            && readStrings(reader, meta.fieldNames)
            && readValues(reader, meta.defaults)
            && readBools(reader, meta.hasDefault)
            && readStrings(reader, meta.staticFieldNames)
            && readValues(reader, meta.staticDefaults)
            && readBools(reader, meta.staticHasDefault)
            && reader.i32(meta.constructorIndex)
            && readInts(reader, meta.methods)
            && readStrings(reader, meta.methodSignatures)
            && readInts(reader, meta.staticMethods)
            && readStrings(reader, meta.staticMethodSignatures)
            && readInts(reader, meta.ancestorIndices);
    }

    bool writeEnumMeta(Writer& writer, const EnumMeta& meta) {
        writer.str(meta.name);
        writeStrings(writer, meta.variantNames);
        writer.u32(static_cast<uint32_t>(meta.variantArity.size()));
        for (const auto& a : meta.variantArity)
            writer.u8(a);
        return true;
    }

    bool readEnumMeta(Reader& reader, EnumMeta& meta) {
        uint32_t arityCount = 0;
        if (!reader.str(meta.name)
            || !readStrings(reader, meta.variantNames)
            || !reader.u32(arityCount)
            || arityCount > reader.remaining())
            return false;

        meta.variantArity.resize(arityCount);
        for (uint32_t i = 0; i < arityCount; ++i) {
            uint8_t a = 0;
            if (!reader.u8(a))
                return false;
            meta.variantArity[i] = a;
        }
        return true;
    }

    void writeMethodMeta(Writer& writer, const MethodMeta& meta) {
        writer.str(meta.name);
        writer.i32(meta.argCount);
        writer.i32(meta.classIndex);
        writer.i32(meta.bodyIndex);
    }

    bool readMethodMeta(Reader& reader, MethodMeta& meta) {
        return reader.str(meta.name)
            && reader.i32(meta.argCount)
            && reader.i32(meta.classIndex)
            && reader.i32(meta.bodyIndex);
    }

    void writeNativeCallMeta(Writer& writer, const NativeCallMeta& meta) {
        writer.str(meta.className);
        writer.str(meta.name);
        writer.i32(meta.argCount);
        writer.u8(meta.isStatic ? 1 : 0);
    }

    bool readNativeCallMeta(Reader& reader, NativeCallMeta& meta) {
        uint8_t isStatic = 0;
        if (!reader.str(meta.className) || !reader.str(meta.name) || !reader.i32(meta.argCount) || !reader.u8(isStatic))
            return false;

        meta.isStatic = isStatic != 0;
        return true;
    }

    void writeVirtualCallMeta(Writer& writer, const VirtualCallMeta& meta) {
        writer.i32(meta.classIndex);
        writer.i32(meta.ordinal);
        writer.i32(meta.argCount);
    }

    bool readVirtualCallMeta(Reader& reader, VirtualCallMeta& meta) {
        return reader.i32(meta.classIndex) && reader.i32(meta.ordinal) && reader.i32(meta.argCount);
    }

    void writeByNameCallMeta(Writer& writer, const ByNameCallMeta& meta) {
        writer.str(meta.methodName);
        writer.i32(meta.argCount);
    }

    bool readByNameCallMeta(Reader& reader, ByNameCallMeta& meta) {
        return reader.str(meta.methodName) && reader.i32(meta.argCount);
    }

    void writeSuperCallMeta(Writer& writer, const SuperCallMeta& meta) {
        writer.i32(meta.constructorIndex);
        writer.i32(meta.argCount);
    }

    bool readSuperCallMeta(Reader& reader, SuperCallMeta& meta) {
        return reader.i32(meta.constructorIndex) && reader.i32(meta.argCount);
    }

    void writeLambdaMeta(Writer& writer, const LambdaMeta& meta) {
        writer.i32(meta.bodyIndex);
        writer.i32(meta.argCount);
        writer.i32(meta.captureCount);
        writeVector<LambdaCapture>(writer, meta.captures, [](Writer& w, const LambdaCapture& capture) {
            w.str(capture.name);
            w.i32(capture.sourceSlot);
            w.i32(capture.slot);
        });
    }

    bool readLambdaMeta(Reader& reader, LambdaMeta& meta) {
        return reader.i32(meta.bodyIndex)
            && reader.i32(meta.argCount)
            && reader.i32(meta.captureCount)
            && readVector<LambdaCapture>(reader, meta.captures, [](Reader& r, LambdaCapture& capture) -> bool {
                return r.str(capture.name) && r.i32(capture.sourceSlot) && r.i32(capture.slot);
            });
    }

    bool writeChunkFlat(Writer& writer, const MirChunk& chunk) {
        writeVector<MirInstr>(writer, chunk.code, [](Writer& w, const MirInstr& instr) {
            writeInstruction(w, instr);
        });
        if (!writeValues(writer, chunk.constants))
            return false;

        writeVector<FuncMeta>(writer, chunk.functions, [](Writer& w, const FuncMeta& meta) {
            writeFuncMeta(w, meta);
        });
        writeVector<MacroMeta>(writer, chunk.macros, [](Writer& w, const MacroMeta& meta) {
            writeMacroMeta(w, meta);
        });
        writer.u32(static_cast<uint32_t>(chunk.classes.size()));
        for (const auto& cls : chunk.classes)
            if (!writeClassMeta(writer, cls))
                return false;

        writer.u32(static_cast<uint32_t>(chunk.enums.size()));
        for (const auto& enm : chunk.enums)
            if (!writeEnumMeta(writer, enm))
                return false;

        writeVector<MethodMeta>(writer, chunk.methods, [](Writer& w, const MethodMeta& meta) {
            writeMethodMeta(w, meta);
        });
        writeVector<NativeCallMeta>(writer, chunk.nativeCalls, [](Writer& w, const NativeCallMeta& meta) {
            writeNativeCallMeta(w, meta);
        });
        writeVector<VirtualCallMeta>(writer, chunk.virtualCalls, [](Writer& w, const VirtualCallMeta& meta) {
            writeVirtualCallMeta(w, meta);
        });
        writeVector<ByNameCallMeta>(writer, chunk.byNameCalls, [](Writer& w, const ByNameCallMeta& meta) {
            writeByNameCallMeta(w, meta);
        });
        writeVector<SuperCallMeta>(writer, chunk.superCalls, [](Writer& w, const SuperCallMeta& meta) {
            writeSuperCallMeta(w, meta);
        });
        writeVector<LambdaMeta>(writer, chunk.lambdas, [](Writer& w, const LambdaMeta& meta) {
            writeLambdaMeta(w, meta);
        });

        writer.i32(chunk.slotCount);
        writer.u32(static_cast<uint32_t>(chunk.methodBodies.size()));
        return true;
    }

    bool writeChunk(Writer& writer, const MirChunk& chunk) {
        std::vector<const MirChunk*> pending;
        pending.push_back(&chunk);
        while (!pending.empty()) {
            const MirChunk* current = pending.back();
            pending.pop_back();

            if (!writeChunkFlat(writer, *current))
                return false;

            for (auto it = current->methodBodies.rbegin(); it != current->methodBodies.rend(); ++it) {
                if (!*it)
                    return false;
                pending.push_back(it->get());
            }
        }

        return true;
    }

    void refreshChunkMetadata(MirChunk& chunk) {
        for (auto& cls : chunk.classes) {
            cls.ancestorMask = 0;
            for (int ancestor : cls.ancestorIndices)
                if (ancestor >= 0 && ancestor < 64)
                    cls.ancestorMask |= std::uint64_t{ 1 } << ancestor;
        }

        opt::RefStoreAnalysis analysis{ chunk };
        analysis.run();

        for (auto& body : chunk.methodBodies)
            if (body)
                refreshChunkMetadata(*body);
    }

    bool validArgCount(int count) {
        return count >= 0 && static_cast<std::size_t>(count) <= kMaxSlotCount;
    }

    bool fitsSlots(int base, std::int64_t count, int slotCount) {
        if (base < 0 || count < 0)
            return false;

        return static_cast<std::int64_t>(base) + count <= static_cast<std::int64_t>(slotCount);
    }

    bool isNameConstant(const MirChunk& chunk, int index) {
        return index >= 0 && static_cast<std::size_t>(index) < chunk.constants.size()
            && std::holds_alternative<std::string>(chunk.constants[static_cast<std::size_t>(index)]);
    }

    bool validateInstruction(const MirChunk& chunk, std::size_t ip) {
        const MirInstr& instr = chunk.code[ip];
        const int slots = chunk.slotCount;

        const auto hasConstant = [&chunk](int index) {
            return index >= 0 && static_cast<std::size_t>(index) < chunk.constants.size();
        };

        const auto hasTarget = [&chunk, ip](int offset) {
            const std::int64_t target = static_cast<std::int64_t>(ip) + 1 + offset;
            return target >= 0 && target <= static_cast<std::int64_t>(chunk.code.size());
        };

        const auto hasArgs = [slots](int base, std::int64_t count) {
            return fitsSlots(base, count, slots);
        };

        switch (instr.op) {
            case MirOp::LOAD_CONST:
                return hasConstant(instr.operand);

            case MirOp::LOAD_VAR:
            case MirOp::STORE_VAR:
            case MirOp::LOAD_FIELD:
            case MirOp::STORE_FIELD:
            case MirOp::INSTANCEOF:
                return isNameConstant(chunk, instr.operand);

            case MirOp::MAKE_ARRAY:
                return hasArgs(instr.src1, instr.operand);

            case MirOp::MAKE_LAMBDA:
                return instr.operand >= 0
                    && static_cast<std::size_t>(instr.operand) < chunk.lambdas.size();

            case MirOp::MAKE_ENUM: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.enums.size())
                    return false;

                const auto& meta = chunk.enums[static_cast<std::size_t>(instr.operand)];
                return instr.imm >= 0 && static_cast<std::size_t>(instr.imm) < meta.variantNames.size();
            }

            case MirOp::NEW: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.classes.size())
                    return false;

                const auto& cls = chunk.classes[static_cast<std::size_t>(instr.operand)];
                if (cls.constructorIndex < 0)
                    return true;

                if (static_cast<std::size_t>(cls.constructorIndex) >= chunk.methods.size())
                    return false;

                return hasArgs(instr.src1, chunk.methods[static_cast<std::size_t>(cls.constructorIndex)].argCount);
            }

            case MirOp::NEW_NATIVE:
                return instr.operand >= 0
                    && static_cast<std::size_t>(instr.operand) < chunk.nativeCalls.size()
                    && hasArgs(instr.src1, chunk.nativeCalls[static_cast<std::size_t>(instr.operand)].argCount);

            case MirOp::CALL:
                return instr.operand >= 0
                    && static_cast<std::size_t>(instr.operand) < chunk.functions.size()
                    && hasArgs(instr.src1, instr.imm);

            case MirOp::CALL_MACRO:
                return instr.operand >= 0
                    && static_cast<std::size_t>(instr.operand) < chunk.macros.size()
                    && hasArgs(instr.src1, instr.imm);

            case MirOp::CALL_METHOD: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.methods.size())
                    return false;

                return hasArgs(instr.src1, instr.imm)
                    && hasArgs(instr.src1, chunk.methods[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_METHOD_VIRTUAL: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.virtualCalls.size())
                    return false;

                return hasArgs(instr.src1, instr.imm)
                    && hasArgs(instr.src1, chunk.virtualCalls[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_METHOD_BY_NAME: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.byNameCalls.size())
                    return false;

                return hasArgs(instr.src1, instr.imm)
                    && hasArgs(instr.src1, chunk.byNameCalls[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_NATIVE_METHOD: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.nativeCalls.size())
                    return false;

                return hasArgs(instr.src1, chunk.nativeCalls[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_SUPER_CTOR: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.superCalls.size())
                    return false;

                return hasArgs(instr.src1, instr.imm)
                    && hasArgs(instr.src1, chunk.superCalls[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_FUNC: {
                if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.methods.size())
                    return false;

                return hasArgs(instr.src1, chunk.methods[static_cast<std::size_t>(instr.operand)].argCount);
            }

            case MirOp::CALL_LAMBDA:
                return hasArgs(instr.src1, instr.imm);

            case MirOp::JMP:
            case MirOp::JMP_IF_FALSE:
            case MirOp::JMP_IF_TRUE:
                return hasTarget(instr.operand);

            case MirOp::JMP_IF_CMP_FALSE:
                return hasTarget(instr.operand)
                    && instr.imm >= static_cast<int>(MirOp::CMP_EQ)
                    && instr.imm <= static_cast<int>(MirOp::CMP_LE);

            case MirOp::JMP_IF_TAG_NE:
                return hasTarget(instr.operand)
                    && instr.src2 >= 0
                    && static_cast<std::size_t>(instr.src2) < chunk.enums.size()
                    && instr.imm >= 0;

            default:
                return true;
        }
    }

    bool validateChunk(const MirChunk& chunk) {
        if (chunk.slotCount < 0 || static_cast<std::size_t>(chunk.slotCount) > kMaxSlotCount)
            return false;

        for (const auto& meta : chunk.functions)
            if (!validArgCount(meta.argCount))
                return false;

        for (const auto& meta : chunk.macros)
            if (!validArgCount(meta.argCount))
                return false;

        for (const auto& meta : chunk.nativeCalls)
            if (!validArgCount(meta.argCount))
                return false;

        for (const auto& meta : chunk.byNameCalls)
            if (!validArgCount(meta.argCount))
                return false;

        for (const auto& cls : chunk.classes) {
            if (cls.baseClassIndex < -1 || cls.baseClassIndex >= static_cast<int>(chunk.classes.size()))
                return false;

            if (cls.fieldNames.size() != cls.defaults.size()
                || cls.fieldNames.size() != cls.hasDefault.size())
                return false;

            if (cls.staticFieldNames.size() != cls.staticDefaults.size()
                || cls.staticFieldNames.size() != cls.staticHasDefault.size())
                return false;

            if (cls.methods.size() != cls.methodSignatures.size()
                || cls.staticMethods.size() != cls.staticMethodSignatures.size())
                return false;

            if (cls.constructorIndex < -1 || cls.constructorIndex >= static_cast<int>(chunk.methods.size()))
                return false;

            for (const int index : cls.methods)
                if (index < 0 || index >= static_cast<int>(chunk.methods.size()))
                    return false;

            for (const int index : cls.staticMethods)
                if (index < 0 || index >= static_cast<int>(chunk.methods.size()))
                    return false;

            for (const int ancestor : cls.ancestorIndices)
                if (ancestor < 0 || ancestor >= static_cast<int>(chunk.classes.size()))
                    return false;
        }

        for (const auto& meta : chunk.methods) {
            if (meta.classIndex < -1 || meta.classIndex >= static_cast<int>(chunk.classes.size()))
                return false;

            if (!validArgCount(meta.argCount))
                return false;

            if (meta.bodyIndex < 0 || static_cast<std::size_t>(meta.bodyIndex) >= chunk.methodBodies.size())
                return false;

            const auto& body = chunk.methodBodies[static_cast<std::size_t>(meta.bodyIndex)];
            if (!body || meta.argCount > body->slotCount)
                return false;
        }

        for (const auto& meta : chunk.virtualCalls) {
            if (meta.classIndex < 0 || static_cast<std::size_t>(meta.classIndex) >= chunk.classes.size())
                return false;

            const auto& cls = chunk.classes[static_cast<std::size_t>(meta.classIndex)];
            if (meta.ordinal < 0 || static_cast<std::size_t>(meta.ordinal) >= cls.methods.size())
                return false;

            if (!validArgCount(meta.argCount))
                return false;
        }

        for (const auto& meta : chunk.superCalls) {
            if (!validArgCount(meta.argCount))
                return false;

            if (meta.constructorIndex >= 0
                && static_cast<std::size_t>(meta.constructorIndex) >= chunk.methods.size())
                return false;
        }

        for (const auto& meta : chunk.lambdas) {
            if (meta.bodyIndex < 0 || static_cast<std::size_t>(meta.bodyIndex) >= chunk.methodBodies.size())
                return false;

            if (!validArgCount(meta.argCount))
                return false;

            if (meta.captureCount < 0
                || static_cast<std::size_t>(meta.captureCount) != meta.captures.size())
                return false;

            const auto& body = chunk.methodBodies[static_cast<std::size_t>(meta.bodyIndex)];
            if (!body || meta.argCount > body->slotCount)
                return false;

            if (static_cast<std::int64_t>(meta.captureCount) + meta.argCount > body->slotCount)
                return false;

            for (const auto& capture : meta.captures) {
                if (capture.sourceSlot < -1)
                    return false;

                if (capture.slot < 0 || capture.slot >= body->slotCount)
                    return false;
            }
        }

        for (std::size_t ip = 0; ip < chunk.code.size(); ++ip)
            if (!validateInstruction(chunk, ip))
                return false;

        return true;
    }

    bool readChunk(Reader& reader, MirChunk& chunk, size_t depth) {
        if (depth > kMaxDepth || !reader.consumeChunk())
            return false;

        if (!(readVector<MirInstr>(reader, chunk.code, [](Reader& r, MirInstr& instr) -> bool {
            return readInstruction(r, instr);
        })
            && readValues(reader, chunk.constants)
            && readVector<FuncMeta>(reader, chunk.functions, [](Reader& r, FuncMeta& meta) -> bool {
                return readFuncMeta(r, meta);
            })
            && readVector<MacroMeta>(reader, chunk.macros, [](Reader& r, MacroMeta& meta) -> bool {
                return readMacroMeta(r, meta);
            })
            && readVector<ClassMeta>(reader, chunk.classes, [](Reader& r, ClassMeta& meta) -> bool {
                return readClassMeta(r, meta);
            })
            && readVector<EnumMeta>(reader, chunk.enums, [](Reader& r, EnumMeta& meta) -> bool {
                return readEnumMeta(r, meta);
            })
            && readVector<MethodMeta>(reader, chunk.methods, [](Reader& r, MethodMeta& meta) -> bool {
                return readMethodMeta(r, meta);
            })
            && readVector<NativeCallMeta>(reader, chunk.nativeCalls, [](Reader& r, NativeCallMeta& meta) -> bool {
                return readNativeCallMeta(r, meta);
            })
            && readVector<VirtualCallMeta>(reader, chunk.virtualCalls, [](Reader& r, VirtualCallMeta& meta) -> bool {
                return readVirtualCallMeta(r, meta);
            })
            && readVector<ByNameCallMeta>(reader, chunk.byNameCalls, [](Reader& r, ByNameCallMeta& meta) -> bool {
                return readByNameCallMeta(r, meta);
            })
            && readVector<SuperCallMeta>(reader, chunk.superCalls, [](Reader& r, SuperCallMeta& meta) -> bool {
                return readSuperCallMeta(r, meta);
            })
            && readVector<LambdaMeta>(reader, chunk.lambdas, [](Reader& r, LambdaMeta& meta) -> bool {
                return readLambdaMeta(r, meta);
            })
            && reader.i32(chunk.slotCount)))
            return false;

        if (chunk.slotCount < 0 || static_cast<std::size_t>(chunk.slotCount) > kMaxSlotCount)
            return false;

        uint32_t bodyCount = 0;
        if (!reader.u32(bodyCount)
            || bodyCount > kMaxBodyCount
            || bodyCount > reader.remaining() / kMinChunkBytes)
            return false;

        chunk.methodBodies.clear();
        for (uint32_t i = 0; i < bodyCount; ++i) {
            auto body = std::make_unique<MirChunk>();
            if (!readChunk(reader, *body, depth + 1))
                return false;

            chunk.methodBodies.push_back(std::move(body));
        }

        return validateChunk(chunk);
    }
}

namespace LOICollection::frontend::ir {
    using namespace detail;

    std::optional<std::string> MirSerializer::serialize(
        const MirChunk& chunk, const Header& header, std::string* bodyChecksum
    ) {
        if (header.abiFingerprint.size() != kDigestSize)
            return std::nullopt;

        Writer payload;

        const std::string layout = layoutFingerprint(chunk);
        payload.raw(layout.data(), layout.size());

        if (!writeChunk(payload, chunk))
            return std::nullopt;

        Writer writer;

        writer.raw(MAGIC, sizeof(MAGIC));
        writer.u32(FORMAT_VERSION);
        writer.str(header.scriptId);
        writer.raw(header.abiFingerprint.data(), header.abiFingerprint.size());
        writer.u8(header.sourceHash ? 1 : 0);
        if (header.sourceHash)
            writer.raw(header.sourceHash->data(), header.sourceHash->size());

        writer.u32(static_cast<uint32_t>(header.importHashes.size()));
        for (const auto& hash : header.importHashes)
            writer.raw(hash.data(), hash.size());

        const std::string body = payload.take();
        const std::string checksum = utils::Sha256::compute(body);
        writer.raw(checksum.data(), checksum.size());
        writer.raw(body.data(), body.size());

        if (bodyChecksum)
            *bodyChecksum = checksum;

        return writer.take();
    }

    std::optional<MirChunk> MirSerializer::deserialize(
        const std::string& blob, const Header& expected, std::string* bodyChecksum
    ) {
        Reader reader(blob);

        char magic[sizeof(MAGIC)] = {};
        uint32_t version = 0;
        if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
            return std::nullopt;

        if (!reader.u32(version) || version != FORMAT_VERSION)
            return std::nullopt;

        std::string scriptId;
        if (!reader.str(scriptId) || scriptId != expected.scriptId)
            return std::nullopt;

        std::string fingerprint(expected.abiFingerprint.size(), '\0');
        if (fingerprint.empty()
            || !reader.raw(fingerprint.data(), fingerprint.size())
            || fingerprint != expected.abiFingerprint)
            return std::nullopt;

        uint8_t hasSource = 0;
        if (!reader.u8(hasSource))
            return std::nullopt;

        std::string sourceHash(kDigestSize, '\0');
        if (hasSource != 0 && !reader.raw(sourceHash.data(), sourceHash.size()))
            return std::nullopt;

        uint32_t importCount = 0;
        if (!reader.u32(importCount) || importCount > reader.remaining() / kDigestSize)
            return std::nullopt;

        std::vector<std::string> importHashes;
        importHashes.reserve(importCount);
        for (uint32_t i = 0; i < importCount; ++i) {
            std::string hash(kDigestSize, '\0');
            if (!reader.raw(hash.data(), hash.size()))
                return std::nullopt;

            importHashes.push_back(std::move(hash));
        }

        if (expected.sourceHash
            && (hasSource == 0 || sourceHash != *expected.sourceHash || importHashes != expected.importHashes))
            return std::nullopt;

        std::string checksum(kDigestSize, '\0');
        if (!reader.raw(checksum.data(), checksum.size()))
            return std::nullopt;

        const size_t bodyOffset = reader.position();
        const std::string body = blob.substr(bodyOffset);
        if (utils::Sha256::compute(body) != checksum)
            return std::nullopt;

        if (bodyChecksum)
            *bodyChecksum = checksum;

        std::string layout(kDigestSize, '\0');
        if (!reader.raw(layout.data(), layout.size()))
            return std::nullopt;

        MirChunk chunk;
        if (!readChunk(reader, chunk) || !reader.done())
            return std::nullopt;

        if (layoutFingerprint(chunk) != layout)
            return std::nullopt;

        detail::refreshChunkMetadata(chunk);

        return chunk;
    }

    std::optional<MirSerializer::Header> MirSerializer::peekHeader(const std::string& blob) {
        Reader reader(blob);

        char magic[sizeof(MAGIC)] = {};
        uint32_t version = 0;
        if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
            return std::nullopt;

        if (!reader.u32(version) || version != FORMAT_VERSION)
            return std::nullopt;

        Header header;
        if (!reader.str(header.scriptId))
            return std::nullopt;

        header.abiFingerprint.resize(kDigestSize);
        if (!reader.raw(header.abiFingerprint.data(), header.abiFingerprint.size()))
            return std::nullopt;

        uint8_t hasSource = 0;
        if (!reader.u8(hasSource))
            return std::nullopt;

        if (hasSource != 0) {
            std::string sourceHash(kDigestSize, '\0');
            if (!reader.raw(sourceHash.data(), sourceHash.size()))
                return std::nullopt;

            header.sourceHash = std::move(sourceHash);
        }

        uint32_t importCount = 0;
        if (!reader.u32(importCount) || importCount > reader.remaining() / kDigestSize)
            return std::nullopt;

        header.importHashes.reserve(importCount);
        for (uint32_t i = 0; i < importCount; ++i) {
            std::string hash(kDigestSize, '\0');
            if (!reader.raw(hash.data(), hash.size()))
                return std::nullopt;

            header.importHashes.push_back(std::move(hash));
        }

        return header;
    }

    std::optional<std::string> MirSerializer::serializeDebugInfo(
        const MirChunk& chunk, const std::string& bodyChecksum
    ) {
        if (bodyChecksum.size() != kDigestSize)
            return std::nullopt;

        Writer writer;

        writer.raw(DEBUG_MAGIC, sizeof(DEBUG_MAGIC));
        writer.u32(FORMAT_VERSION);
        writer.raw(bodyChecksum.data(), bodyChecksum.size());
        writer.u64(countInstructions(chunk));
        writeDebugInfo(writer, chunk);

        return writer.take();
    }

    bool MirSerializer::attachDebugInfo(
        MirChunk& chunk, const std::string& debugBlob, const std::string& bodyChecksum
    ) {
        Reader reader(debugBlob);

        char magic[sizeof(DEBUG_MAGIC)] = {};
        uint32_t version = 0;
        if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, DEBUG_MAGIC, sizeof(DEBUG_MAGIC)) != 0)
            return false;

        if (!reader.u32(version) || version != FORMAT_VERSION)
            return false;

        std::string checksum(bodyChecksum.size(), '\0');
        if (checksum.empty() || !reader.raw(checksum.data(), checksum.size()) || checksum != bodyChecksum)
            return false;

        uint64_t count = 0;
        if (!reader.u64(count) || count != countInstructions(chunk))
            return false;

        return readDebugInfo(reader, chunk) && reader.done();
    }
}
