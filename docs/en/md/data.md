# Data Files

## Configuration File

Configuration files are in the `.json` format, and are generally named `config.json`  
Configuration files are usually automatically generated when the server is first started after installation, located in the `plugins/LOICollectionA/config/` directory  
The configuration items in `config.json` are only read once when the server starts. Modifications to the configuration file afterwards will only take effect after restarting the server  
When you upgrade the plugin version, newly added configuration items are automatically merged into the existing configuration file. Existing configuration items and their values are preserved, so there is no need to add them manually

> [!WARNING]
> The names of the configuration items in the configuration file must be in English, digits, or underscores. Please do not use Chinese as configuration item names, otherwise the configuration file may fail to be read properly.  
> Please do not use text editors such as Notepad that do not support the `.json` format, to avoid corrupting the structure of the configuration file.

```json
{
    "version": 90052239, // Configuration file version number, automatically generated from the plugin version number, used for configuration synchronization; modification is not recommended
    "ConsoleLanguage": "system", // Console language, where system follows the system language, zh_CN is Chinese, en_US is English
    "ServerConfig": { // Server configuration
        "Plugins": { // Built-in plugin configuration
            "Blacklist": {
                "ModuleEnabled": false, // Whether to enable the blacklist
                "BroadcastMessage": true // Whether to enable blacklist content broadcast
            },
            "Mute": false,   // Whether to enable mute
            "Cdk": false, // Whether to enable CDK
            "Menu": { // Menu configuration
                "ModuleEnabled": false, // Whether to enable the menu
                "MenuItemId": "minecraft:clock",  // Item ID used to open the menu
                "EntranceKey": "main", // Menu entrance
                "GuiPath": "menu.lcui" // Gui entry file
            },
            "Tpa": { // TPA configuration
                "ModuleEnabled": false, // Whether to enable TPA
                "TargetScoreboard": "money", // Score object used by TPA request targets
                "BlacklistUpload": 10, // Maximum number of blacklisted targets a player can upload
                "RequestRequired": 100, // Score required for a TPA request
                "RequestTimeout": 60, // TPA request timeout (in seconds)
                "RequestUpload": 5 // Maximum number of teleport requests a player can upload
            },
            "Shop": {
                "ModuleEnabled": false, // Whether to enable the shop
                "GuiPath": "shop.lcui"
            },
            "Monitor": { // Message enhancement configuration
                "ModuleEnabled": false, // Whether to enable message enhancement
                "BelowName": {
                    "ModuleEnabled": true, // Whether to enable player name display
                    "RefreshInterval": 20, // Refresh interval, in ticks (20 ticks = 1 second)
                    "RefreshDisplayInterval": 100, // Display refresh interval, in ticks
                    "Pages": [
                        [
                            "{player}" // Content displayed on each line
                        ]
                    ] // Player name display format, supports LOICollectionA API variables
                },
                "ServerToast": {
                    "ModuleEnabled": true, // Whether to enable server toast notifications
                    "Messager": {
                        "join": true, // Whether to enable the player join server notification
                        "leave": true // Whether to enable the player leave server notification
                    }
                },
                "ChangeScore": {
                    "ModuleEnabled": true, // Whether to enable Score change detection
                    "ScoreboardLists": [] // Objects for detecting Score changes (when empty, all Score changes are detected)
                },
                "DisableCommand": {
                    "ModuleEnabled": true, // Whether to enable command disable detection
                    "CommandLists": [] // List of disabled commands
                },
                "DynamicMotd": {
                    "ModuleEnabled": true, // Whether to enable the dynamic server MOTD
                    "RefreshInterval": 200, // Refresh interval, in ticks (20 ticks = 1 second)
                    "Pages": [
                        "'Players: ' + {server_player_online} + '/' + {server_player_max}" // Content displayed on each line, supports LOICollectionA API variables
                    ]
                },
                "Sidebar": {
                    "ModuleEnabled": true, // Whether to enable the sidebar
                    "RefreshInterval": 20, // Refresh interval, in ticks (20 ticks = 1 second)
                    "Titles": [ // Sidebar titles, each line is the title of the corresponding page
                        "'Title'" // Title, supports LOICollectionA API variables
                    ],
                    "Pages": [
                        [
                            "'Content'" // Content displayed on each line, supports LOICollectionA API variables
                        ] // Content of each page
                    ]
                }
            },
            "Pvp": {
                "ModuleEnabled": false, // Whether to enable PVP
                "ExtraListener": {
                    "onActorHurt": true, // Whether to enable the player damage listener
                    "onSplashPotion": true, // Whether to enable the potion effect listener
                    "onProjectileHit": true // Whether to enable the projectile hit listener
                }
            },
            "Wallet": { // Wallet configuration
                "ModuleEnabled": false, // Whether to enable the wallet
                "TargetScoreboard": "money", // Score object used by the wallet
                "ExchangeRate": 0.1, // Wallet exchange rate
                "RedEnvelopeTimeout": 60, // Red envelope timeout (in seconds)
                "WalletHistoryEnabled": true, // Whether to record the fund ledger
                "WalletHistoryRetentionDays": 90, // Ledger retention days; expired records are cleaned up
                "TransferMinAmount": 1, // Minimum transfer amount; below is rejected (0 = unrestricted)
                "TransferDailyLimit": 0, // Daily accumulated transfer cap (incl. fee), 0 = unrestricted
                "TransferConfirmThreshold": 1000, // Large transfer confirmation threshold; requires GUI confirmation when exceeded (0 = none)
                "TransferCooldownSeconds": 0 // Minimum interval between transfers (seconds), 0 = no cooldown
            },
            "Chat": { // Chat enhancement configuration
                "ModuleEnabled": false, // Whether to enable chat enhancement
                "FormatText": "<{player}> ${0}", // Chat message format, where ${0} is the player's chat content, supports LOICollectionA API variables
                "BlacklistUpload": 10 // Maximum number of blacklisted targets to upload
            },
            "Notice": false, // Whether to enable the notice board
            "Market": { // Player market configuration
                "ModuleEnabled": false, // Whether to enable the player market
                "TargetScoreboard": "money", // Score object used by the player market
                "MaximumUpload": 20, // Maximum number of items a player can upload to the market
                "BlacklistUpload": 10, // Maximum number of blacklisted targets a player can upload
                "TradeRequestTimeout": 60, // Player trade request timeout (in seconds)
                "TradeTimeout": 90, // Player trade timeout (in seconds)
                "ProhibitedItems": [], // Items prohibited from being uploaded to the player market
                "StoreEnabled": true, // Whether to enable the player store (shop) feature
                "StoreReviewEnabled": true, // Whether to enable store review functionality
                "StoreMaximumItems": 20, // Maximum number of items a single store can list
                "StoreCreationCost": 0, // Score cost required to create a store (deducted from TargetScoreboard; free when 0)
                "StoreSalesWeight": 0.5, // Store ranking weight - number of transactions in the last 30 days
                "StoreVolumeWeight": 0.3, // Store ranking weight - transaction volume in the last 30 days
                "StoreRatingWeight": 0.35, // Store ranking weight - player rating
                "StoreBadReviewPenalty": 3.0, // Store ranking penalty - bad review ratio in the last 30 days
                "StoreColdStartWeight": 0.5, // Store ranking bonus - cold start weight for new stores
                "StoreRatingSmoothing": 15.0, // Bayesian smoothing parameter for store ratings
                "StoreColdStartDays": 7, // Cold start bonus days for new stores (in days)
                "StoreTransactionWindowDays": 30, // Store transaction data statistics window (in days)
                "StoreRatingWindowDays": 180, // Store review data statistics window (in days)
                "StoreRiskWindowDays": 30, // Store risk data statistics window (in days)
                "StoreRankRefreshMinutes": 60, // Store leaderboard refresh interval (in minutes)
                "StoreQuoteEnabled": true, // Whether to enable quote aggregation (average price, volume and turnover rankings)
                "StoreQuoteRefreshMinutes": 30, // Quote refresh interval (in minutes)
                "StoreTransactionTaxRate": 0.0, // Transaction tax rate (0.0 ~ 1.0, floor(price × rate); can be overridden at runtime via /market tax)
                "StorePriceCeilingRatio": 0.0, // Price ceiling ratio (based on the 30-day average price; listings above average × ratio are rejected, 0 disables)
                "StoreWantedEnabled": true, // Whether to enable wanted orders
                "StoreWantedMaxPerPlayer": 5, // Maximum number of concurrent wanted orders per player
                "StoreWantedExpireDays": 7, // Wanted order expiry (in days; frozen prepayment is refunded on expiry)
                "StorePartialBuyEnabled": true, // Whether to enable partial fulfillment of wanted orders
                "StoreAuctionEnabled": true, // Whether to enable auctions
                "StoreAuctionMinDurationMinutes": 30, // Minimum auction duration (in minutes)
                "StoreAuctionMaxDurationHours": 72, // Maximum auction duration (in hours)
                "StoreAuctionMinBidIncrement": 1.05, // Minimum bid increment ratio (a new bid must exceed current price × ratio)
                "StoreAuctionAntiSnipeSeconds": 0, // Anti-sniping duration (in seconds; late bids extend the auction, 0 disables)
                "StoreRepeatTradeLimit": 3, // Repeat trade count limit per trade pair (excess trades are excluded from average price, anti-farming)
                "StorePriceOutlierRatio": 0.0 // Outlier price filter ratio (trades deviating beyond this multiple of the recent average are excluded, 0 disables)
            },
            "BehaviorEvent": { // Behavior event configuration
                "ModuleEnabled": false, // Whether to enable behavior events
                "OrganizeDatabaseInterval": 144, // Behavior event database cleanup threshold (in hours)
                "CleanThresholdEvent": 10000, // Behavior event cleanup threshold
                "CleanDatabaseInterval": 1, // Automatic database cleanup interval (in hours)
                "RefreshIntervalInMinutes": 5, // Behavior event recording interval (in minutes)
                "SingleBacktrackingQuantity": 2000, // Number of events per backtracking run (note: the larger the number, the more events are processed within a single tick, which may cause the server to stall until processing completes)
                "Events": { // Behavior event configuration
                    "onPlayerConnect": { // Player connect event
                        "ModuleEnabled": true, // Whether to enable this event
                        "RecordDatabase": true, // Whether to record to the database
                        "OutputConsole": true // Whether to output to the console
                    },
                    "onPlayerDisconnect": { // Player disconnect event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerChat": { // Player chat event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerAddExperience": { // Player gaining experience event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerAttack": { // Player attack event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerChangePerm": { // Player permission change event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerDestroyBlock": { // Player block destroy event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerPlaceBlock": { // Player block place event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerDie": { // Player death event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerPickUpItem": { // Player item pickup event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerRespawn": { // Player respawn event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerUseItem": { // Player item use event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerContainerInteract": { // Player container interaction event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onBlockExplode": { // Block explosion event
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    }
                }
            },
            "Statistics": {
                "ModuleEnabled": false, // Whether to enable statistics
                "RefreshIntervalInMinutes": 5, // Statistics refresh interval (in minutes)
                "RankingPlayerCount": 100, // Number of players displayed on the leaderboard
                "DatabaseInfo": { // Statistics database information (recorded after each exit)
                    "OnlineTime": true, // Whether to enable online time statistics
                    "Kill": true, // Whether to enable mob kill statistics
                    "Death": true, // Whether to enable death statistics
                    "Place": true, // Whether to enable block place statistics
                    "Destroy": true, // Whether to enable block destroy statistics
                    "Respawn": true, // Whether to enable respawn statistics
                    "Join": true // Whether to enable join server statistics
                }
            }
        },
        "ProtableTool": { // Portable tool configuration
            "BasicHook": { // Basic feature configuration
                "ModuleEnabled": false, // Whether to enable basic features
                "FakeSeed": "$random" // Fake seed configuration
            },
            "RedStone": 0, // Redstone high-frequency detection, where the value is the frequency per second (disabled when 0)
            "OrderedUI": false // Whether to enable ordered UI
        }
    }
}
```

