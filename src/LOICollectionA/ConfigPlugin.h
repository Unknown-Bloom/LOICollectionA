#pragma once

#include <string>
#include <vector>

#include <ll/api/Expected.h>

namespace Config {
    struct C_Blacklist {
        bool ModuleEnabled = false;
        bool BroadcastMessage = true;
    };

    struct C_Menu {
        bool ModuleEnabled = false;
        std::string MenuItemId = "minecraft:clock";
        std::string EntranceKey = "main";
        std::string GuiPath = "menu.lcui";
    };

    struct C_Shop {
        bool ModuleEnabled = false;
        std::string GuiPath = "shop.lcui";
    };

    struct C_Tpa {
        bool ModuleEnabled = false;
        std::string TargetScoreboard = "money";
        int BlacklistUpload = 10;
        int RequestRequired = 100;
        int RequestTimeout = 60;
        int RequestUpload = 5;
    };

    struct C_Monitor {
        bool ModuleEnabled = false;

        struct C_BelowName {
            bool ModuleEnabled = true;
            int RefreshInterval = 20;
            int RefreshDisplayInterval = 100;
            std::vector<std::vector<std::string>> Pages{{ "{player}" }};
        } BelowName;

        struct C_ServerToast {
            bool ModuleEnabled = true;
            struct C_Messager {
                bool join = true;
                bool leave = true;
            } Messager;
        } ServerToast;

        struct C_ChangeScore {
            bool ModuleEnabled = true;
            std::vector<std::string> ScoreboardLists{};
        } ChangeScore;

        struct C_DisableCommand {
            bool ModuleEnabled = true;
            std::vector<std::string> CommandLists{};
        } DisableCommand;
        
        struct C_DynamicMotd {
            bool ModuleEnabled = true;
            int RefreshInterval = 200;
            std::vector<std::string> Pages{ "'Players: ' + {server_player_online} + '/' + {server_player_max}" };
        } DynamicMotd;

        struct C_Sidebar {
            bool ModuleEnabled = true;
            int RefreshInterval = 20;
        
            std::vector<std::string> Titles{ "'Title'" };
            std::vector<std::vector<std::string>> Pages{ { "'Content'" } };
        } Sidebar;
    };

    struct C_Pvp {
        bool ModuleEnabled = false;
        struct C_ExtraListener {
            bool onActorHurt = true;
            bool onSplashPotion = true;
            bool onProjectileHit = true;
        } ExtraListener;
    };

    struct C_Wallet {
        bool ModuleEnabled = false;
        std::string TargetScoreboard = "money";
        double ExchangeRate = 0.1;
        int RedEnvelopeTimeout = 60;

        bool WalletHistoryEnabled = true;
        int WalletHistoryRetentionDays = 90;

        int TransferMinAmount = 1;
        int TransferDailyLimit = 0;
        int TransferConfirmThreshold = 1000;
        int TransferCooldownSeconds = 0;

        int WealthTopSize = 50;
        int WealthRefreshMinutes = 10;

        bool WalletBankEnabled = true;
        int WalletBankMinDeposit = 10;
        double WalletBankDailyRate = 0.0005;
        double WalletInterestTaxRate = 0.1;
        bool WalletInterestFromPool = true;

        bool RedEnvelopeTargetedEnabled = true;
        int RedEnvelopeMaxCount = 100;
    };

    struct C_Chat {
        bool ModuleEnabled = false;
        std::string FormatText = "<{player}> ${0}";
        int BlacklistUpload = 10;
    };

    struct C_Market {
        bool ModuleEnabled = false;
        std::string TargetScoreboard = "money";
        int MaximumUpload = 20;
        int BlacklistUpload = 10;
        int TradeRequestTimeout = 60;
        int TradeTimeout = 90;
        std::vector<std::string> ProhibitedItems{};

