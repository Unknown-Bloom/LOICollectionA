#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "LOICollectionA/frontend/lsp/LanguageServer.h"
#include "LOICollectionA/frontend/lsp/Protocol.h"
#include "LOICollectionA/frontend/lsp/Server.h"

namespace LOICollection::frontend::lsp {
    namespace {
        constexpr std::uint16_t kPort = 29517;

        nlohmann::ordered_json request(int id, const std::string& method, const nlohmann::ordered_json& params) {
            return nlohmann::ordered_json{
                { "jsonrpc", "2.0" },
                { "id", id },
                { "method", method },
                { "params", params },
            };
        }

        std::string frame(const nlohmann::ordered_json& message) {
            const std::string body = message.dump();
            return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        }

        std::string readFramed(int fd) {
            std::string buf;
            char c = 0;

            while (buf.size() < 4096) {
                if (::recv(fd, &c, 1, 0) <= 0)
                    return {};
                buf.push_back(c);
                if (buf.size() >= 4 && buf.compare(buf.size() - 4, 4, "\r\n\r\n") == 0)
                    break;
            }

            std::size_t length = 0;
            if (const auto pos = buf.find("Content-Length:"); pos != std::string::npos) {
                const auto start = buf.find_first_not_of(' ', pos + 15);
                const auto end = buf.find_first_of("\r\n", start);
                std::from_chars(buf.data() + start, buf.data() + end, length);
            }

            const std::size_t total = buf.size() + length;
            while (buf.size() < total) {
                char tmp[1024];
                const auto n = ::recv(fd, tmp, sizeof(tmp), 0);
                if (n <= 0)
                    break;
                buf.append(tmp, static_cast<std::size_t>(n));
            }

            return buf;
        }
    }

    TEST(LspServerTest, HandshakeOverLoopback) {
        LanguageServer engine;
        LspServer server(engine);

        ASSERT_FALSE(server.start(kPort));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

        const std::string initialize = frame(request(1, "initialize", nlohmann::ordered_json::object()));
        ASSERT_EQ(::send(fd, initialize.data(), initialize.size(), 0),
                  static_cast<ssize_t>(initialize.size()));

        std::string response = readFramed(fd);
        nlohmann::ordered_json message;
        ASSERT_TRUE(decodeMessage(response, message)) << response;
        EXPECT_EQ(message.value("id", 0), 1);
        ASSERT_TRUE(message.contains("result"));
        EXPECT_TRUE(message.at("result").contains("capabilities"));

        const std::string unknown = frame(request(2, "textDocument/unknown", nlohmann::ordered_json::object()));
        ASSERT_EQ(::send(fd, unknown.data(), unknown.size(), 0),
                  static_cast<ssize_t>(unknown.size()));

        response = readFramed(fd);
        message = nlohmann::ordered_json{};
        ASSERT_TRUE(decodeMessage(response, message)) << response;
        EXPECT_EQ(message.value("id", 0), 2);
        EXPECT_TRUE(message.contains("error"));
        EXPECT_EQ(message.at("error").value("code", 0), -32601);

        ::close(fd);
        server.stop();
    }
}