> [!NOTE]
> The above content is taken from the configuration file structure of LOICollectionA 1.15.0. The configuration file structure of later versions may differ.

## Module Data Files

Data files refer to files that store data in the database. Data files are the core of the database, as they store all the data in the database. Data files can be text files, binary files, or other types of files. The format and content of a data file depend on the type of database and the application scenario.  
Currently, `LOICollectionA` supports two data file formats: `Json` and `SQLite`. Only data files in the `Json` format can be modified directly. As for data files in the `SQLite` format, we do not recommend modifying them directly.

> [!TIP]
> In most cases, you do not need to modify data files manually, because most data files have built-in editors during the use of `LOICollectionA`. Starting from 1.15.0, Menu and Shop have been changed to edit lcui data files directly, and in-game editors are no longer provided.

### menu.lcui (config directory)

> Starting from LOICollectionA 1.15.0, Menu no longer reads `menu.json`. Please create `menu.lcui` yourself in the `plugins/LOICollectionA/config` directory, directly define `MenuData` in the file, and open it using `MenuForm`. The form ID corresponds to the Id passed in `/menu gui <Id>`.

```lcui
button1 = new MenuItemData();
button1.type = "button";
button1.title = "Button 1";
button1.id = "Button1";
button1.run = [ "say Button1" ];
button1.permission = 0;

form = new MenuForm("main", "Menu Example");
form.label("This is a menu example", new TextOptions());
form.button("Button 1", button1, func () -> void {
}, new ButtonOptions());
form.closeButton();
form.show(func (result) -> void {
    if (result.closeReason == 2) [
        mc::runCmd("say No permission");
    :
        if (result.closeReason == 3) [
            mc::runCmd("say No score");
        ]
    ]
});

confirmAction = new MenuItemData();
confirmAction.type = "button";
confirmAction.title = "Confirm";
confirmAction.run = [ "say Confirm" ];
confirmAction.permission = 0;

cancelAction = new MenuItemData();
cancelAction.type = "button";
cancelAction.title = "Cancel";
cancelAction.run = [ "say Cancel" ];
cancelAction.permission = 0;

box = new MenuMessageBox("Menu1", "Menu 1");
box.body("This is a menu 1");
box.button1("Confirm", confirmAction);
box.button2("Cancel", cancelAction);
box.show(func (result) -> void {
});

```

