#include "LOICollectionA/frontend/lsp/Server.h"

namespace LOICollection::frontend::lsp {
    std::error_code LspServer::start(std::uint16_t port) {
        asio::error_code ec;
        const asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::loopback(), port);

        acceptor_.open(endpoint.protocol(), ec);
        if (!ec) acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (!ec) acceptor_.bind(endpoint, ec);
        if (!ec) acceptor_.listen(asio::socket_base::max_listen_connections, ec);
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

            this->serve(std::move(socket));
            this->acceptLoop();
        });
    }

    void LspServer::serve(asio::ip::tcp::socket socket) {
        asio::error_code ec;
        char byte = 0;

        auto readHeader = [&]() -> std::string {
            std::string header;
            header.reserve(256);

            while (header.size() < 4096) {
                asio::read(socket, asio::buffer(&byte, 1), ec);
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
            if (const auto field = header.find("Content-Length:");
                field != std::string::npos) {
                const auto start = header.find_first_not_of(' ', field + 15);
                if (start != std::string::npos) {
                    const auto end = header.find_first_of("\r\n", start);
                    std::from_chars(
                        header.data() + start,
                        end == std::string::npos ? header.data() + header.size() : header.data() + end,
                        length
                    );
                }
            }
            if (length == 0)
                break;

            std::string body(length, '\0');
            asio::read(socket, asio::buffer(body.data(), length), ec);
            if (ec)
                break;

            const std::string response = engine_.handle(header + body);
            if (response.empty())
                continue;

            asio::write(socket, asio::buffer(response), ec);
            if (ec)
                break;
        }
    }

    void LspServer::stop() {
        running_.store(false);

        asio::error_code ec;
        acceptor_.close(ec);
        io_.stop();

        if (worker_.joinable())
            worker_.join();
    }
}
