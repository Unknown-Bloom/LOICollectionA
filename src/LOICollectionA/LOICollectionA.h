#pragma once

#include <memory>

#include <ll/api/Mod/NativeMod.h>

#include "LOICollectionA/ConfigPlugin.h"
#include "LOICollectionA/frontend/lsp/LanguageServer.h"

namespace LOICollection::frontend::lsp {
    class LspServer;
}

namespace LOICollection {
    class A {
    public:
        static A& getInstance();

        A();
        ~A();

        [[nodiscard]] ll::mod::NativeMod& getSelf() const { 
            return mSelf;
        }

        bool load();
        bool unload();
        bool enable();
        bool disable();

    private:
        ll::mod::NativeMod& mSelf;

        ::Config::C_Config config;
        frontend::lsp::LanguageServer lspEngine_;
        std::unique_ptr<frontend::lsp::LspServer> lspServer_;
    };
}
