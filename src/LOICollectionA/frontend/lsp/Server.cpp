#include <atomic>
#include <charconv>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <asio.hpp>

#include "LOICollectionA/frontend/lsp/LanguageServer.h"
#include "LOICollectionA/frontend/lsp/Protocol.h"

#include "LOICollectionA/frontend/lsp/Server.h"

namespace LOICollection::frontend::lsp::detail {
    constexpr std::size_t kMaxHeaderBytes = 4096;
    constexpr std::size_t kMaxBodyBytes = 4 << 20;

    bool parseContentLength(std::string_view header, std::size_t& length) {
        const auto field = header.find("Content-Length:");
        if (field == std::string_view::npos)
            return false;

        const auto start = header.find_first_not_of(" \t", field + 15);
        if (start == std::string_view::npos)
            return false;

        const auto end = header.find_first_of("\r\n", start);
        const std::string_view digits = header.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start
        );

        std::size_t parsed = 0;
        const auto [ptr, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        if (error != std::errc{} || ptr != digits.data() + digits.size()
            || parsed == 0 || parsed > kMaxBodyBytes)
            return false;

        length = parsed;
        return true;
    }
}

namespace LOICollection::frontend::lsp {
    std::error_code LspServer::start(std::uint16_t port) {
        asio::error_code ec;
        const asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::loopback(), port);

        ec = acceptor_.open(endpoint.protocol(), ec);
        if (!ec) ec = acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (!ec) ec = acceptor_.bind(endpoint, ec);
        if (!ec) ec = acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) return ec;

        running_.store(true);
        this->acceptLoop();
        worker_ = std::thread([this] { io_.run(); });

        return ec;
    }

    void LspServer::acceptLoop() {
        acceptor_.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
            if (ec || !running_.load())
                return;

            auto connection = std::make_shared<asio::ip::tcp::socket>(std::move(socket));
            {
                std::lock_guard<std::mutex> lock(this->socketMutex_);
                this->activeSocket_ = connection;
            }

            this->serve(connection);

            {
                std::lock_guard<std::mutex> lock(this->socketMutex_);
                if (this->activeSocket_ == connection)
                    this->activeSocket_.reset();
            }

            if (running_.load())
                this->acceptLoop();
        });
    }

    void LspServer::serve(const std::shared_ptr<asio::ip::tcp::socket>& socket) {
        asio::error_code ec;
        char byte = 0;

        auto readHeader = [&]() -> std::string {
            std::string header;
            header.reserve(256);

            while (header.size() < detail::kMaxHeaderBytes) {
                asio::read(*socket, asio::buffer(&byte, 1), ec);
                if (ec)
                    return {};

                header.push_back(byte);
                if (header.size() >= 4 && header.compare(header.size() - 4, 4, "\r\n\r\n") == 0)
                    return header;
            }

            ec = asio::error::message_size;
            return {};
        };

        while (running_.load()) {
            const std::string header = readHeader();
            if (ec || header.empty())
                break;

            std::size_t length = 0;
            if (!detail::parseContentLength(header, length))
                break;

            std::string body(length, '\0');
            asio::read(*socket, asio::buffer(body.data(), length), ec);
            if (ec)
                break;

            std::string response;
            try {
                response = engine_.handle(header + body);
            } catch (const std::exception& e) {
                response = encodeMessage(makeError(
                    nlohmann::ordered_json(nullptr), -32603, std::string("Internal error: ") + e.what()));
            } catch (...) {
                response = encodeMessage(makeError(
                    nlohmann::ordered_json(nullptr), -32603, "Internal error"));
            }

            if (response.empty())
                continue;

            asio::write(*socket, asio::buffer(response), ec);
            if (ec)
                break;
        }
    }

    void LspServer::stop() {
        running_.store(false);

        {
            std::lock_guard<std::mutex> lock(this->socketMutex_);
            if (this->activeSocket_) {
                asio::error_code ignored;
                this->activeSocket_->close(ignored);
            }
        }

        asio::error_code ec;
        ec = acceptor_.close(ec);
        io_.stop();

        if (worker_.joinable())
            worker_.join();
    }
}
