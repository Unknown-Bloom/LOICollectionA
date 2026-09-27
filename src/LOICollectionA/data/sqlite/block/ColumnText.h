#pragma once

#include <cstddef>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

[[nodiscard]] inline std::string columnToString(SQLite::Column const& column) {
    auto const* text = column.getText();
    auto const  size = static_cast<std::size_t>(column.getBytes());
    return size != 0 ? std::string(text, size) : std::string{};
}
