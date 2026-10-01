#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include <ll/api/Expected.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>

#include <mc/server/ServerPlayer.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/NetworkSystem.h>
#include <mc/network/MinecraftPackets.h>
#include <mc/network/MinecraftPacketIds.h>
#include <mc/network/ServerNetworkHandler.h>
#include <mc/network/PacketHandlerDispatcherInstance.h>
#include <mc/network/packet/ModalFormRequestPacket.h>
#include <mc/network/packet/ModalFormResponsePacket.h>

#include "LOICollectionA/include/server/Events/server/NetworkPacketEvent.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/ProtableTool/OrderedUI.h"

namespace LOICollection::server::ProtableTool {
    struct OrderedUI::Impl {
        std::unordered_map<uint64_t, uint> mFormResponse;
        std::unordered_map<uint64_t, std::unordered_map<uint, std::string>> mFormLists;

        std::atomic<bool> mRegistered{ false };

        bool ModuleEnabled = false;

        ll::event::ListenerPtr ModalFormRequestEventListener;
        ll::event::ListenerPtr ModalFormResponseEventListener;
        ll::event::ListenerPtr PlayerDisconnectEventListener;
    };

    OrderedUI::OrderedUI() : mImpl(std::make_unique<Impl>()) {};
    OrderedUI::~OrderedUI() = default;

    std::shared_ptr<OrderedUI> OrderedUI::getShared() {
        static auto instance = std::shared_ptr<OrderedUI>(new OrderedUI());
        return instance;
    }

    void OrderedUI::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->ModalFormRequestEventListener = eventBus.emplaceListener<LOICollection::server::Events::NetworkPacketBeforeEvent>([this](LOICollection::server::Events::NetworkPacketBeforeEvent& event) mutable -> void {
            if (event.getPacket().getId() != MinecraftPacketIds::ShowModalForm)
                return;

            auto& packet = static_cast<const ModalFormRequestPacket&>(event.getPacket());

            uint64_t mIdentifierHash = event.getNetworkIdentifier().getHash();
            if (!packet.mFormId || packet.mFormJSON.empty())
                return;

            if (this->mImpl->mFormResponse.contains(mIdentifierHash)) {
                this->mImpl->mFormLists[mIdentifierHash].insert({ packet.mFormId, packet.mFormJSON });

                event.cancel();
                return;
            }

            this->mImpl->mFormResponse[mIdentifierHash] = packet.mFormId;
        });
        this->mImpl->ModalFormResponseEventListener = eventBus.emplaceListener<LOICollection::server::Events::NetworkPacketBeforeEvent>([this](LOICollection::server::Events::NetworkPacketBeforeEvent& event) mutable -> void {
            if (event.getPacket().getId() != MinecraftPacketIds::ModalFormResponse)
                return;

            auto& packet = static_cast<const ModalFormResponsePacket&>(event.getPacket());

            uint64_t mIdentifierHash = event.getNetworkIdentifier().getHash();
            if (!this->mImpl->mFormResponse.contains(mIdentifierHash))
                return;

            if (packet.mFormId != this->mImpl->mFormResponse[mIdentifierHash]) {
                event.cancel();
                return;
            }

            this->mImpl->mFormResponse.erase(mIdentifierHash);

            if (!this->mImpl->mFormLists.contains(mIdentifierHash))
                return;

            if (this->mImpl->mFormLists[mIdentifierHash].empty()) {
                this->mImpl->mFormLists.erase(mIdentifierHash);

                return;
            }
            
            auto it = std::prev(this->mImpl->mFormLists[mIdentifierHash].end());

            auto request = static_pointer_cast<ModalFormRequestPacket>(MinecraftPackets::createPacket(MinecraftPacketIds::ShowModalForm));

            request->mFormId = it->first;
            request->mFormJSON = it->second;
            request->sendToClient(event.getNetworkIdentifier(), event.getSubClientId());
            
            this->mImpl->mFormLists[mIdentifierHash].erase(it);
        });
        this->mImpl->PlayerDisconnectEventListener = eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            uint64_t mIdentifierHash = event.self().getNetworkIdentifier().getHash();
            if (this->mImpl->mFormResponse.contains(mIdentifierHash))
                this->mImpl->mFormResponse.erase(mIdentifierHash);

            if (this->mImpl->mFormLists.contains(mIdentifierHash))
                this->mImpl->mFormLists.erase(mIdentifierHash);
        });
    }

    void OrderedUI::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->ModalFormRequestEventListener);
        eventBus.removeListener(this->mImpl->ModalFormResponseEventListener);
        eventBus.removeListener(this->mImpl->PlayerDisconnectEventListener);
    }

    std::string OrderedUI::getName() {
        return "OrderedUI";
    }

    modules::ModulePriority OrderedUI::getPriority() {
        return modules::ModulePriority::Normal;
    }

    ll::Expected<bool> OrderedUI::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.ProtableTool.OrderedUI)
            return false;

        this->mImpl->ModuleEnabled = true;

        return true;
    }

    ll::Expected<bool> OrderedUI::unload() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->ModuleEnabled = false;

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> OrderedUI::registry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->listenEvent();

        this->mImpl->mRegistered.store(true, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> OrderedUI::unregistry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->unlistenEvent();

        this->mImpl->mRegistered.store(false, std::memory_order_release);

        return true;
    }
}
