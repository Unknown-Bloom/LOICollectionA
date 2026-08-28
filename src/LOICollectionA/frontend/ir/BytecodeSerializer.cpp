#include <cstring>

#include "LOICollectionA/frontend/ir/BytecodeSerializer.h"

#include "LOICollectionA/utils/core/Sha256.h"

namespace LOICollection::frontend::ir {
    namespace {
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
            explicit Reader(const std::string& blob) : blob(blob) {}

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

            bool done() const { return this->pos == this->blob.size(); }

            size_t position() const { return this->pos; }

        private:
            const std::string& blob;
            size_t pos = 0;
        };

        constexpr uint8_t CONST_INT = 0;
        constexpr uint8_t CONST_FLOAT = 1;
        constexpr uint8_t CONST_STRING = 2;
        constexpr uint8_t CONST_BOOL = 3;
        constexpr uint8_t CONST_NONE = 4;
        constexpr uint8_t CONST_ARRAY = 5;

        constexpr size_t kDigestSize = 32;

        bool writeValue(Writer& writer, const ValueNode::ValueType& value) {
            switch (value.index()) {
                case CONST_INT:
                    writer.u8(CONST_INT);
                    writer.i32(std::get<int>(value));
                    return true;
                case CONST_FLOAT:
                    writer.u8(CONST_FLOAT);
                    writer.f32(std::get<float>(value));
                    return true;
                case CONST_STRING:
                    writer.u8(CONST_STRING);
                    writer.str(std::get<std::string>(value));
                    return true;
                case CONST_BOOL:
                    writer.u8(CONST_BOOL);
                    writer.u8(std::get<bool>(value) ? 1 : 0);
                    return true;
                case 6: {
                    writer.u8(CONST_ARRAY);

                    const auto& elements = std::get<ArrayRef>(value)->elements;
                    writer.u32(static_cast<uint32_t>(elements.size()));
                    for (const auto& element : elements)
                        if (!writeValue(writer, element))
                            return false;

                    return true;
                }
                case 7:
                    writer.u8(CONST_NONE);
                    return true;
                default:
                    return false;
            }
        }

        bool readValue(Reader& reader, ValueNode::ValueType& value) {
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
                    if (!reader.u32(count)) return false;

                    auto elements = std::make_shared<ArrayValue>();
                    elements->elements.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        ValueNode::ValueType element;
                        if (!readValue(reader, element)) return false;

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
            if (!reader.u32(count))
                return false;

            items.clear();
            items.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                T item{};
                if (!read(reader, item))
                    return false;

                items.push_back(std::move(item));
            }

