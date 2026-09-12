#include <charconv>
#include <string_view>

#include "LOICollectionA/frontend/lsp/Protocol.h"

namespace LOICollection::frontend::lsp {
    void to_json(nlohmann::ordered_json& j, const Position& value) {
        j = nlohmann::ordered_json{ { "line", value.line }, { "character", value.character } };
    }

    void from_json(const nlohmann::ordered_json& j, Position& value) {
        value.line = j.value("line", std::size_t{ 0 });
        value.character = j.value("character", std::size_t{ 0 });
    }

    void to_json(nlohmann::ordered_json& j, const Range& value) {
        j = nlohmann::ordered_json{ { "start", value.start }, { "end", value.end } };
    }

    void from_json(const nlohmann::ordered_json& j, Range& value) {
        if (j.contains("start"))
            value.start = j.at("start").get<Position>();
        if (j.contains("end"))
            value.end = j.at("end").get<Position>();
    }

    void to_json(nlohmann::ordered_json& j, const Location& value) {
        j = nlohmann::ordered_json{ { "uri", value.uri }, { "range", value.range } };
    }

    void to_json(nlohmann::ordered_json& j, const Diagnostic& value) {
        j = nlohmann::ordered_json{
            { "range", value.range },
            { "severity", static_cast<int>(value.severity) },
            { "message", value.message },
        };
    }

    void to_json(nlohmann::ordered_json& j, const CompletionItem& value) {
        j = nlohmann::ordered_json{
            { "label", value.label },
            { "kind", static_cast<int>(value.kind) },
            { "detail", value.detail },
        };
    }

    void to_json(nlohmann::ordered_json& j, const Hover& value) {
        j = nlohmann::ordered_json{ { "contents", value.contents } };
        if (value.range)
            j["range"] = *value.range;
    }

    nlohmann::ordered_json makeResponse(const nlohmann::ordered_json& id, nlohmann::ordered_json result) {
        return nlohmann::ordered_json{
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", std::move(result) },
        };
    }

    nlohmann::ordered_json makeError(const nlohmann::ordered_json& id, int code, std::string message) {
        return nlohmann::ordered_json{
            { "jsonrpc", "2.0" },
            { "id", id },
            { "error", nlohmann::ordered_json{ { "code", code }, { "message", std::move(message) } } },
        };
    }

    nlohmann::ordered_json makeNotification(std::string method, nlohmann::ordered_json params) {
        return nlohmann::ordered_json{
            { "jsonrpc", "2.0" },
            { "method", std::move(method) },
            { "params", std::move(params) },
        };
    }

    std::string encodeMessage(const nlohmann::ordered_json& message) {
        const std::string body = message.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);

        std::string result = "Content-Length: ";
        result += std::to_string(body.size());
        result += "\r\n\r\n";
        result += body;

        return result;
    }

    bool decodeMessage(std::string& buffer, nlohmann::ordered_json& message) {
        constexpr std::string_view kSeparator = "\r\n\r\n";
        constexpr std::string_view kLengthField = "Content-Length:";
        constexpr std::size_t kMaxPending = 64u << 20;

        if (buffer.size() > kMaxPending) {
            buffer.clear();
            return false;
        }

        const auto headerEnd = buffer.find(kSeparator);
        if (headerEnd == std::string::npos)
            return false;

        const std::string_view header(buffer.data(), headerEnd);
        std::size_t length = 0;

        if (const auto field = header.find(kLengthField); field != std::string_view::npos) {
            const auto valueStart = header.find_first_not_of(' ', field + kLengthField.size());
            if (valueStart != std::string_view::npos) {
                const auto valueEnd = header.find_first_of("\r\n", valueStart);
                const std::string_view digits =
                    header.substr(valueStart, valueEnd == std::string_view::npos
                        ? std::string_view::npos
                        : valueEnd - valueStart);

                std::from_chars(digits.data(), digits.data() + digits.size(), length);
            }
        }

        const std::size_t bodyStart = headerEnd + kSeparator.size();
        if (buffer.size() - bodyStart < length)
            return false;

        const std::string body = buffer.substr(bodyStart, length);
        buffer.erase(0, bodyStart + length);

        message = nlohmann::ordered_json::parse(body, nullptr, false);

        return !message.is_discarded();
    }
}