        bool StoreEnabled = true;
        bool StoreReviewEnabled = true;
        int StoreMaximumItems = 20;
        int StoreCreationCost = 0;
        double StoreSalesWeight = 0.5;
        double StoreVolumeWeight = 0.3;
        double StoreRatingWeight = 0.35;
        double StoreBadReviewPenalty = 3.0;
        double StoreColdStartWeight = 0.5;
        double StoreRatingSmoothing = 15.0;
        int StoreColdStartDays = 7;
        int StoreTransactionWindowDays = 30;
        int StoreRatingWindowDays = 180;
        int StoreRiskWindowDays = 30;
        int StoreRankRefreshMinutes = 60;

        bool StoreQuoteEnabled = true;
        int StoreQuoteRefreshMinutes = 30;
        double StoreTransactionTaxRate = 0.0;
        double StorePriceCeilingRatio = 0.0;

        bool StoreWantedEnabled = true;
        int StoreWantedMaxPerPlayer = 5;
        int StoreWantedExpireDays = 7;
        bool StorePartialBuyEnabled = true;

        bool StoreAuctionEnabled = true;
        int StoreAuctionMinDurationMinutes = 30;
        int StoreAuctionMaxDurationHours = 72;
        double StoreAuctionMinBidIncrement = 1.05;
        int StoreAuctionAntiSnipeSeconds = 0;
        int StoreRepeatTradeLimit = 3;
        double StorePriceOutlierRatio = 0.0;
    };

    struct C_BehaviorEvent {
        bool ModuleEnabled = false;
        int OrganizeDatabaseInterval = 144;
        int CleanThresholdEvent = 10000;
        int CleanDatabaseInterval = 1;
        int RefreshIntervalInMinutes = 5;
        int SingleBacktrackingQuantity = 2000;

        struct C_Events {
            struct C_onPlayerConnect {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerConnect;
            struct C_onPlayerDisconnect {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerDisconnect;
            struct C_onPlayerChat {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerChat;
            struct C_onPlayerAddExperience {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerAddExperience;
            struct C_onPlayerAttack {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerAttack;
            struct C_onPlayerChangePerm {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerChangePerm;
            struct C_onPlayerDestroyBlock {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerDestroyBlock;
            struct C_onPlayerPlaceBlock {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerPlaceBlock;
            struct C_onPlayerDie {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerDie;
            struct C_onPlayerPickUpItem {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerPickUpItem;
            struct C_onPlayerRespawn {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerRespawn;
            struct C_onPlayerUseItem {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerUseItem;
            struct C_onPlayerContainerInteract {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onPlayerContainerInteract;
            struct C_onBlockExplode {
                bool ModuleEnabled = true;
                bool RecordDatabase = true;
                bool OutputConsole = true;
            } onBlockExplode;
        } Events;
    };

    struct C_Statistics {
        bool ModuleEnabled = false;
        int RefreshIntervalInMinutes = 5;
        int RankingPlayerCount = 100;

        struct C_DatabaseInfo {
            bool OnlineTime = true;
            bool Kill = true;
            bool Death = true;
            bool Place = true;
            bool Destroy = true;
            bool Respawn = true;
            bool Join = true;
        } DatabaseInfo;
    };

    struct C_ServerPlugins {
        C_Blacklist Blacklist;

        bool Mute = false;
        bool Cdk = false;

        C_Menu Menu;
        C_Tpa Tpa;
        
        C_Shop Shop;

        C_Monitor Monitor;
        C_Pvp Pvp;
        C_Wallet Wallet;
        C_Chat Chat;

        bool Notice = false;

        C_Market Market;
        C_BehaviorEvent BehaviorEvent;
        C_Statistics Statistics;
    };

    struct C_BasicHook {
        bool ModuleEnabled = false;
        std::string FakeSeed = "$random";
    };

    struct C_ServerProtableTool {
        C_BasicHook BasicHook;

        int RedStone = 0;
        bool OrderedUI = false;
    };

    struct C_ServerConfig {
        C_ServerPlugins Plugins;
        C_ServerProtableTool ProtableTool;
    };

    struct C_Config {
        int version = 0;
        std::string ConsoleLanguage = "system";
        
        C_ServerConfig ServerConfig;
    };
    
    std::string GetVersion();

    void SynchronousPluginConfigVersion(C_Config& config);

    ll::Expected<void> SynchronousPluginConfigType(C_Config& config, const std::string& path);
}
