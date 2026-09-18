#include "LOICollectionA/data/sqlite/block/LegacyMigrator.h"

#include <chrono>
#include <filesystem>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

namespace legacy {

namespace {

    std::string quote(std::string_view ident) {
        std::string out;
        out.reserve(ident.size() + 2);
        out.push_back('"');
        for (char c : ident) {
            out.push_back(c);
            if (c == '"')
                out.push_back('"');
        }
        out.push_back('"');
        return out;
    }

    std::vector<std::string> tableNames(SQLite::Database& db) {
        std::vector<std::string> names;
        SQLite::Statement stmt(db, "SELECT name FROM sqlite_master WHERE type = 'table'");
        while (stmt.executeStep()) {
            names.emplace_back(stmt.getColumn(0).getString());
        }
        return names;
    }

    struct ColumnInfo {
        std::string name;
        bool isPrimaryKey = false;
    };

    std::vector<ColumnInfo> tableColumns(SQLite::Database& db, std::string_view table) {
        std::vector<ColumnInfo> columns;
        SQLite::Statement stmt(db,
            std::string("PRAGMA table_info(") + quote(table) + ")");
        while (stmt.executeStep()) {
            ColumnInfo info;
            info.name = stmt.getColumn(1).getString();
            info.isPrimaryKey = stmt.getColumn(5).getInt() != 0;
            columns.push_back(std::move(info));
        }
        return columns;
    }

    // 旧版 SQLiteStorage 建表固定带这三列，其中 key 为主键；created_at/updated_at
    // 由 SQLite 维护，新块模型用自身的 created/updated 时间戳替代。
    bool isMetaColumn(std::string_view name) {
        return name == "created_at" || name == "updated_at";
    }

    std::string timestamp() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    void removeLegacyFiles(std::string const& dbPath) {
        std::error_code ec;
        std::filesystem::remove(dbPath, ec);
        std::filesystem::remove(dbPath + "-wal", ec);
        std::filesystem::remove(dbPath + "-shm", ec);
    }

} // namespace

ll::Expected<LegacyDb> archiveLegacy(std::string dbPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dbPath))
        return LegacyDb{};

    LegacyDb legacy;
    std::string archivePath = dbPath + "." + timestamp() + ".legacy";
    try {
        {
            SQLite::Database db(dbPath.c_str(),
                SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

            auto names = tableNames(db);
            bool hasBlockSchema = false;
            for (auto const& name : names) {
                if (name == "block") {
                    hasBlockSchema = true;
                    break;
                }
            }

            // 已是新块格式：无需迁移。
            if (hasBlockSchema)
                return LegacyDb{};

            for (auto const& name : names) {
                // 仅迁移顶层用户表，忽略 SQLite 内部表。
                if (name.starts_with("sqlite_"))
                    continue;

                auto columns = tableColumns(db, name);
                if (columns.empty())
                    continue;

                LegacyTable table;
                table.name = name;

                std::string sql = "SELECT ";
                for (size_t i = 0; i < columns.size(); ++i) {
                    if (i)
                        sql += ", ";
                    sql += quote(columns[i].name);
                }
                sql += " FROM " + quote(name);

                SQLite::Statement stmt(db, sql);
                while (stmt.executeStep()) {
                    LegacyRecord record;
                    for (size_t i = 0; i < columns.size(); ++i) {
                        auto const& col = columns[i];
                        auto value = stmt.getColumn(static_cast<int>(i));
                        if (col.isPrimaryKey) {
                            if (!value.isNull())
                                record.key = value.getString();
                            continue;
                        }
                        if (isMetaColumn(col.name))
                            continue;
                        if (value.isNull())
                            continue;
                        record.columns[col.name] = value.getString();
                    }

                    if (!record.key.empty() || !record.columns.empty())
                        table.records.push_back(std::move(record));
                }

                if (!table.records.empty())
                    legacy.tables.push_back(std::move(table));
            }

            // 没有任何用户数据：视为空文件，无需归档。
            if (legacy.tables.empty())
                return LegacyDb{};

            // 用 VACUUM INTO 制作一致的单文件快照作为现场归档。
            std::string escaped;
            escaped.reserve(archivePath.size());
            for (char c : archivePath) {
                escaped.push_back(c);
                if (c == '\'')
                    escaped.push_back('\'');
            }
            db.exec("VACUUM INTO '" + escaped + "'");
        } // Database 在此析构，连接关闭。

        if (!fs::exists(archivePath))
            return ll::makeStringError("legacy archive failed: " + archivePath);

    } catch (std::exception const& e) {
        return ll::makeStringError(std::string("legacy archive failed: ") + e.what());
    }

    removeLegacyFiles(dbPath);

    legacy.empty = legacy.tables.empty();
    return legacy;
}

} // namespace legacy