### shop.lcui (config directory)

> Starting from LOICollectionA 1.15.0, Shop no longer reads `shop.json`. Please create `shop.lcui` yourself in the `plugins/LOICollectionA/config` directory, directly define `ShopData` in the file, and open it using `ShopForm`. The form ID corresponds to the Id passed in `/shop gui <Id>`.

```lcui
mainBuy = new ShopData();
mainBuy.id = "MainBuy";
mainBuy.type = "buy";
mainBuy.title = "Buy Shop Example";
mainBuy.content = "This is a shop example";
mainBuy.exitCommand = "say Exit Shop";
mainBuy.scoreCommand = "say No score";

apple = new ShopItemData();
apple.type = "commodity";
apple.title = "Apple";
apple.introduce = "A red apple";
apple.number = "Buy number";
apple.id = "minecraft:apple";
appleScore = new ScoreRequirement();
appleScore.objective = "money";
appleScore.value = 100;
apple.scores = [ appleScore ];

mainBuy.items = [ apple ];

form = new ShopForm("MainBuy", mainBuy);
form.show(func (result) -> void {
    if (result.closeReason == 1) [
        if (result.resultCode == 1) [
            mc::runCmd(result.shop.scoreCommand);
        ]
    ]
});

```

> [!TIP]
> Apart from Menu and Shop, the interface files of the other modules (Blacklist, Mute, Cdk, Chat, Market, Notice, Pvp, Statistics, Tpa, Wallet, Language, etc.) are bundled with the plugin in the `plugins/LOICollectionA/gui` directory (e.g. `blacklist.lcui`, `mute.lcui`). They are updated together with the plugin and are not editable configuration files. Please do not modify them directly. (If you need to modify them, study the lcui syntax in depth first)

