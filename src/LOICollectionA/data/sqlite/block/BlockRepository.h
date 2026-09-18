#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/Ownership.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

class BlockRepository {
public:
    using ColumnCallback = std::function<void(std::string_view)>;
    using CreateCallback = std::function<void(ColumnCallback)>;

    enum class FindCondition {
        AND,
        OR
    };

    // 块模型下的原子写事务。对多行/多表的一组 set/del 在提交时一次性原子生效，
    // 镜像历史 SQLiteStorageTransaction 语义。可移动、按值持有，随提交/回滚析构。
    class WriteTransaction {
    public:
        WriteTransaction(WriteTransaction const&) = delete;
        WriteTransaction& operator=(WriteTransaction const&) = delete;
        WriteTransaction(WriteTransaction&&) = default;
        WriteTransaction& operator=(WriteTransaction&&) = delete;

        [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<WriteTransaction> create(
            BlockRepository& repo);

        [[nodiscard]] ll::Expected<bool> commit();
        [[nodiscard]] ll::Expected<bool> rollback();

        // 返回事务句柄，供事务版本的 set/del/get/has/list 传入。
        [[nodiscard]] WriteBatch& connection() const noexcept { return *mBatch; }

    private:
        explicit WriteTransaction(std::shared_ptr<WriteBatch> batch) : mBatch(std::move(batch)) {}

        std::shared_ptr<WriteBatch> mBatch;
    };

    LOICOLLECTION_A_API ~BlockRepository();

    BlockRepository(BlockRepository const&) = delete;
    BlockRepository& operator=(BlockRepository const&) = delete;

    // 从数据库路径构造（自建连接池与底层块存储，默认写连接数=4）
    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<BlockRepository>> open(
        std::string dbPath, size_t connections = 4);

    // 从调用方持有的已有块存储构造（共享同一底层库）
    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<BlockRepository>> fromStore(
        std::shared_ptr<BlockStore> store);

    [[nodiscard]] ll::Expected<void> exec(std::string_view sql);

    [[nodiscard]] ll::Expected<void> create(std::string_view table, CreateCallback callback);
    [[nodiscard]] ll::Expected<void> remove(std::string_view table);

    [[nodiscard]] ll::Expected<void> set(
        std::string_view table, std::string_view key, std::string_view column, std::string_view value);
    [[nodiscard]] ll::Expected<void> set(
        std::string_view table, std::string_view key, std::unordered_map<std::string, std::string> values);
    [[nodiscard]] ll::Expected<void> del(std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<void> del(std::string_view table, std::vector<std::string> keys);

    // 事务上下文版本的写操作：写入被暂存到 tx，随 WriteTransaction::commit() 原子生效。
    [[nodiscard]] ll::Expected<void> set(
        WriteBatch& tx, std::string_view table, std::string_view key,
        std::string_view column, std::string_view value);
    [[nodiscard]] ll::Expected<void> set(
        WriteBatch& tx, std::string_view table, std::string_view key,
        std::unordered_map<std::string, std::string> values);
    [[nodiscard]] ll::Expected<void> del(WriteBatch& tx, std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<void> del(WriteBatch& tx, std::string_view table, std::vector<std::string> keys);

    // 事务上下文版本的读取：读取已提交数据（历史 SQLite 在同一连接上读取未提交写入；
    // 现有调用点均为“先读后写同 key”，块模型下用已提交快照即可得到等价结果）。
    [[nodiscard]] ll::Expected<std::unordered_map<std::string, std::string>> get(
        WriteBatch& tx, std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<std::string> get(
        WriteBatch& tx, std::string_view table, std::string_view key,
        std::string_view column, std::string_view defaultValue = "");
    [[nodiscard]] ll::Expected<bool> has(WriteBatch& tx, std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<std::vector<std::string>> list(WriteBatch& tx, std::string_view table);

    [[nodiscard]] ll::Expected<bool> has(std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<bool> has(std::string_view table);

    [[nodiscard]] ll::Expected<std::unordered_map<std::string, std::string>> get(
        std::string_view table, std::string_view key);
    [[nodiscard]] ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> get(
        std::string_view table, std::vector<std::string> keys);
    [[nodiscard]] ll::Expected<std::string> get(
        std::string_view table, std::string_view key, std::string_view column, std::string_view defaultValue = "");

    [[nodiscard]] ll::Expected<std::string> find(
        std::string_view table, std::vector<std::pair<std::string, std::string>> conditions,
        std::string_view defaultValue = "", FindCondition match = FindCondition::AND);
    [[nodiscard]] ll::Expected<std::vector<std::string>> find(
        std::string_view table, std::vector<std::pair<std::string, std::string>> conditions,
        FindCondition match = FindCondition::AND);
    [[nodiscard]] ll::Expected<std::vector<std::string>> find(
        std::string_view table, std::string_view column, std::vector<std::pair<std::string, std::string>> conditions,
        FindCondition match = FindCondition::AND);

    [[nodiscard]] ll::Expected<std::vector<std::string>> list(std::string_view table);
    [[nodiscard]] ll::Expected<std::vector<std::string>> list();
    [[nodiscard]] ll::Expected<std::vector<std::string>> columns(std::string_view table);

private:
    explicit BlockRepository(std::shared_ptr<BlockStore> store);

    [[nodiscard]] ll::Expected<BlockId> ensureRoot(std::string_view table);
    [[nodiscard]] ll::Expected<std::unordered_map<std::string, std::string>> readRow(
        std::string_view table, std::string_view key);

    static ll::Expected<std::string> encodeRow(
        observer<BlockStore> store, std::unordered_map<std::string, std::string> const& values);
    static ll::Expected<std::unordered_map<std::string, std::string>> decodeRow(
        observer<BlockStore> store, std::vector<std::byte> const& payload);

    std::shared_ptr<BlockStore> mStore = nullptr;
    std::shared_ptr<ConnectionPool> mPool = nullptr;
};