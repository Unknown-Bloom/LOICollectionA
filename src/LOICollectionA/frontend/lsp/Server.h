#pragma once

#define ASIO_STANDALONE

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "LOICollectionA/frontend/lsp/LanguageServer.h"

namespace LOICollection::frontend::lsp {
    class LspServer {
    public:
        explicit LspServer(LanguageServer& engine) : engine_(engine) {}

        LspServer(const LspServer&) = delete;
        LspServer& operator=(const LspServer&) = delete;

        std::error_code start(std::uint16_t port);
        void stop();

        bool running() const { return running_.load(); }

    private:
        void acceptLoop();
        void serve(asio::ip::tcp::socket socket);

        LanguageServer& engine_;
        asio::io_context io_;
        asio::ip::tcp::acceptor acceptor_{ io_ };
        std::thread worker_;
        std::atomic<bool> running_{ false };
    };
}