### notice.json

> [!NOTE]
> The following content is taken from the `notice.json` structure of LOICollectionA 1.10.0. The `notice.json` structure of later versions may differ.  
> You can find the `notice.json` file in the `plugins/LOICollectionA/config` directory.  

For the built-in editor, you can edit it through the following [command](./command.md#notice)

```json
{
    "main": { // Notice ID (must be unique)
        "title": "'Test Notice 123'", // Notice title
        "content": [ // Notice content (supports multiple lines)
            "'This is a test text 1'", // Content of line 1
            "'This is a test text 2'", // Content of line 2
            "'This is a test text 3'" // Content of line 3
        ],
        "priority": 0, // Notice priority
        "poiontout": true // Whether the notice pops up when a player comes online
    }
}
```

### cdk.json

> [!NOTE]
> The following content is taken from the `cdk.json` structure of LOICollectionA 1.7.0. The `cdk.json` structure of later versions may differ.  
> You can find the `cdk.json` file in the `plugins/LOICollectionA/config` directory.  

For the built-in editor, you can edit it through the following [command](./command.md#cdk)

```json
{
    "cdk": { // CDK ID (must be unique)
        "personal": false, // Whether it can only be redeemed once
        "player": [], // List of players who redeemed the CDK
        "scores": { // Scores granted by the CDK (optional)
            "money": 100 // Score amount granted by the CDK
        },
        "item": [ // Items granted by the CDK (optional)
            { 
                "id": "minecraft:apple", // Item ID
                "name": "apple", // Item name
                "quantity": 1, // Item quantity
                "specialvalue": 0, // Item special value
                "type": "universal" // Item parsing type
            },
            {
                "id": "{Count:2b,Damage:0s,Name:'minecraft:apple',WasPickedUp:0b}",
                "type": "nbt"
            }
        ],
        "title": { // Title granted by the CDK (optional)
            "None": 0 // Title ID (a value of 0 means a permanent title)
        },
        "time": "0" // CDK deletion time (the exact format is %Y%m%d%H%M%S; when 0, it exists permanently)
    }
}
```

> [!DANGER]
> When the server is starting, please do not modify data files directly, otherwise data content is very likely to be lost.

---

> [!TIP]
> Sometimes, friends are really great ヾ(•ω•`)o