            return true;
        }

        bool writeChunk(Writer& writer, const BytecodeChunk& chunk);

        bool readChunk(Reader& reader, BytecodeChunk& chunk);

        void writeInstruction(Writer& writer, const Instruction& instr) {
            writer.u8(static_cast<uint8_t>(instr.op));
            writer.i32(instr.operand);
        }

        bool readInstruction(Reader& reader, Instruction& instr) {
            uint8_t op = 0;
            int32_t operand = 0;

            if (!reader.u8(op) || !reader.i32(operand))
                return false;

            if (op > static_cast<uint8_t>(OpCode::LOAD_LEN))
                return false;

            instr.op = static_cast<OpCode>(op);
            instr.operand = operand;
            return true;
        }

        void writeDebugInfo(Writer& writer, const BytecodeChunk& chunk) {
            for (const auto& instr : chunk.code) {
                writer.u64(instr.loc.line);
                writer.u64(instr.loc.column);
                writer.u64(instr.loc.offset);
            }

            for (const auto& body : chunk.methodBodies)
                writeDebugInfo(writer, *body);
        }

        bool readDebugInfo(Reader& reader, BytecodeChunk& chunk) {
            for (auto& instr : chunk.code) {
                uint64_t line = 0;
                uint64_t column = 0;
                uint64_t offset = 0;

                if (!reader.u64(line) || !reader.u64(column) || !reader.u64(offset))
                    return false;

                instr.loc = SourceLocation(line, column, offset);
            }

            for (auto& body : chunk.methodBodies)
                if (!readDebugInfo(reader, *body))
                    return false;

            return true;
        }

        size_t countInstructions(const BytecodeChunk& chunk) {
            size_t total = chunk.code.size();
            for (const auto& body : chunk.methodBodies)
                total += countInstructions(*body);

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
        }

        bool readLambdaMeta(Reader& reader, LambdaMeta& meta) {
            return reader.i32(meta.bodyIndex) && reader.i32(meta.argCount) && reader.i32(meta.captureCount);
        }

        bool writeChunk(Writer& writer, const BytecodeChunk& chunk) {
            writeVector<Instruction>(writer, chunk.code, [](Writer& w, const Instruction& instr) {
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

            writeVector<MethodMeta>(writer, chunk.methods, [](Writer& w, const MethodMeta& meta) {
                writeMethodMeta(w, meta);
            });
            writeVector<NativeCallMeta>(writer, chunk.nativeCalls, [](Writer& w, const NativeCallMeta& meta) {
                writeNativeCallMeta(w, meta);
            });
            writeVector<VirtualCallMeta>(writer, chunk.virtualCalls, [](Writer& w, const VirtualCallMeta& meta) {
                writeVirtualCallMeta(w, meta);
            });
            writeVector<SuperCallMeta>(writer, chunk.superCalls, [](Writer& w, const SuperCallMeta& meta) {
                writeSuperCallMeta(w, meta);
            });
            writeVector<LambdaMeta>(writer, chunk.lambdas, [](Writer& w, const LambdaMeta& meta) {
                writeLambdaMeta(w, meta);
            });

            writer.i32(chunk.slotCount);

            writer.u32(static_cast<uint32_t>(chunk.methodBodies.size()));
            for (const auto& body : chunk.methodBodies)
                if (!writeChunk(writer, *body))
                    return false;

            return true;
        }

        bool readChunk(Reader& reader, BytecodeChunk& chunk) {
            if (!(readVector<Instruction>(reader, chunk.code, [](Reader& r, Instruction& instr) -> bool {
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
                && readVector<MethodMeta>(reader, chunk.methods, [](Reader& r, MethodMeta& meta) -> bool {
                    return readMethodMeta(r, meta);
                })
                && readVector<NativeCallMeta>(reader, chunk.nativeCalls, [](Reader& r, NativeCallMeta& meta) -> bool {
                    return readNativeCallMeta(r, meta);
                })
                && readVector<VirtualCallMeta>(reader, chunk.virtualCalls, [](Reader& r, VirtualCallMeta& meta) -> bool {
                    return readVirtualCallMeta(r, meta);
                })
                && readVector<SuperCallMeta>(reader, chunk.superCalls, [](Reader& r, SuperCallMeta& meta) -> bool {
                    return readSuperCallMeta(r, meta);
                })
                && readVector<LambdaMeta>(reader, chunk.lambdas, [](Reader& r, LambdaMeta& meta) -> bool {
                    return readLambdaMeta(r, meta);
                })
                && reader.i32(chunk.slotCount)))
                return false;

            if (chunk.slotCount < 0)
                return false;

            uint32_t bodyCount = 0;
            if (!reader.u32(bodyCount))
                return false;

            chunk.methodBodies.clear();
            for (uint32_t i = 0; i < bodyCount; ++i) {
                auto body = std::make_unique<BytecodeChunk>();
                if (!readChunk(reader, *body))
                    return false;

                chunk.methodBodies.push_back(std::move(body));
            }

            return true;
        }
    }

    std::optional<std::string> BytecodeSerializer::serialize(
        const BytecodeChunk& chunk, const Header& header, std::string* bodyChecksum
    ) {
        Writer payload;
        if (!writeChunk(payload, chunk))
            return std::nullopt;

        Writer writer;

        writer.raw(MAGIC, sizeof(MAGIC));
        writer.u32(FORMAT_VERSION);
        writer.raw(header.sourceHash.data(), header.sourceHash.size());
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

    std::optional<BytecodeChunk> BytecodeSerializer::deserialize(
        const std::string& blob, const Header& expected, std::string* bodyChecksum
    ) {
        Reader reader(blob);

        char magic[sizeof(MAGIC)] = {};
        uint32_t version = 0;
        if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
            return std::nullopt;

        if (!reader.u32(version) || version != FORMAT_VERSION)
            return std::nullopt;

        std::string sourceHash(expected.sourceHash.size(), '\0');
        if (sourceHash.empty() || !reader.raw(sourceHash.data(), sourceHash.size()) || sourceHash != expected.sourceHash)
            return std::nullopt;

        uint32_t importCount = 0;
        if (!reader.u32(importCount) || importCount != expected.importHashes.size())
            return std::nullopt;

        for (const auto& expectedHash : expected.importHashes) {
            std::string hash(expectedHash.size(), '\0');
            if (hash.empty() || !reader.raw(hash.data(), hash.size()) || hash != expectedHash)
                return std::nullopt;
        }

        std::string checksum(kDigestSize, '\0');
        if (!reader.raw(checksum.data(), checksum.size()))
            return std::nullopt;

        const size_t bodyOffset = reader.position();
        const std::string body = blob.substr(bodyOffset);
        if (utils::Sha256::compute(body) != checksum)
            return std::nullopt;

        if (bodyChecksum)
            *bodyChecksum = checksum;

        BytecodeChunk chunk;
        if (!readChunk(reader, chunk) || !reader.done())
            return std::nullopt;

        return chunk;
    }

    std::optional<std::string> BytecodeSerializer::serializeDebugInfo(
        const BytecodeChunk& chunk, const std::string& bodyChecksum
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

    bool BytecodeSerializer::attachDebugInfo(
        BytecodeChunk& chunk, const std::string& debugBlob, const std::string& bodyChecksum
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
