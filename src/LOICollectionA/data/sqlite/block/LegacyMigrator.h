#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <ll/api/Expected.h>

namespace legacy {

struct LegacyRecord {
    std::string key;
    std::unordered_map<std::string, std::string> columns;
};

struct LegacyTable {
    std::string name;
    std::vector<LegacyRecord> records;
};

struct LegacyDb {
    bool empty = true;
    std::vector<LegacyTable> tables;
};

// 检测 dbPath 处的旧版 SQLiteStorage 数据库并归档。
// 判定规则：
//   - 文件不存在                -> 无需迁移（empty == true）
//   - 已含块存储 schema(block)  -> 已是新格式（empty == true）
//   - 存在但没有任何用户表      -> 空文件，无需迁移（empty == true）
//   - 否则视为旧库：
//       1) 将全部旧行读入内存（仅保留用户列，剔除 key/created_at/updated_at）
//       2) 用 VACUUM INTO 把原库归档为 <dbPath>.<时间戳>.legacy（现场保留）
//       3) 删除原始库及其 -wal/-shm，等待上层以新块格式重开
// 返回值：捕获到的旧数据；empty==false 时调用方应在块存储就绪后回放。
[[nodiscard]] ll::Expected<LegacyDb> archiveLegacy(std::string dbPath);

} // namespace legacy