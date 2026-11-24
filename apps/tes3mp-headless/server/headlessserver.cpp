#include "headlessserver.hpp"

#include "headlessconfig.hpp"

#include "../rangeddebug.hpp"

#include <RakPeer.h>
#include <RakPeerInterface.h>

#include <Cell.hpp>
#include <CellController.hpp>
#include <MasterClient.hpp>
#include <Networking.hpp>
#include <Player.hpp>
#include <Script/Functions/Actors.hpp>
#include <Script/Functions/Cells.hpp>
#include <Script/Functions/Objects.hpp>
#include <Script/Functions/Positions.hpp>
#include <Script/Functions/RecordsDynamic.hpp>
#include <Script/Functions/Settings.hpp>
#include <Script/LangLua/LangLua.hpp>
#ifdef ENABLE_LUA
#include <lua.hpp>
#endif
#include <Script/Script.hpp>
#include <Utils.hpp>

#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwbase/dialoguemanager.hpp>
#include <apps/openmw/mwlua/worldbindings.hpp>
#include <apps/openmw/mwmechanics/aiactivate.hpp>
#include <apps/openmw/mwmechanics/aicombat.hpp>
#include <apps/openmw/mwmechanics/aiescort.hpp>
#include <apps/openmw/mwmechanics/aifollow.hpp>
#include <apps/openmw/mwmechanics/aipursue.hpp>
#include <apps/openmw/mwmechanics/aitravel.hpp>
#include <apps/openmw/mwmechanics/aiwander.hpp>
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/movement.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spellcasting.hpp>
#include <apps/openmw/mwmechanics/summoning.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>
#include <apps/openmw/mwmp/RecordHelper.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwphysics/raycasting.hpp>
#include <apps/openmw/mwworld/action.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include "../stubs/headlesswindowmanager.hpp"
#include <apps/openmw/mwworld/doorstate.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

#include <components/debug/debuglog.hpp>
#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/parser.hpp>
#include <components/version/version.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace TES3MPHeadless
{
    namespace
    {
        std::string getCfgString(const Settings::CategorySettingValueMap& settings, const std::string& category,
            const std::string& key, const std::string& fallback)
        {
            auto it = settings.find(std::make_pair(category, key));
            if (it == settings.end())
                return fallback;
            return it->second;
        }

        int getCfgInt(const Settings::CategorySettingValueMap& settings, const std::string& category,
            const std::string& key, int fallback)
        {
            auto it = settings.find(std::make_pair(category, key));
            if (it == settings.end() || it->second.empty())
                return fallback;
            try
            {
                return std::stoi(it->second);
            }
            catch (...)
            {
                return fallback;
            }
        }

        bool getCfgBool(const Settings::CategorySettingValueMap& settings, const std::string& category,
            const std::string& key, bool fallback)
        {
            auto it = settings.find(std::make_pair(category, key));
            if (it == settings.end() || it->second.empty())
                return fallback;
            const std::string lower = Misc::StringUtils::lowerCase(it->second);
            if (lower == "true" || lower == "1" || lower == "yes")
                return true;
            if (lower == "false" || lower == "0" || lower == "no")
                return false;
            return fallback;
        }

        bool isMovableObjectType(const MWWorld::Ptr& ptr)
        {
            const unsigned int type = ptr.getType();
            return type == ESM::Miscellaneous::sRecordId || type == ESM::Weapon::sRecordId
                || type == ESM::Armor::sRecordId || type == ESM::Clothing::sRecordId
                || type == ESM::Book::sRecordId || type == ESM::Potion::sRecordId
                || type == ESM::Ingredient::sRecordId || type == ESM::Apparatus::sRecordId
                || type == ESM::Lockpick::sRecordId || type == ESM::Probe::sRecordId
                || type == ESM::Repair::sRecordId || type == ESM::Light::sRecordId;
        }
    }

    HeadlessServer::HeadlessServer(
        Files::ConfigurationManager& cfgMgr, MWWorld::World& world, HeadlessServerConfig config)
        : mCfgMgr(cfgMgr)
        , mWorld(world)
        , mConfig(std::move(config))
        , mAnchors(world)
        , mCombat(world)
        , mDialogue(world)
        , mReplication(world)
    {
    }

    HeadlessServer::~HeadlessServer()
    {
        shutdown();
    }

    std::filesystem::path HeadlessServer::resolveConfigPath(const std::filesystem::path& candidate) const
    {
        return resolveServerConfigPath(mCfgMgr, candidate);
    }

    std::filesystem::path HeadlessServer::resolvePluginHome(
        const std::string& rawHome, const std::filesystem::path& cfgDir) const
    {
        return resolveServerPluginHome(mCfgMgr, rawHome, cfgDir);
    }

    void HeadlessServer::init()
    {
        Settings::CategorySettingValueMap serverSettings;
        Settings::SettingsFileParser parser;

        std::filesystem::path defaultCfg = resolveConfigPath(
            mConfig.mServerCfgPath.empty() ? std::filesystem::path("tes3mp-server-default.cfg") : mConfig.mServerCfgPath);

        if (defaultCfg.empty())
            defaultCfg = resolveConfigPath("tes3mp-server-default.cfg");

        std::filesystem::path cfgDir;
        if (!defaultCfg.empty())
        {
            parser.loadSettingsFile(defaultCfg, serverSettings, false, true);
            cfgDir = defaultCfg.parent_path();
            Log(Debug::Info) << "Loaded server default config: " << defaultCfg.string();

            const std::filesystem::path siblingOverride = cfgDir / "tes3mp-server.cfg";
            if (siblingOverride != defaultCfg && std::filesystem::exists(siblingOverride))
            {
                parser.loadSettingsFile(siblingOverride, serverSettings, false, true);
                Log(Debug::Info) << "Loaded server override config: " << siblingOverride.string();
            }
        }
        else if (!mConfig.mServerCfgPath.empty())
        {
            throw std::runtime_error("Requested --server-cfg file was not found: " + mConfig.mServerCfgPath.string());
        }
        else
        {
            Log(Debug::Warning) << "tes3mp-server-default.cfg not found; using built-in server defaults.";
        }

        const std::filesystem::path userCfg = mCfgMgr.getUserConfigPath() / "tes3mp-server.cfg";
        if (std::filesystem::exists(userCfg) && userCfg != defaultCfg)
        {
            parser.loadSettingsFile(userCfg, serverSettings, false, true);
            Log(Debug::Info) << "Loaded user server config: " << userCfg.string();
        }

        int logLevel = getCfgInt(serverSettings, "General", "logLevel", TimedLog::LOG_INFO);
        logLevel = std::clamp(logLevel, static_cast<int>(TimedLog::LOG_VERBOSE), static_cast<int>(TimedLog::LOG_FATAL));
        LOG_INIT(logLevel);
        mLogInitialized = true;

        const int maxPlayers = std::max(1, getCfgInt(serverSettings, "General", "maximumPlayers", 64));
        const std::string address = getCfgString(serverSettings, "General", "localAddress", "0.0.0.0");
        const int cfgPort = getCfgInt(serverSettings, "General", "port", 25565);
        const uint16_t port = mConfig.mHasPortOverride ? mConfig.mPortOverride : static_cast<uint16_t>(cfgPort);
        const std::string password = getCfgString(serverSettings, "General", "password", "");

        const std::string rawPluginHome = getCfgString(serverSettings, "Plugins", "home", "./server");
        const std::filesystem::path resolvedHome = resolvePluginHome(rawPluginHome, cfgDir);
        const std::string pluginHome = resolvedHome.string();
        const std::string dataDirectory = Utils::convertPath(pluginHome + "/data");
        const std::string pluginsCsv = getCfgString(serverSettings, "Plugins", "plugins", "serverCore.lua");
        std::vector<std::string> plugins = Utils::split(pluginsCsv, ',');
        for (auto& plugin : plugins)
        {
            while (!plugin.empty() && std::isspace(static_cast<unsigned char>(plugin.front())))
                plugin.erase(plugin.begin());
            while (!plugin.empty() && std::isspace(static_cast<unsigned char>(plugin.back())))
                plugin.pop_back();
        }

        const bool pvpEnabled = getCfgBool(serverSettings, "PvP", "enabled", true);
        auto splitTrimCsv = [](const std::string& csv) {
            std::vector<std::string> out = Utils::split(csv, ',');
            for (std::string& entry : out)
            {
                while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.front())))
                    entry.erase(entry.begin());
                while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.back())))
                    entry.pop_back();
            }
            out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }),
                out.end());
            return out;
        };
        mCombat.setPvpConfig(pvpEnabled,
            splitTrimCsv(getCfgString(serverSettings, "PvP", "freeCells", "")),
            splitTrimCsv(getCfgString(serverSettings, "PvP", "freeRegions", "")));

        std::string commitHash(Version::getCommitHash());
        commitHash.erase(std::remove(commitHash.begin(), commitHash.end(), '\r'), commitHash.end());

        std::string versionInfo
            = Utils::getVersionInfo("TES3MP headless server", TES3MP_VERSION, commitHash, TES3MP_PROTO_VERSION);
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s", versionInfo.c_str());

        Script::SetModDir(dataDirectory);

#ifdef ENABLE_LUA
        LangLua::AddPackagePath(
            Utils::convertPath(pluginHome + "/scripts/?.lua;" + pluginHome + "/lib/lua/?.lua;"));
#ifdef _WIN32
        LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.dll"));
#else
        LangLua::AddPackageCPath(Utils::convertPath(pluginHome + "/lib/?.so"));
#endif
#endif

        if (RakNet::NonNumericHostString(address.c_str()))
            throw std::runtime_error("Non-numeric server localAddress is not supported: " + address);

        mPeer = RakNet::RakPeerInterface::GetInstance();
        if (!mPeer)
            throw std::runtime_error("Failed to create RakPeerInterface instance");

        std::stringstream passStream;
        passStream << TES3MP_VERSION << TES3MP_PROTO_VERSION << commitHash;
        const std::string protoPassword = passStream.str();
        mPeer->SetIncomingPassword(protoPassword.c_str(), static_cast<int>(protoPassword.size()));

        if (!pluginHome.empty() && std::filesystem::exists(resolvedHome))
        {
            for (const std::string& plugin : plugins)
            {
                if (plugin.empty())
                    continue;
                Script::LoadScript(plugin.c_str(), pluginHome.c_str());
            }
            mScriptsLoaded = true;
        }
        else
        {
            Log(Debug::Warning) << "Server plugin directory '" << pluginHome
                                << "' not found; skipping serverCore.lua load.";
        }

        RakNet::SocketDescriptor sd(port, address.c_str());
        switch (mPeer->Startup(static_cast<unsigned int>(maxPlayers), &sd, 1))
        {
            case RakNet::CRABNET_STARTED:
                break;
            case RakNet::CRABNET_ALREADY_STARTED:
                throw std::runtime_error("RakNet server already started");
            case RakNet::INVALID_SOCKET_DESCRIPTORS:
                throw std::runtime_error("Incorrect server port or localAddress");
            case RakNet::INVALID_MAX_CONNECTIONS:
                throw std::runtime_error("maximumPlayers must be positive");
            case RakNet::SOCKET_FAILED_TO_BIND:
            case RakNet::SOCKET_PORT_ALREADY_IN_USE:
            case RakNet::PORT_CANNOT_BE_ZERO:
                throw std::runtime_error("Failed to bind UDP port " + std::to_string(port));
            default:
                throw std::runtime_error("Cannot start RakNet server socket");
        }

        mPeer->SetMaximumIncomingConnections(static_cast<unsigned short>(maxPlayers));

        mNetworking = std::make_unique<mwmp::Networking>(mPeer);
        mNetworking->setServerPassword(password);
        mNetworking->setAuthoritativeServer(mConfig.mAuthority.authoritativeMode);
        mNetworking->setActorSyncCallback([this](unsigned char packetId, const mwmp::BaseActorList& list) {
            if (!mConfig.mAuthority.authoritativeMode || packetId == ID_ACTOR_AI)
                mPendingActorSyncs.push_back(QueuedActorSync{ packetId, list });
        });
        mNetworking->setPlayerPositionCallback(
            [this](::Player& player) { return validatePlayerPosition(player); });
        mNetworking->setPlayerCellChangeCallback(
            [this](::Player& player) { handlePlayerCellChange(player); });
        mNetworking->setPlayerInventoryCallback([this](::Player& player) {
            const unsigned short pid = player.getId();
            int gold = (player.inventoryChanges.action == mwmp::InventoryChanges::SET) ? 0 : mDialogue.getPlayerGold(pid);
            for (const mwmp::Item& item : player.inventoryChanges.items)
            {
                if (Misc::StringUtils::ciEqual(item.refId, "gold_001"))
                {
                    if (player.inventoryChanges.action == mwmp::InventoryChanges::REMOVE)
                        gold = std::max(0, gold - item.count);
                    else
                        gold += std::max(0, item.count);
                }
            }
            mDialogue.setPlayerGold(pid, gold);
        });
        mNetworking->setPlayerAttackCallback([this](::Player& player, mwmp::Attack& attack) {
            if (mConfig.mAuthority.authoritativeMode && mConfig.mAuthority.validateCombat)
            {
                MWWorld::Ptr attackerPtr = mAnchors.getAnchorPtr(player.getId());
                if (!attackerPtr.isEmpty())
                    syncPlayerAnchorState(player, attackerPtr);
            }
            return mCombat.handlePlayerAttack(player, attack, mAnchors, mNetworking.get(),
                mConfig.mAuthority.authoritativeMode && mConfig.mAuthority.validateCombat);
        });
        mNetworking->setPlayerCastCallback(
            [this](::Player& player, mwmp::Cast& cast) { return handlePlayerCast(player, cast); });
        mNetworking->setObjectActionCallback(
            [this](unsigned char packetId, ::Player& player, mwmp::BaseObjectList& list) {
                return handleObjectAction(packetId, player, list);
            });

        if (getCfgBool(serverSettings, "MasterServer", "enabled", false))
        {
            std::string masterAddr = getCfgString(serverSettings, "MasterServer", "address", "master.tes3mp.com");
            int masterPort = getCfgInt(serverSettings, "MasterServer", "port", 25561);
            int updateRate = getCfgInt(serverSettings, "MasterServer", "rate", 10000);

            if (Misc::StringUtils::ciEqual(masterAddr, "master.tes3mp.com") && masterPort == 25560)
                masterPort = 25561;
            if (updateRate < 8000)
                updateRate = 8000;

            mNetworking->InitQuery(masterAddr, static_cast<unsigned short>(masterPort));
            mNetworking->getMasterClient()->SetMaxPlayers(static_cast<unsigned int>(maxPlayers));
            mNetworking->getMasterClient()->SetUpdateRate(static_cast<unsigned int>(updateRate));
            mNetworking->getMasterClient()->SetHostname(
                getCfgString(serverSettings, "General", "hostname", "TES3MP headless server"));
            mNetworking->getMasterClient()->SetRuleString("CommitHash", commitHash.substr(0, 10));
            mNetworking->getMasterClient()->Start();
        }

        CellFunctions::SetCellPinnedHandler([this](const char* cellDesc, bool pinned) {
            if (cellDesc)
                mWorld.getWorldScene().setCellPinned(cellDesc, pinned);
        });
        CellFunctions::SetPartyHandlers(
            [this](unsigned short leaderPid) { return mCellInstances.createParty(leaderPid); },
            [this](unsigned int partyId, unsigned short pid) { return mCellInstances.addPlayerToParty(partyId, pid); },
            [this](unsigned short pid) { return mCellInstances.removePlayerFromParty(pid); },
            [this](unsigned short pid) { return mCellInstances.getPlayerPartyId(pid); },
            [this](const char* cellName, bool instanced) {
                if (cellName)
                    mCellInstances.setInstancedDungeon(cellName, instanced);
            },
            [this](unsigned int partyId, const char* baseCellName) -> std::string {
                return baseCellName
                    ? mCellInstances.getOrCreateDungeonInstance(mWorld, partyId, baseCellName, mNetworking.get())
                    : std::string();
            });
        RecordsDynamicFunctions::SetRecordDynamicHandler(
            [this](const mwmp::BaseWorldstate& ws) { applyDynamicRecords(ws); });
        ObjectFunctions::SetServerObjectEventHandler(
            [this](unsigned char packetId, const mwmp::BaseObjectList& list) {
                handleServerObjectEvent(packetId, list);
            });
        ActorFunctions::SetServerActorAIHandler(
            [this](const mwmp::BaseActorList& list) { handleServerActorAI(list); });
        SettingFunctions::SetSendSettingsHandler([](::Player& player) {
#ifdef ENABLE_LUA
            if (lua_State* L = Script::GetPrimaryLuaState())
            {
                const int top = lua_gettop(L);
                lua_getglobal(L, "Players");
                if (lua_istable(L, -1))
                {
                    lua_pushinteger(L, player.getId());
                    lua_gettable(L, -2);
                    if (lua_istable(L, -1))
                    {
                        lua_getfield(L, -1, "data");
                        if (lua_istable(L, -1))
                        {
                            lua_getfield(L, -1, "settings");
                            if (lua_istable(L, -1))
                            {
                                lua_getfield(L, -1, "staffRank");
                                if (lua_isnumber(L, -1))
                                    player.staffRank = static_cast<int>(lua_tointeger(L, -1));
                                lua_pop(L, 1);
                            }
                        }
                    }
                }
                lua_settop(L, top);
            }
#endif
            Log(Debug::Info) << "[INFO] Syncing ID_GAME_SETTINGS pid=" << player.getId()
                             << " (" << player.npc.mName << ")"
                             << " consoleAllowed=" << (player.consoleAllowed ? "true" : "false")
                             << " staffRank=" << player.staffRank;
        });
        MWLua::setHeadlessRecordCreatedCallback(
            [this](const std::type_info& type, const void* recordPtr) { handleLuaRecordCreated(type, recordPtr); });
        MWMechanics::setHeadlessSummonCallback(
            [this](const MWWorld::Ptr& placed, const MWWorld::Ptr& summoner, ESM::RefId effectId, ESM::RefId sourceId) {
                if (!mNetworking || placed.isEmpty() || !placed.isInCell())
                    return;

                const int mpNum = mNetworking->incrementMpNum();
                const_cast<MWWorld::Ptr&>(placed).getCellRef().setMpNum(mpNum);

                mwmp::BaseObjectList spawnList;
                spawnList.cell = placed.getCell()->getCell()->getEsm3();
                spawnList.packetOrigin = mwmp::SERVER_SCRIPT;

                mwmp::BaseObject obj;
                obj.refId = placed.getCellRef().getRefId().getRefIdString();
                obj.refNum = 0;
                obj.mpNum = mpNum;
                obj.position = placed.getRefData().getPosition();
                obj.isSummon = true;
                obj.summonEffectId = ESM::MagicEffect::refIdToIndex(effectId);
                obj.summonSpellId = sourceId.serializeText();
                obj.summonDuration = summoner.getClass()
                                         .getCreatureStats(summoner)
                                         .getActiveSpells()
                                         .getEffectDuration(effectId, sourceId);

                const std::optional<unsigned short> masterPid = mAnchors.resolvePidFromPtr(summoner);
                if (masterPid.has_value())
                {
                    if (::Player* masterPlayer = Players::getPlayer(*masterPid))
                    {
                        obj.master.isPlayer = true;
                        obj.master.guid = masterPlayer->guid;
                    }
                }
                else if (!summoner.isEmpty())
                {
                    obj.master.isPlayer = false;
                    obj.master.refNum = summoner.getCellRef().getRefNum().mIndex;
                    obj.master.mpNum = summoner.getCellRef().getMpNum();
                    obj.master.refId = summoner.getCellRef().getRefId().getRefIdString();
                }

                spawnList.baseObjects.push_back(std::move(obj));
                if (mwmp::ObjectPacket* spawnPkt
                    = mNetworking->getObjectPacketController()->GetPacket(ID_OBJECT_SPAWN))
                {
                    spawnPkt->setObjectList(&spawnList);
                    spawnPkt->Send(true);
                }

                Log(Debug::Info) << "[INFO] Spawned summoned creature "
                                 << placed.getCellRef().getRefId().serializeText()
                                 << " mpNum=" << mpNum
                                 << " cell=" << Utils::getCellDescription(spawnList.cell)
                                 << " master="
                                 << (masterPid.has_value() ? ("pid=" + std::to_string(*masterPid))
                                                           : summoner.getCellRef().getRefId().serializeText())
                                 << " spell=" << sourceId.serializeText()
                                 << " duration=" << spawnList.baseObjects.front().summonDuration;
            });
        MWMechanics::setHeadlessUnsummonCallback([this](const MWWorld::Ptr& ptr) {
            if (!mNetworking || ptr.isEmpty() || !ptr.isInCell())
                return;

            mwmp::BaseObjectList deleteList;
            deleteList.cell = ptr.getCell()->getCell()->getEsm3();
            deleteList.packetOrigin = mwmp::SERVER_SCRIPT;

            mwmp::BaseObject obj;
            obj.refId = ptr.getCellRef().getRefId().getRefIdString();
            obj.mpNum = ptr.getCellRef().getMpNum();
            obj.refNum = (obj.mpNum != 0) ? 0 : ptr.getCellRef().getRefNum().mIndex;
            deleteList.baseObjects.push_back(std::move(obj));

            if (mwmp::ObjectPacket* delPkt = mNetworking->getObjectPacketController()->GetPacket(ID_OBJECT_DELETE))
            {
                delPkt->setObjectList(&deleteList);
                delPkt->Send(true);
            }

            Log(Debug::Info) << "[INFO] Despawned summoned creature "
                             << ptr.getCellRef().getRefId().serializeText()
                             << " mpNum=" << ptr.getCellRef().getMpNum()
                             << " cell=" << Utils::getCellDescription(deleteList.cell);
        });

        mNetworking->postInit();
        mBoundPort = mNetworking->getPort();

        Log(Debug::Info) << "TES3MP networking listening on UDP " << address << ":" << mBoundPort
                         << " (maxPlayers=" << maxPlayers
                         << ", authoritative=" << (mConfig.mAuthority.authoritativeMode ? "true" : "false")
                         << ", plugins=" << (mScriptsLoaded ? pluginsCsv : "<none>") << ")";
    }

    void HeadlessServer::shutdown()
    {
        if (mShutDown)
            return;
        mShutDown = true;

        CellFunctions::SetCellPinnedHandler(nullptr);
        CellFunctions::SetPartyHandlers(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        RecordsDynamicFunctions::SetRecordDynamicHandler(nullptr);
        ObjectFunctions::SetServerObjectEventHandler(nullptr);
        ActorFunctions::SetServerActorAIHandler(nullptr);
        SettingFunctions::SetSendSettingsHandler(nullptr);
        MWLua::setHeadlessRecordCreatedCallback(nullptr);
        MWMechanics::setHeadlessSummonCallback(nullptr);
        MWMechanics::setHeadlessUnsummonCallback(nullptr);
        mCellInstances.clear();

        mAnchors.clear();
        mCombat.clear();
        mDialogue.clear();
        mReplication.clear();
        mPlayerValidation.clear();
        mPendingActorSyncs.clear();
        mBroadcastAccumulator = 0.0;

        if (mNetworking)
        {
            mNetworking->setActorSyncCallback(nullptr);
            mNetworking->setPlayerPositionCallback(nullptr);
            mNetworking->setPlayerCellChangeCallback(nullptr);
            mNetworking->setPlayerAttackCallback(nullptr);
            mNetworking->setPlayerCastCallback(nullptr);
            mNetworking->setObjectActionCallback(nullptr);
            if (MasterClient* mc = mNetworking->getMasterClient())
                mc->Stop();
            mNetworking->terminateTimers();
            mNetworking.reset();
        }

        if (mScriptsLoaded)
        {
            Script::UnloadScripts();
            mScriptsLoaded = false;
        }

        if (mPeer)
        {
            mPeer->Shutdown(100);
            RakNet::RakPeerInterface::DestroyInstance(mPeer);
            mPeer = nullptr;
        }

        if (mLogInitialized)
        {
            LOG_QUIT();
            mLogInitialized = false;
        }
    }

    void HeadlessServer::handlePlayerCellChange(::Player& player)
    {
        mPlayersWithKnownCell.insert(player.getId());
        auto& state = mPlayerValidation[player.getId()];
        state.mLastCell = player.cell;
        state.mLastUpdate = std::chrono::steady_clock::now();
        // Reset initialized flag so the next ID_PLAYER_POSITION establishes the new cell's starting
        // coordinates instead of comparing old-cell coordinates against a post-teleport/door position.
        state.mInitialized = false;
    }

    bool HeadlessServer::validatePlayerPosition(::Player& player)
    {
        const unsigned short pid = player.getId();
        if (player.getLoadState() == Player::POSTLOADED
            && (!player.cell.isExterior() || player.position.asVec3().length2() > 1.0f))
        {
            mPlayersWithKnownCell.insert(pid);
        }

        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validatePlayerMovement)
            return true;

        auto& state = mPlayerValidation[pid];
        const auto now = std::chrono::steady_clock::now();
        const osg::Vec3f newPos = player.position.asVec3();

        const bool sameCell = state.mInitialized && state.mLastCell.isExterior() == player.cell.isExterior()
            && (player.cell.isExterior()
                    ? (state.mLastCell.mData.mX == player.cell.mData.mX && state.mLastCell.mData.mY == player.cell.mData.mY)
                    : (state.mLastCell.mName == player.cell.mName));

        // Detect out-of-order ID_PLAYER_POSITION arriving just before ID_PLAYER_CELL_CHANGE across doors/borders.
        bool pendingCellTransition = false;
        if (player.cell.isExterior())
        {
            const ESM::ExteriorCellLocation posCell
                = ESM::positionToExteriorCellLocation(newPos.x(), newPos.y(), ESM::Cell::sDefaultWorldspaceId);
            const int dx = std::abs(posCell.mX - player.cell.mData.mX);
            const int dy = std::abs(posCell.mY - player.cell.mData.mY);
            if (dx > 1 || dy > 1)
            {
                if (sameCell)
                {
                    player.position.pos[0] = state.mLastPos.x();
                    player.position.pos[1] = state.mLastPos.y();
                    player.position.pos[2] = state.mLastPos.z();
                }
                return false;
            }
            if (dx > 0 || dy > 0)
                pendingCellTransition = true;
        }
        else if (state.mInitialized && (newPos - state.mLastPos).length2() > (16384.0f * 16384.0f))
        {
            pendingCellTransition = true;
        }

        if (!state.mInitialized || player.exchangeFullInfo || !sameCell || pendingCellTransition)
        {
            state.mLastCell = player.cell;
            state.mLastPos = newPos;
            state.mLastUpdate = now;
            state.mInitialized = true;
            return true;
        }

        const double elapsedSec = std::max(0.016, std::chrono::duration<double>(now - state.mLastUpdate).count());
        const float maxAllowedDist = static_cast<float>(mConfig.mAuthority.maxPlayerSpeed * elapsedSec)
            + mConfig.mAuthority.playerTeleportAllowance;
        const float distSq = (newPos - state.mLastPos).length2();
        if (distSq > maxAllowedDist * maxAllowedDist)
        {
            Log(Debug::Warning) << "Rejected implausible movement delta (" << std::sqrt(distSq)
                                << " units in " << elapsedSec << "s) for player " << player.npc.mName;
            player.position.pos[0] = state.mLastPos.x();
            player.position.pos[1] = state.mLastPos.y();
            player.position.pos[2] = state.mLastPos.z();
            return false;
        }

        state.mLastPos = newPos;
        state.mLastUpdate = now;
        return true;
    }

    bool HeadlessServer::handlePlayerCast(::Player& player, mwmp::Cast& cast)
    {
        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validateCombat)
            return true;

        MWWorld::Ptr casterPtr = mAnchors.getAnchorPtr(player.getId());
        if (casterPtr.isEmpty())
            return true;

        if (cast.pressed)
            return true;

        if (!cast.success)
            return true;

        syncPlayerAnchorState(player, casterPtr);
        MWWorld::Ptr targetPtr
            = mCombat.resolveTargetPtr(cast.target, casterPtr.isInCell() ? casterPtr.getCell() : nullptr, mAnchors);

        if (!cast.spellId.empty())
        {
            const ESM::Spell* spell
                = mWorld.getStore().get<ESM::Spell>().search(ESM::RefId::deserializeText(cast.spellId));
            if (!spell)
                return false;

            bool hasTouchOrTarget = false;
            bool hasTouch = false;
            bool hasHarmful = false;
            const auto& mgefStore = mWorld.getStore().get<ESM::MagicEffect>();
            for (const ESM::IndexedENAMstruct& eff : spell->mEffects.mList)
            {
                if (eff.mData.mRange == ESM::RT_Touch)
                {
                    hasTouch = true;
                    hasTouchOrTarget = true;
                }
                else if (eff.mData.mRange == ESM::RT_Target)
                {
                    hasTouchOrTarget = true;
                }
                if (const ESM::MagicEffect* mgef = mgefStore.search(eff.mData.mEffectID))
                {
                    if (mgef->mData.mFlags & ESM::MagicEffect::Harmful)
                        hasHarmful = true;
                }
            }

            if (targetPtr.isEmpty() && hasTouch)
            {
                const float fCombatDistance = mWorld.getStore().get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat();
                targetPtr = MWMechanics::getHitContact(casterPtr, fCombatDistance).first;
            }

            if (!targetPtr.isEmpty() && targetPtr != casterPtr && hasHarmful
                && mAnchors.resolvePidFromPtr(targetPtr).has_value())
            {
                const MWWorld::CellStore* targetCell = targetPtr.isInCell() ? targetPtr.getCell() : nullptr;
                if (!mCombat.isPvpAllowed(targetCell, targetPtr.getRefData().getPosition().asVec3()))
                {
                    cast.isHit = false;
                    return false;
                }
            }

            if (!targetPtr.isEmpty() && targetPtr != casterPtr && mWorld.getRayCasting()
                && !mWorld.getRayCasting()->getLineOfSight(casterPtr, targetPtr))
            {
                cast.isHit = false;
                return false;
            }

            MWMechanics::CastSpell spellCast(casterPtr, targetPtr);
            spellCast.mAlwaysSucceed = true;
            if (!targetPtr.isEmpty())
                spellCast.mHitPosition = targetPtr.getRefData().getPosition().asVec3();
            cast.success = spellCast.cast(spell);

            if (cast.success)
                casterPtr.getClass().getCreatureStats(casterPtr).getActiveSpells().update(casterPtr, 0.016f);

            if (hasTouchOrTarget)
            {
                Log(Debug::Info) << "[INFO] Headless player cast: caster=" << player.npc.mName
                                 << " spellId=" << cast.spellId
                                 << " target="
                                 << (targetPtr.isEmpty() ? "<none>" : targetPtr.getCellRef().getRefId().serializeText())
                                 << " success=" << (cast.success ? "true" : "false");
            }
        }

        return true;
    }

    bool HeadlessServer::handleObjectAction(unsigned char packetId, ::Player& player, mwmp::BaseObjectList& objectList)
    {
        if (packetId == ID_CONSOLE_COMMAND && (!player.consoleAllowed || player.staffRank <= 0))
        {
            Log(Debug::Warning) << "Rejected ID_CONSOLE_COMMAND from unauthorized player "
                                << player.npc.mName << " (pid=" << player.getId()
                                << ", consoleAllowed=" << (player.consoleAllowed ? "true" : "false")
                                << ", staffRank=" << player.staffRank << ")";
            return false;
        }

        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validateObjects)
            return true;

        if (objectList.packetOrigin == mwmp::SERVER_SCRIPT)
            return true;

        MWWorld::CellStore* cellStore = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, objectList.cell);
        if (!cellStore || !mWorld.getWorldScene().isCellActive(*cellStore))
        {
            Log(Debug::Warning) << "Rejected object packet " << static_cast<int>(packetId) << " from "
                                << player.npc.mName << " targeting inactive cell "
                                << Utils::getCellDescription(objectList.cell);
            return false;
        }

        MWWorld::Ptr playerAnchor = mAnchors.getAnchorPtr(player.getId());

        const float maxDistSq = mConfig.mAuthority.maxInteractDistance * mConfig.mAuthority.maxInteractDistance;

        for (mwmp::BaseObject& obj : objectList.baseObjects)
        {
            if (packetId == ID_OBJECT_PLACE)
            {
                if (!playerAnchor.isEmpty()
                    && (obj.position.asVec3() - playerAnchor.getRefData().getPosition().asVec3()).length2() > maxDistSq)
                {
                    return false;
                }
                try
                {
                    MWWorld::ManualRef ref(
                        mWorld.getStore(), ESM::RefId::deserializeText(obj.refId), std::max(1, obj.count));
                    MWWorld::Ptr newPtr = ref.getPtr();
                    if (obj.charge > -1)
                        newPtr.getCellRef().setCharge(obj.charge);
                    if (obj.enchantmentCharge > -1)
                        newPtr.getCellRef().setEnchantmentCharge(obj.enchantmentCharge);
                    if (!obj.soul.empty())
                        newPtr.getCellRef().setSoul(ESM::RefId::deserializeText(obj.soul));
                    MWWorld::Ptr placed = mWorld.placeObject(newPtr, cellStore, obj.position);
                    if (!placed.isEmpty())
                    {
                        placed.getCellRef().setMpNum(obj.mpNum);
                        mWorld.enable(placed);
                    }
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Authoritative ID_OBJECT_PLACE skipped '" << obj.refId
                                        << "': " << e.what();
                }
                continue;
            }

            MWWorld::Ptr targetPtr = cellStore->searchExact(obj.refNum, obj.mpNum, obj.refId);
            if (targetPtr.isEmpty())
                continue;

            if (!playerAnchor.isEmpty())
            {
                const float distSq
                    = (targetPtr.getRefData().getPosition().asVec3() - playerAnchor.getRefData().getPosition().asVec3())
                          .length2();
                if (distSq > maxDistSq)
                {
                    Log(Debug::Warning) << "Rejected out-of-range object packet " << static_cast<int>(packetId)
                                        << " from " << player.npc.mName;
                    return false;
                }
            }

            switch (packetId)
            {
                case ID_OBJECT_ACTIVATE:
                    if (targetPtr.getType() == ESM::Door::sRecordId && targetPtr.getCellRef().getTeleport())
                    {
                        const std::string destCell = targetPtr.getCellRef().getDestCell().serializeText();
                        if (mCellInstances.isInstancedDungeon(destCell))
                        {
                            const unsigned int partyId = mCellInstances.getOrAssignSoloPartyId(player.getId());
                            const std::string instanceCell = mCellInstances.getOrCreateDungeonInstance(
                                mWorld, partyId, destCell, mNetworking.get());
                            const ESM::Position destPos = targetPtr.getCellRef().getDoorDest();
                            CellFunctions::SetCell(player.getId(), instanceCell.c_str());
                            CellFunctions::SendCell(player.getId());
                            PositionFunctions::SetPos(
                                player.getId(), destPos.pos[0], destPos.pos[1], destPos.pos[2]);
                            PositionFunctions::SetRot(player.getId(), destPos.rot[0], destPos.rot[2]);
                            PositionFunctions::SendPos(player.getId());
                            return false;
                        }
                    }
                    if (!playerAnchor.isEmpty())
                    {
                        if (auto action = targetPtr.getClass().activate(targetPtr, playerAnchor))
                            action->execute(playerAnchor);
                    }
                    break;
                case ID_OBJECT_MOVE:
                    if (isMovableObjectType(targetPtr))
                        mWorld.moveObject(targetPtr, obj.position.asVec3());
                    break;
                case ID_OBJECT_ROTATE:
                    if (isMovableObjectType(targetPtr))
                        mWorld.rotateObject(
                            targetPtr, osg::Vec3f(obj.position.rot[0], obj.position.rot[1], obj.position.rot[2]));
                    break;
                case ID_DOOR_STATE:
                    if (targetPtr.getType() == ESM::Door::sRecordId)
                    {
                        MWWorld::DoorState state = MWWorld::DoorState::Idle;
                        if (obj.doorState == 1)
                            state = MWWorld::DoorState::Opening;
                        else if (obj.doorState == 2)
                            state = MWWorld::DoorState::Closing;
                        targetPtr.getClass().setDoorState(targetPtr, state);
                    }
                    break;
                case ID_OBJECT_LOCK:
                    targetPtr.getCellRef().setLockLevel(obj.lockLevel);
                    break;
                case ID_OBJECT_DELETE:
                    mWorld.deleteObject(targetPtr);
                    break;
                case ID_OBJECT_DIALOGUE_CHOICE:
                    Log(Debug::Info) << "[HeadlessServer] Received ID_OBJECT_DIALOGUE_CHOICE actor=" << obj.refId
                                     << " type=" << static_cast<int>(obj.dialogueChoiceType)
                                     << " topic='" << obj.topicId << "' pid=" << player.getId();
                    if (!playerAnchor.isEmpty() && targetPtr.getClass().isActor())
                    {
                        const int prevBounty = player.npcStats.mBounty;
                        if (mDialogue.executeDialogueChoice(targetPtr, playerAnchor, player.npcStats.mBounty,
                                obj.dialogueChoiceType, obj.topicId, obj.refId, player.getId(), mAnchors, mCombat))
                        {
                            if (prevBounty > 0 && player.npcStats.mBounty == 0)
                            {
                                if (mwmp::PlayerPacket* bountyPkt
                                    = mNetworking->getPlayerPacketController()->GetPacket(ID_PLAYER_BOUNTY))
                                {
                                    bountyPkt->setPlayer(&player);
                                    bountyPkt->Send(player.guid);
                                    bountyPkt->Send(true);
                                }
                            }

                            // A "Go to Jail" choice ran World::goToJail() phase one synchronously (it clears the
                            // bounty and flags mPlayerInJail). Its second phase only teleports via the GUI
                            // WindowManager, which the headless server lacks, so replicate the jail relocation
                            // here.
                            if (mWorld.isPlayerInJail())
                                applyJailTeleport(player, playerAnchor, prevBounty);
                        }
                    }
                    break;
                case ID_CONTAINER:
                    if (targetPtr.getType() == ESM::Container::sRecordId
                        || targetPtr.getClass().hasInventoryStore(targetPtr))
                    {
                        MWWorld::ContainerStore& store = targetPtr.getClass().getContainerStore(targetPtr);
                        if (objectList.action == mwmp::BaseObjectList::SET)
                            store.clear();

                        for (const mwmp::ContainerItem& item : obj.containerItems)
                        {
                            try
                            {
                                const ESM::RefId itemId = ESM::RefId::deserializeText(item.refId);
                                if (objectList.action == mwmp::BaseObjectList::REMOVE)
                                    store.remove(itemId, std::max(1, item.count));
                                else if (item.count > 0)
                                    store.add(itemId, item.count, true);
                            }
                            catch (const std::exception& e)
                            {
                                Log(Debug::Warning) << "Authoritative ID_CONTAINER item skipped '" << item.refId
                                                    << "': " << e.what();
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        return true;
    }

    void HeadlessServer::applyJailTeleport(::Player& player, const MWWorld::Ptr& playerAnchor, int bounty)
    {
        /*
            Start of tes3mp change (major)

            In singleplayer World::goToJail() finishes by asking the (GUI) WindowManager to run JailScreen,
            which teleports the player to the nearest "prisonmarker", rests the computed number of days and
            rolls the skill changes. The headless WindowManager has no JailScreen, so that second phase did
            nothing: the dialogue choice ran, the bounty was cleared, but nobody was ever moved. Replicate the
            same end state here on the authoritative side. The day count matches World::goToJail exactly.
        */
        int days = 1;
        if (const ESM::GameSetting* setting
            = mWorld.getStore().get<ESM::GameSetting>().search("iDaysinPrisonMod"))
        {
            const int mod = std::max(1, setting->mValue.getInteger());
            days = std::max(1, bounty / mod);
        }

        // Route the dummy local player to the closest prison marker. It mirrors the active player, so its
        // resulting cell/position is the destination we replicate. Unlike ActionTeleport on an arbitrary
        // actor, the player branch sends no stray ActorCellChange packets.
        MWWorld::Ptr worldPlayer = mWorld.getPlayerPtr();
        if (!worldPlayer.isEmpty())
            mWorld.teleportToClosestMarker(worldPlayer, ESM::RefId::stringRefId("prisonmarker"));

        worldPlayer = mWorld.getPlayerPtr();
        if (worldPlayer.isEmpty() || !worldPlayer.isInCell())
        {
            Log(Debug::Warning) << "[HeadlessServer] Jail teleport failed for pid=" << player.getId()
                                << ": no prison marker found";
            return;
        }

        player.cell = worldPlayer.getCell()->getCell()->getEsm3();
        player.position = worldPlayer.getRefData().getPosition();

        // Force the client into the prison cell and position, and keep our stored authoritative cell/pos in
        // sync so syncPlayersToWorld does not snap the anchor back to the pre-jail location.
        const std::string cellDesc = worldPlayer.getCell()->getCell()->getId().serializeText();
        CellFunctions::SetCell(player.getId(), cellDesc.c_str());
        CellFunctions::SendCell(player.getId());
        PositionFunctions::SetPos(
            player.getId(), player.position.pos[0], player.position.pos[1], player.position.pos[2]);
        PositionFunctions::SetRot(player.getId(), player.position.rot[0], player.position.rot[2]);
        PositionFunctions::SendPos(player.getId());

        // Stop the arresting guard(s) from chasing/escalating as the anchor leaves the cell.
        if (!playerAnchor.isEmpty())
            mCombat.calmGuardsForAnchor(playerAnchor);

        // The server already relocated the player, so tell the client's jail screen not to teleport it again;
        // it still shows the progress window, rests and rolls the skill changes.
        if (mwmp::PlayerPacket* jailPkt = mNetworking->getPlayerPacketController()->GetPacket(ID_PLAYER_JAIL))
        {
            player.jailDays = days;
            player.ignoreJailTeleportation = true;
            player.ignoreJailSkillIncreases = false;
            player.jailProgressText = "";
            player.jailEndText = "";
            jailPkt->setPlayer(&player);
            jailPkt->Send(player.guid);
        }

        Log(Debug::Info) << "[HeadlessServer] Jailed pid=" << player.getId() << " (" << player.npc.mName
                         << ") days=" << days << " cell=" << Utils::getCellDescription(player.cell)
                         << " pos=(" << player.position.pos[0] << ", " << player.position.pos[1] << ", "
                         << player.position.pos[2] << ")";
        /*
            End of tes3mp change (major)
        */
    }

    void HeadlessServer::syncPlayersToWorld()
    {
        TPlayers* players = Players::getPlayers();
        if (!players)
            return;

        std::unordered_set<unsigned short> activeIds;
        for (const auto& [guid, player] : *players)
        {
            if (!player || player->getLoadState() != Player::POSTLOADED)
                continue;

            const unsigned short pid = player->getId();
            activeIds.insert(pid);

            // Don't spawn/move an anchor into the default (0, 0) cell before ID_PLAYER_CELL_CHANGE arrives.
            if (mPlayersWithKnownCell.find(pid) == mPlayersWithKnownCell.end())
                continue;

            MWWorld::CellStore* targetCell = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, player->cell);
            if (!targetCell || !mWorld.getWorldScene().isCellActive(*targetCell))
                continue;

            const bool isNewAnchor = mAnchors.getAnchorPtr(pid).isEmpty();
            MWWorld::Ptr anchor = mAnchors.spawnOrMoveAnchor(pid, targetCell, player->position);
            if (anchor.isEmpty())
                continue;

            MWMechanics::CreatureStats& anchorStats = anchor.getClass().getCreatureStats(anchor);
            if (isNewAnchor)
            {
                syncPlayerAnchorState(*player, anchor);
                mCombat.setLastHealth(pid, anchorStats.getHealth().getCurrent());
                mDialogue.setLastBounty(pid, player->npcStats.mBounty);
                Log(Debug::Info) << "[INFO] Player anchor spawned: pid=" << pid
                                 << " (" << player->npc.mName << ")"
                                 << " cell=" << Utils::getCellDescription(player->cell)
                                 << " activePlayers=" << activeIds.size();
#ifdef _DEBUG
                Log(Debug::Debug) << "[SmokeDiag] Player join anchor pid=" << pid
                                  << " cell=" << Utils::getCellDescription(player->cell)
                                  << " anchorEmpty=" << (anchor.isEmpty() ? "true" : "false")
                                  << " worldPlayerSync=" << (activeIds.size() == 1 ? "true" : "false");
#endif
            }
            else
            {
                const float anchorHealth = anchorStats.getHealth().getCurrent();
                if (mCombat.hasLastHealth(pid) && anchorHealth < mCombat.getLastHealth(pid) - 0.01f
                    && !player->creatureStats.mDead)
                {
                    /*
                        Start of tes3mp addition

                        Resolve the aggressor before dispatching the health change: dispatchAnchorHealthAndDeath
                        clears the anchor's hit-attempt actor on a lethal hit, so reading it afterwards would lose
                        the killer. Also mirror the landed attack to the owning client so it renders the hit
                        feedback and sees the ranged attack complete (success: true).
                    */
                    MWWorld::Ptr aggressor;
                    {
                        const ESM::RefNum aggressorNum = anchorStats.getHitAttemptActor();
                        if (aggressorNum.isSet())
                            aggressor
                                = MWBase::Environment::get().getWorldModel()->getPtr(aggressorNum);
                    }
                    const float hpDelta = std::max(0.f, mCombat.getLastHealth(pid) - anchorHealth);

                    if (mCombat.dispatchAnchorHealthAndDeath(
                            *player, anchor, anchorStats, mCombat.getLastHealth(pid), mNetworking.get(), mAnchors))
                        mDialogue.setResistedArrest(pid, false);

                    if (!aggressor.isEmpty() && aggressor != anchor && aggressor.getClass().isActor()
                        && !mAnchors.isPlayerAnchorPtr(aggressor))
                    {
                        bool ranged = false;
                        if (aggressor.getClass().hasInventoryStore(aggressor))
                        {
                            const MWWorld::InventoryStore& inv = aggressor.getClass().getInventoryStore(aggressor);
                            auto weapIt = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                            if (weapIt != inv.end() && weapIt->getType() == ESM::Weapon::sRecordId)
                            {
                                const int weapClass
                                    = MWMechanics::getWeaponType(weapIt->get<ESM::Weapon>()->mBase->mData.mType)
                                          ->mWeaponClass;
                                ranged = (weapClass == ESM::WeaponType::Ranged
                                    || weapClass == ESM::WeaponType::Thrown);
                            }
                        }
                        TES3MP_RANGED_LOG << "[RangedDebug] anchor pid=" << pid << " took " << hpDelta
                                          << " damage from " << aggressor.getCellRef().getRefId()
                                          << " ranged=" << (ranged ? "true" : "false");
                        mReplication.sendAttackLandedToPlayer(
                            *player, aggressor, ranged, hpDelta, mPeer, mNetworking.get());
                    }
                    /*
                        End of tes3mp addition
                    */
                }
                else
                {
                    if (anchor.getClass().isNpc())
                    {
                        const int anchorBounty = anchor.getClass().getNpcStats(anchor).getBounty();
                        if (mDialogue.hasLastBounty(pid) && anchorBounty != mDialogue.getLastBounty(pid))
                        {
                            player->npcStats.mBounty = anchorBounty;
                            if (mNetworking)
                            {
                                if (mwmp::PlayerPacket* bountyPkt
                                    = mNetworking->getPlayerPacketController()->GetPacket(ID_PLAYER_BOUNTY))
                                {
                                    bountyPkt->setPlayer(player);
                                    bountyPkt->Send(player->guid);
                                    bountyPkt->Send(true);
                                }
                            }
                        }
                        else if (mDialogue.hasLastBounty(pid) && mDialogue.getLastBounty(pid) > 0
                            && player->npcStats.mBounty == 0)
                        {
                            if (auto* winMgr
                                = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
                                winMgr->setPlayerDialogueOpen(pid, false);
                            mDialogue.setResistedArrest(pid, false);
                            mCombat.calmGuardsForAnchor(anchor);
                        }
                        if (player->npcStats.mBounty <= 0)
                            mDialogue.setResistedArrest(pid, false);
                    }
                    syncPlayerAnchorState(*player, anchor);
                }
                mCombat.setLastHealth(pid, anchorStats.getHealth().getCurrent());
                mDialogue.setLastBounty(
                    pid, anchor.getClass().isNpc() ? anchor.getClass().getNpcStats(anchor).getBounty() : 0);
            }

            // Keep dummy worldPlayer synced to the active player's cell/pos (including physics)
            // so World::doPhysics / Scene::changeCellGrid never pulls it back to Pelagiad (0, -7).
            if (activeIds.size() == 1)
            {
                MWWorld::Ptr worldPlayer = mWorld.getPlayerPtr();
                osg::Vec3f syncPos = player->position.asVec3();
                if (targetCell->isExterior())
                {
                    const ESM::RefId ws = targetCell->getCell()->getWorldSpace();
                    const ESM::ExteriorCellLocation posLoc
                        = ESM::positionToExteriorCellLocation(syncPos.x(), syncPos.y(), ws);
                    const int gx = targetCell->getCell()->getGridX();
                    const int gy = targetCell->getCell()->getGridY();
                    if (posLoc.mX != gx || posLoc.mY != gy)
                    {
                        const float cellSize = ESM::getCellSize(ws);
                        syncPos.x() = (gx + 0.5f) * cellSize;
                        syncPos.y() = (gy + 0.5f) * cellSize;
                    }
                }
                if (!worldPlayer.isEmpty()
                    && (worldPlayer.getCell() != targetCell
                        || (worldPlayer.getRefData().getPosition().asVec3() - syncPos).length2() > 1.0f))
                {
                    mWorld.moveObject(worldPlayer, targetCell, syncPos, true);
                }
            }
        }

        for (auto it = mPlayersWithKnownCell.begin(); it != mPlayersWithKnownCell.end();)
        {
            if (activeIds.find(*it) == activeIds.end() && (!Players::getPlayers() || !Players::getPlayer(*it)))
            {
                mCellInstances.removePlayerFromParty(*it);
                it = mPlayersWithKnownCell.erase(it);
            }
            else
                ++it;
        }

        std::vector<unsigned short> removedPids;
        for (const auto& [pid, anchor] : mAnchors.getAnchors())
        {
            (void)anchor;
            if (activeIds.find(pid) == activeIds.end())
                removedPids.push_back(pid);
        }
        for (unsigned short pid : removedPids)
        {
            Log(Debug::Info) << "[INFO] Player disconnected: pid=" << pid
                             << " removing anchor, remainingPlayers=" << activeIds.size();
#ifdef _DEBUG
            Log(Debug::Debug) << "[SmokeDiag] Player disconnect pid=" << pid
                              << " remainingPlayers=" << activeIds.size()
                              << " worldPlayerSync=" << (activeIds.size() == 1 ? "true" : "false");
#endif
            mAnchors.removeAnchor(pid);
            mCombat.clearPlayerHealth(pid);
            mDialogue.clearPlayerState(pid);
            mPlayerValidation.erase(pid);
            if (auto* winMgr = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
                winMgr->clearPlayerGuiState(pid);
        }
    }

    void HeadlessServer::syncPlayerAnchorState(const ::Player& player, MWWorld::Ptr& anchor)
    {
        if (anchor.isEmpty() || !anchor.getClass().isActor())
            return;

        MWMechanics::CreatureStats& stats = anchor.getClass().getCreatureStats(anchor);
        if (stats.isDead() && !player.creatureStats.mDead)
        {
            mCombat.disengageActorsTargeting(anchor, false);
            mAnchors.resurrectAnchor(anchor, player.npc.mName);
        }
        stats.setLevel(std::max(1, player.creatureStats.mLevel));
        for (int i = 0; i < 3; ++i)
        {
            if (player.creatureStats.mDynamic[i].mBase > 0.f)
            {
                MWMechanics::DynamicStat<float> dyn;
                dyn.readState(player.creatureStats.mDynamic[i]);
                stats.setDynamic(i, dyn);
            }
        }
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            if (player.creatureStats.mAttributes[i].mBase > 0)
            {
                MWMechanics::AttributeValue attr;
                attr.readState(player.creatureStats.mAttributes[i]);
                stats.setAttribute(ESM::Attribute::indexToRefId(i), attr);
            }
        }
        if (anchor.getClass().isNpc())
        {
            MWMechanics::NpcStats& npcStats = anchor.getClass().getNpcStats(anchor);
            npcStats.setBounty(player.npcStats.mBounty);
            for (int i = 0; i < ESM::Skill::Length; ++i)
            {
                if (player.npcStats.mSkills[i].mBase > 0)
                    npcStats.getSkill(ESM::Skill::indexToRefId(i)).readState(player.npcStats.mSkills[i]);
            }
        }

        if (anchor.getClass().hasInventoryStore(anchor))
        {
            MWWorld::InventoryStore& inv = anchor.getClass().getInventoryStore(anchor);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                const std::string& desiredId = player.equipmentItems[slot].refId;
                MWWorld::ContainerStoreIterator currentIt = inv.getSlot(slot);
                if (desiredId.empty())
                {
                    if (currentIt != inv.end())
                        inv.unequipSlot(slot);
                    continue;
                }
                if (currentIt != inv.end()
                    && Misc::StringUtils::ciEqual(currentIt->getCellRef().getRefId().serializeText(), desiredId))
                {
                    continue;
                }
                if (currentIt != inv.end())
                    inv.unequipSlot(slot);

                const ESM::RefId refId = ESM::RefId::deserializeText(desiredId);
                MWWorld::Ptr itemPtr = inv.search(refId);
                if (itemPtr.isEmpty())
                {
                    try
                    {
                        MWWorld::ContainerStoreIterator added = anchor.getClass().getContainerStore(anchor).add(
                            refId, std::max(1, player.equipmentItems[slot].count), true);
                        if (added != inv.end())
                            itemPtr = *added;
                    }
                    catch (const std::exception&)
                    {
                        continue;
                    }
                }
                if (!itemPtr.isEmpty())
                {
                    for (MWWorld::ContainerStoreIterator it = inv.begin(); it != inv.end(); ++it)
                    {
                        if (*it == itemPtr)
                        {
                            inv.equip(slot, it);
                            break;
                        }
                    }
                }
            }
        }
    }

    void HeadlessServer::syncActorsToWorld()
    {
        if (mPendingActorSyncs.empty())
            return;

        if (mConfig.mAuthority.authoritativeMode)
        {
            for (const QueuedActorSync& sync : mPendingActorSyncs)
            {
                if (sync.mPacketId != ID_ACTOR_AI)
                    continue;
                MWWorld::CellStore* sourceCell
                    = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, sync.mActorList.cell);
                if (!sourceCell)
                    continue;
                for (const mwmp::BaseActor& actor : sync.mActorList.baseActors)
                {
                    if (actor.aiAction != mwmp::BaseActorList::COMBAT || !actor.hasAiTarget || !actor.aiTarget.isPlayer)
                        continue;
                    ::Player* targetPlayer = Players::getPlayers() ? Players::getPlayer(actor.aiTarget.guid) : nullptr;
                    if (!targetPlayer)
                        continue;
                    MWWorld::Ptr targetAnchor = mAnchors.getAnchorPtr(targetPlayer->getId());
                    if (targetAnchor.isEmpty())
                        continue;
                    MWWorld::Ptr actorPtr = sourceCell->searchExact(actor.refNum, actor.mpNum, actor.refId, true);
                    if (actorPtr.isEmpty() || !actorPtr.getClass().isActor())
                        continue;
                    if (auto* winMgr = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
                        winMgr->setPlayerDialogueOpen(targetPlayer->getId(), false);
                    Log(Debug::Info) << "[HeadlessServer] Received ID_ACTOR_AI (COMBAT): actor=" << actor.refId
                                     << " (" << actor.refNum << "-" << actor.mpNum << ") target pid="
                                     << targetPlayer->getId();
                    actorPtr.getClass().getCreatureStats(actorPtr).getAiSequence().stopPursuit();
                    MWBase::Environment::get().getMechanicsManager()->startCombat(actorPtr, targetAnchor, nullptr);
                }
            }
            mPendingActorSyncs.clear();
            return;
        }

        for (const QueuedActorSync& sync : mPendingActorSyncs)
        {
            for (const mwmp::BaseActor& actor : sync.mActorList.baseActors)
            {
                const ESM::Cell& targetEsmCell
                    = (sync.mPacketId == ID_ACTOR_CELL_CHANGE) ? actor.cell : sync.mActorList.cell;
                MWWorld::CellStore* targetCell = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, targetEsmCell);
                MWWorld::CellStore* sourceCell
                    = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, sync.mActorList.cell);
                MWWorld::Ptr actorPtr;

                if (sourceCell)
                    actorPtr = sourceCell->searchExact(actor.refNum, actor.mpNum, actor.refId, true);
                if (actorPtr.isEmpty() && targetCell && targetCell != sourceCell)
                    actorPtr = targetCell->searchExact(actor.refNum, actor.mpNum, actor.refId, true);

                if (actorPtr.isEmpty() || !targetCell)
                    continue;

                actorPtr = mWorld.moveObject(actorPtr, targetCell, actor.position.asVec3());
                mWorld.rotateObject(
                    actorPtr, osg::Vec3f(actor.position.rot[0], actor.position.rot[1], actor.position.rot[2]));
            }
        }

        mPendingActorSyncs.clear();
    }

    void HeadlessServer::broadcastAuthoritativeActorDeltas()
    {
        mReplication.broadcastAuthoritativeActorDeltas(
            mPlayersWithKnownCell, mConfig.mAuthority, mPeer, mNetworking.get(), mAnchors);
        if (mConfig.mAuthority.authoritativeMode)
            mDialogue.processPendingArrests(mPeer, mNetworking.get(), mAnchors);
    }

    void HeadlessServer::pollNetwork()
    {
        if (!mNetworking)
            return;

        mNetworking->pollOnce();
        syncPlayersToWorld();
        syncActorsToWorld();
    }

    void HeadlessServer::stepSmokeArrestCombat()
    {
        if (!mSmokeInitialized)
        {
            mSmokeInitialized = true;
            for (MWWorld::CellStore* cellStore : mWorld.getWorldScene().getActiveCells())
            {
                if (!cellStore || !cellStore->getCell()->isExterior())
                    continue;
                if (cellStore->getCell()->getGridX() != -3 || cellStore->getCell()->getGridY() != -2)
                    continue;
                cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                    if (!mSmokeGuard.isEmpty() || ptr.isEmpty() || !ptr.getClass().isActor())
                        return true;
                    if (ptr.getClass().isClass(ptr, "guard"))
                    {
                        mSmokeGuard = ptr;
                        return false;
                    }
                    return true;
                });
                if (!mSmokeGuard.isEmpty())
                    break;
            }

            if (!mSmokeGuard.isEmpty())
            {
                ESM::Position anchorPos = mSmokeGuard.getRefData().getPosition();
                const float yaw = anchorPos.rot[2];
                anchorPos.pos[0] += std::sin(yaw) * 120.f;
                anchorPos.pos[1] += std::cos(yaw) * 120.f;
                MWWorld::ManualRef ref(mWorld.getStore(), mWorld.getPlayerPtr(), 1);
                mSmokeAnchor = mWorld.placeObject(ref.getPtr(), mSmokeGuard.getCell(), anchorPos);
                if (!mSmokeAnchor.isEmpty())
                {
                    mWorld.enable(mSmokeAnchor);
                    if (MWPhysics::PhysicsSystem* phys = mWorld.getPhysicsSystem())
                    {
                        if (!phys->getActor(mSmokeAnchor))
                            phys->addActor(mSmokeAnchor, mSmokeAnchor.getClass().getCorrectedModel(mSmokeAnchor));
                        // Mirror HeadlessPlayerAnchorManager::ensurePhysicsActor: the smoke anchor is not simulated,
                        // so force its broadphase AABB to match its spawn position for projectile sweeps.
                        phys->updatePosition(mSmokeAnchor);
                    }
                    mSmokeAnchor.getClass().getCreatureStats(mSmokeAnchor).getAiSequence().clear();
                    MWMechanics::DynamicStat<float> hp(100.f);
                    mSmokeAnchor.getClass().getCreatureStats(mSmokeAnchor).setHealth(hp);
                    mSmokeAnchor.getClass().getNpcStats(mSmokeAnchor).setBounty(mSmokeBounty);

                    /*
                        Start of tes3mp addition

                        --smoke-ranged arms the smoke-test actor with a bow and arrows so the ranged attack
                        pipeline (releaseArrow -> launchProjectile -> projectileHit) is exercised, mirroring the
                        Moonmoth archer report instead of only the melee path.
                    */
                    if (mConfig.mSmokeRanged && mSmokeGuard.getClass().hasInventoryStore(mSmokeGuard))
                    {
                        MWWorld::InventoryStore& inv = mSmokeGuard.getClass().getInventoryStore(mSmokeGuard);
                        MWWorld::ContainerStore& store = mSmokeGuard.getClass().getContainerStore(mSmokeGuard);
                        inv.resolve();
                        if (inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight) != inv.end())
                            inv.unequipSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                        if (inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition) != inv.end())
                            inv.unequipSlot(MWWorld::InventoryStore::Slot_Ammunition);

                        const ESM::RefId bowId = ESM::RefId::stringRefId("steel longbow");
                        const ESM::RefId arrowId = ESM::RefId::stringRefId("iron arrow");
                        // add() defaults to allowAutoEquip, which would immediately put the actor's existing
                        // (melee) weapon back in hand. Add without auto-equip and equip the bow/arrows below.
                        MWWorld::ContainerStoreIterator bowIt = store.add(bowId, 1, false);
                        MWWorld::ContainerStoreIterator arrowIt = store.add(arrowId, 200, false);

                        if (bowIt != store.end())
                            inv.equip(MWWorld::InventoryStore::Slot_CarriedRight, bowIt);
                        if (arrowIt != store.end())
                            inv.equip(MWWorld::InventoryStore::Slot_Ammunition, arrowIt);

                        /*
                            Start of tes3mp addition

                            HeadlessReplicationSystem calls InventoryStore::autoEquip() when it first snapshots an
                            actor, which would swap the guard's default melee weapon back in for our bow. Drop every
                            other weapon so the only auto-equip candidate is the bow we just added.
                        */
                        std::vector<MWWorld::Ptr> staleWeapons;
                        for (auto it = store.begin(); it != store.end(); ++it)
                        {
                            if (it->getType() == ESM::Weapon::sRecordId
                                && it->getCellRef().getRefId() != bowId && it->getCellRef().getRefId() != arrowId)
                                staleWeapons.push_back(*it);
                        }
                        for (const MWWorld::Ptr& stale : staleWeapons)
                            store.remove(stale, stale.getCellRef().getCount());
                        /*
                            End of tes3mp addition
                        */

                        auto equipped = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                        Log(Debug::Info) << "[SmokeTest] Armed actor=" << mSmokeGuard.getCellRef().getRefId()
                                         << " rangedSmoke=1 equippedRight="
                                         << (equipped != inv.end() ? equipped->getCellRef().getRefId().serializeText()
                                                                   : std::string("<none>"));

                        /*
                            A low-level Balmora guard has almost no Marksman skill, so the upstream to-hit roll
                            (getHitChance) turns nearly every physically-landing arrow into a "miss". Give the
                            smoke archer a real Marksman skill so the test exercises the damage path deterministically.
                        */
                        MWMechanics::SkillValue marksman;
                        marksman.setBase(100.f);
                        mSmokeGuard.getClass().getNpcStats(mSmokeGuard).setSkill(ESM::Skill::Marksman, marksman);
                    }
                    /*
                        End of tes3mp addition
                    */

                    mSmokeGuard.getClass().getCreatureStats(mSmokeGuard).getAiSequence().stack(
                        MWMechanics::AiPursue(mSmokeAnchor), mSmokeGuard);
                    Log(Debug::Info) << "[SmokeTest] Spawned anchor in cell='"
                                     << mSmokeGuard.getCell()->getCell()->getDescription()
                                     << "' near guard=" << mSmokeGuard.getCellRef().getRefId()
                                     << " guardPos=(" << mSmokeGuard.getRefData().getPosition().pos[0] << ", "
                                     << mSmokeGuard.getRefData().getPosition().pos[1] << ", "
                                     << mSmokeGuard.getRefData().getPosition().pos[2] << ")"
                                     << " anchorPos=(" << anchorPos.pos[0] << ", " << anchorPos.pos[1] << ", "
                                     << anchorPos.pos[2] << ") bounty=" << mSmokeBounty;
                }
            }
            return;
        }

        if (mSmokeGuard.isEmpty() || mSmokeAnchor.isEmpty())
            return;

        static int sSmokeTick = 0;
        if ((++sSmokeTick % 30) == 0)
        {
            const osg::Vec3f gp = mSmokeGuard.getRefData().getPosition().asVec3();
            const osg::Vec3f ap = mSmokeAnchor.getRefData().getPosition().asVec3();
            const auto& seq = mSmokeGuard.getClass().getCreatureStats(mSmokeGuard).getAiSequence();
            Log(Debug::Info) << "[SmokeTest] tick=" << sSmokeTick << " dist=" << (gp - ap).length()
                             << " aiType=" << static_cast<int>(seq.getTypeId())
                             << " inPursuit=" << seq.isInPursuit()
                             << " inCombat=" << seq.isInCombat(mSmokeAnchor);
        }

        if (!mSmokeArrestTriggered)
        {
            if (auto* winMgr = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
            {
                for (const MWWorld::Ptr& ptr : winMgr->consumePendingDialogueActors())
                {
                    if (ptr == mSmokeGuard)
                    {
                        mSmokeArrestTriggered = true;
                        Log(Debug::Info) << "[SmokeTest] AiPursue reached anchor and triggered GM_Dialogue (ID_ACTOR_AI ARREST)";
                        mDialogue.executeDialogueChoice(mSmokeGuard, mSmokeAnchor, mSmokeBounty,
                            mwmp::DialogueChoiceType::TOPIC, "__CHOICE__:3",
                            mSmokeGuard.getCellRef().getRefId().serializeText(), 9999, mAnchors, mCombat);
                        break;
                    }
                }
            }
        }
        else
        {
            MWMechanics::CreatureStats& guardStats = mSmokeGuard.getClass().getCreatureStats(mSmokeGuard);
            if (!mSmokeWeaponDrawn && guardStats.getDrawState() == MWMechanics::DrawState::Weapon)
            {
                mSmokeWeaponDrawn = true;
                std::string weaponId = "<none>";
                std::string weaponClass = "?";
                if (mSmokeGuard.getClass().hasInventoryStore(mSmokeGuard))
                {
                    const MWWorld::InventoryStore& inv = mSmokeGuard.getClass().getInventoryStore(mSmokeGuard);
                    auto it = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                    if (it != inv.end())
                    {
                        weaponId = it->getCellRef().getRefId().serializeText();
                        if (it->getType() == ESM::Weapon::sRecordId)
                        {
                            const int cls
                                = MWMechanics::getWeaponType(it->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass;
                            weaponClass = cls == ESM::WeaponType::Ranged  ? "Ranged"
                                : cls == ESM::WeaponType::Thrown           ? "Thrown"
                                                                           : "Melee";
                        }
                    }
                }
                Log(Debug::Info) << "[SmokeTest] Guard drew weapon (DrawState::Weapon) in AiCombat weapon=" << weaponId
                                 << " class=" << weaponClass;
            }
            const bool attacking = guardStats.getAttackingOrSpell();
            if (attacking != mSmokeLastAttacking)
            {
                mSmokeLastAttacking = attacking;
                Log(Debug::Info) << "[SmokeTest] Guard attacking=" << (attacking ? 1 : 0) << " (ID_ACTOR_ATTACK)";
            }
            const float hp = mSmokeAnchor.getClass().getCreatureStats(mSmokeAnchor).getHealth().getCurrent();
            if (!mSmokeDamageObserved && hp < 100.f)
            {
                mSmokeDamageObserved = true;
                Log(Debug::Info) << "[SmokeTest] Player anchor took damage: health=" << hp
                                 << "/100 (ID_PLAYER_STATS_DYNAMIC)";
            }
        }
    }

    void HeadlessServer::broadcastNetworkState(float dt)
    {
        if (mConfig.mSmokeArrestCombat)
            stepSmokeArrestCombat();

        if (!mNetworking)
            return;

        mNetworking->tickTimers();

        const double interval = mConfig.mAuthority.networkBroadcastInterval;
        if (interval <= 0.0)
        {
            broadcastAuthoritativeActorDeltas();
            return;
        }

        mBroadcastAccumulator += static_cast<double>(std::max(0.f, dt));
        if (mBroadcastAccumulator >= interval)
        {
            mBroadcastAccumulator = std::fmod(mBroadcastAccumulator, interval);
            broadcastAuthoritativeActorDeltas();
        }
    }

    bool HeadlessServer::isRunning() const
    {
        return !mNetworking || mNetworking->isRunning();
    }

    void HeadlessServer::collectPlayerCells(std::vector<ESM::Cell>& outCells) const
    {
        TPlayers* players = Players::getPlayers();
        if (!players)
            return;

        for (const auto& [guid, player] : *players)
        {
            if (player && player->getLoadState() == Player::POSTLOADED
                && mPlayersWithKnownCell.find(player->getId()) != mPlayersWithKnownCell.end())
                outCells.push_back(player->cell);
        }
    }

    void HeadlessServer::collectSimulationObservers(
        std::vector<MWBase::MechanicsManager::SimulationObserver>& outObservers) const
    {
        outObservers.clear();
        std::vector<ConnectedObserver> connected;
        mReplication.collectConnectedObservers(mPlayersWithKnownCell, mAnchors, connected);
        if (connected.empty())
            return;

        // Use the live ID_PLAYER_POSITION coordinates so AI culling and DetourNavigator tile generation follow
        // the real player immediately, even before the world anchor catches up with a cell transition.
        outObservers.reserve(connected.size());
        for (const ConnectedObserver& obs : connected)
        {
            outObservers.push_back(MWBase::MechanicsManager::SimulationObserver{ obs.mPosition,
                obs.mCell ? obs.mCell->getCell()->getWorldSpace() : ESM::Cell::sDefaultWorldspaceId,
                obs.mIsExterior, obs.mCell });
        }
    }

    float HeadlessServer::getActorSimulationRadius() const
    {
        return mConfig.mAuthority.actorSimulationRadius;
    }

    uint16_t HeadlessServer::getBoundPort() const
    {
        return mBoundPort;
    }

    std::size_t HeadlessServer::getConnectedPlayerCount() const
    {
        std::vector<ConnectedObserver> connected;
        mReplication.collectConnectedObservers(mPlayersWithKnownCell, mAnchors, connected);
        return connected.size();
    }

    bool HeadlessServer::isAuthoritative() const
    {
        return mConfig.mAuthority.authoritativeMode;
    }

    std::uint64_t HeadlessServer::getAuthoritativeDeltaCount() const
    {
        return mReplication.getAuthoritativeDeltaCount();
    }

    void HeadlessServer::applyDynamicRecords(const mwmp::BaseWorldstate& worldstate)
    {
        try
        {
        auto applyAll = [](const auto& vec) {
            for (const auto& rec : vec)
                RecordHelper::overrideRecord(rec);
        };
        applyAll(worldstate.activatorRecords);
        applyAll(worldstate.apparatusRecords);
        applyAll(worldstate.armorRecords);
        applyAll(worldstate.bodyPartRecords);
        applyAll(worldstate.bookRecords);
        applyAll(worldstate.cellRecords);
        applyAll(worldstate.clothingRecords);
        applyAll(worldstate.containerRecords);
        applyAll(worldstate.creatureRecords);
        applyAll(worldstate.doorRecords);
        applyAll(worldstate.enchantmentRecords);
        applyAll(worldstate.gameSettingRecords);
        applyAll(worldstate.ingredientRecords);
        applyAll(worldstate.lightRecords);
        applyAll(worldstate.lockpickRecords);
        applyAll(worldstate.miscellaneousRecords);
        applyAll(worldstate.npcRecords);
        applyAll(worldstate.potionRecords);
        applyAll(worldstate.probeRecords);
        applyAll(worldstate.repairRecords);
        applyAll(worldstate.scriptRecords);
        applyAll(worldstate.soundRecords);
        applyAll(worldstate.spellRecords);
        applyAll(worldstate.staticRecords);
        applyAll(worldstate.weaponRecords);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "applyDynamicRecords exception: " << e.what();
        }
    }

    void HeadlessServer::handleLuaRecordCreated(const std::type_info& type, const void* recordPtr)
    {
        if (!recordPtr || !mNetworking)
            return;

        mwmp::BaseWorldstate ws;
        if (type == typeid(ESM::Activator))
        {
            ws.recordsType = mwmp::RECORD_TYPE::ACTIVATOR;
            mwmp::ActivatorRecord r{};
            r.data = *static_cast<const ESM::Activator*>(recordPtr);
            ws.activatorRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Armor))
        {
            ws.recordsType = mwmp::RECORD_TYPE::ARMOR;
            mwmp::ArmorRecord r{};
            r.data = *static_cast<const ESM::Armor*>(recordPtr);
            ws.armorRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Clothing))
        {
            ws.recordsType = mwmp::RECORD_TYPE::CLOTHING;
            mwmp::ClothingRecord r{};
            r.data = *static_cast<const ESM::Clothing*>(recordPtr);
            ws.clothingRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Book))
        {
            ws.recordsType = mwmp::RECORD_TYPE::BOOK;
            mwmp::BookRecord r{};
            r.data = *static_cast<const ESM::Book*>(recordPtr);
            ws.bookRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Enchantment))
        {
            ws.recordsType = mwmp::RECORD_TYPE::ENCHANTMENT;
            mwmp::EnchantmentRecord r{};
            r.data = *static_cast<const ESM::Enchantment*>(recordPtr);
            ws.enchantmentRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Miscellaneous))
        {
            ws.recordsType = mwmp::RECORD_TYPE::MISCELLANEOUS;
            mwmp::MiscellaneousRecord r{};
            r.data = *static_cast<const ESM::Miscellaneous*>(recordPtr);
            ws.miscellaneousRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Potion))
        {
            ws.recordsType = mwmp::RECORD_TYPE::POTION;
            mwmp::PotionRecord r{};
            r.data = *static_cast<const ESM::Potion*>(recordPtr);
            ws.potionRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Probe))
        {
            ws.recordsType = mwmp::RECORD_TYPE::PROBE;
            mwmp::ProbeRecord r{};
            r.data = *static_cast<const ESM::Probe*>(recordPtr);
            ws.probeRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Spell))
        {
            ws.recordsType = mwmp::RECORD_TYPE::SPELL;
            mwmp::SpellRecord r{};
            r.data = *static_cast<const ESM::Spell*>(recordPtr);
            ws.spellRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Static))
        {
            ws.recordsType = mwmp::RECORD_TYPE::STATIC;
            mwmp::StaticRecord r{};
            r.data = *static_cast<const ESM::Static*>(recordPtr);
            ws.staticRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::NPC))
        {
            ws.recordsType = mwmp::RECORD_TYPE::NPC;
            mwmp::NpcRecord r{};
            r.data = *static_cast<const ESM::NPC*>(recordPtr);
            ws.npcRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Creature))
        {
            ws.recordsType = mwmp::RECORD_TYPE::CREATURE;
            mwmp::CreatureRecord r{};
            r.data = *static_cast<const ESM::Creature*>(recordPtr);
            ws.creatureRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Container))
        {
            ws.recordsType = mwmp::RECORD_TYPE::CONTAINER;
            mwmp::ContainerRecord r{};
            r.data = *static_cast<const ESM::Container*>(recordPtr);
            ws.containerRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Door))
        {
            ws.recordsType = mwmp::RECORD_TYPE::DOOR;
            mwmp::DoorRecord r{};
            r.data = *static_cast<const ESM::Door*>(recordPtr);
            ws.doorRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Weapon))
        {
            ws.recordsType = mwmp::RECORD_TYPE::WEAPON;
            mwmp::WeaponRecord r{};
            r.data = *static_cast<const ESM::Weapon*>(recordPtr);
            ws.weaponRecords.push_back(std::move(r));
        }
        else if (type == typeid(ESM::Light))
        {
            ws.recordsType = mwmp::RECORD_TYPE::LIGHT;
            mwmp::LightRecord r{};
            r.data = *static_cast<const ESM::Light*>(recordPtr);
            ws.lightRecords.push_back(std::move(r));
        }
        else
            return;

        if (mwmp::WorldstatePacket* pkt
            = mNetworking->getWorldstatePacketController()->GetPacket(ID_RECORD_DYNAMIC))
        {
            ws.guid = RakNet::UNASSIGNED_CRABNET_GUID;
            pkt->setWorldstate(&ws);
            pkt->Send(true);
        }
    }

    void HeadlessServer::handleServerObjectEvent(unsigned char packetId, const mwmp::BaseObjectList& objectList)
    {
        if (!mConfig.mAuthority.authoritativeMode)
            return;

        try
        {
        MWWorld::CellStore* cellStore = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, objectList.cell);
        if (!cellStore)
            return;

        for (const mwmp::BaseObject& obj : objectList.baseObjects)
        {
            if (packetId == ID_OBJECT_PLACE || packetId == ID_OBJECT_SPAWN)
            {
                if (!cellStore->searchExact(0, obj.mpNum, obj.refId, true).isEmpty())
                    continue;
                try
                {
                    MWWorld::ManualRef ref(
                        mWorld.getStore(), ESM::RefId::deserializeText(obj.refId), std::max(1, obj.count));
                    MWWorld::Ptr newPtr = ref.getPtr();
                    if (obj.charge > -1)
                        newPtr.getCellRef().setCharge(obj.charge);
                    if (obj.enchantmentCharge > -1)
                        newPtr.getCellRef().setEnchantmentCharge(obj.enchantmentCharge);
                    if (!obj.soul.empty())
                        newPtr.getCellRef().setSoul(ESM::RefId::deserializeText(obj.soul));
                    MWWorld::Ptr placed = mWorld.placeObject(newPtr, cellStore, obj.position);
                    if (!placed.isEmpty())
                    {
                        placed.getCellRef().setMpNum(obj.mpNum);
                        if (obj.scale != 1.f && obj.scale > 0.f)
                            mWorld.scaleObject(placed, obj.scale);
                        mWorld.enable(placed);
                        if (placed.getClass().hasInventoryStore(placed))
                        {
                            placed.getClass().getInventoryStore(placed).resolve();
                            placed.getClass().getInventoryStore(placed).autoEquip();
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Server object spawn/place skipped '" << obj.refId << "': " << e.what();
                }
                continue;
            }

            MWWorld::Ptr targetPtr = cellStore->searchExact(obj.refNum, obj.mpNum, obj.refId, true);
            if (targetPtr.isEmpty())
                continue;

            switch (packetId)
            {
                case ID_OBJECT_DELETE:
                    mWorld.deleteObject(targetPtr);
                    break;
                case ID_OBJECT_MOVE:
                    mWorld.moveObject(targetPtr, obj.position.asVec3());
                    break;
                case ID_OBJECT_ROTATE:
                    mWorld.rotateObject(
                        targetPtr, osg::Vec3f(obj.position.rot[0], obj.position.rot[1], obj.position.rot[2]));
                    break;
                case ID_OBJECT_LOCK:
                    targetPtr.getCellRef().setLockLevel(obj.lockLevel);
                    break;
                case ID_DOOR_STATE:
                    if (targetPtr.getType() == ESM::Door::sRecordId)
                    {
                        MWWorld::DoorState state = MWWorld::DoorState::Idle;
                        if (obj.doorState == 1)
                            state = MWWorld::DoorState::Opening;
                        else if (obj.doorState == 2)
                            state = MWWorld::DoorState::Closing;
                        targetPtr.getClass().setDoorState(targetPtr, state);
                    }
                    break;
                case ID_CONTAINER:
                    if (targetPtr.getType() == ESM::Container::sRecordId
                        || targetPtr.getClass().hasInventoryStore(targetPtr))
                    {
                        MWWorld::ContainerStore& store = targetPtr.getClass().getContainerStore(targetPtr);
                        if (objectList.action == mwmp::BaseObjectList::SET)
                            store.clear();
                        for (const mwmp::ContainerItem& item : obj.containerItems)
                        {
                            try
                            {
                                const ESM::RefId itemId = ESM::RefId::deserializeText(item.refId);
                                if (objectList.action == mwmp::BaseObjectList::REMOVE)
                                    store.remove(itemId, std::max(1, item.count));
                                else if (item.count > 0)
                                    store.add(itemId, item.count, true);
                            }
                            catch (const std::exception& e)
                            {
                                Log(Debug::Warning) << "Authoritative ID_CONTAINER item skipped '" << item.refId
                                                    << "': " << e.what();
                            }
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "handleServerObjectEvent exception: " << e.what();
        }
    }

    void HeadlessServer::handleServerActorAI(const mwmp::BaseActorList& actorList)
    {
        if (!mConfig.mAuthority.authoritativeMode)
            return;

        MWWorld::CellStore* cellStore = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, actorList.cell);
        for (const mwmp::BaseActor& actor : actorList.baseActors)
        {
            MWWorld::Ptr actorPtr;
            if (cellStore)
                actorPtr = cellStore->searchExact(actor.refNum, actor.mpNum, actor.refId, true);
            if (actorPtr.isEmpty())
            {
                for (MWWorld::CellStore* activeCell : mWorld.getWorldScene().getActiveCells())
                {
                    actorPtr = activeCell->searchExact(actor.refNum, actor.mpNum, actor.refId, true);
                    if (!actorPtr.isEmpty())
                        break;
                }
            }
            if (actorPtr.isEmpty() || !actorPtr.getClass().isActor())
                continue;

            MWMechanics::AiSequence& seq = actorPtr.getClass().getCreatureStats(actorPtr).getAiSequence();
            MWWorld::Ptr aiTarget;
            if (actor.hasAiTarget
                && (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT
                    || actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW))
            {
                aiTarget = mCombat.resolveTargetPtr(actor.aiTarget, cellStore, mAnchors);
                if (aiTarget.isEmpty() && actor.aiAction == mwmp::BaseActorList::ACTIVATE)
                    aiTarget = mWorld.searchPtrViaUniqueIndex(actor.aiTarget.refNum, actor.aiTarget.mpNum);
            }

            switch (actor.aiAction)
            {
                case mwmp::BaseActorList::CANCEL:
                    seq.clear();
                    break;
                case mwmp::BaseActorList::ACTIVATE:
                    if (!aiTarget.isEmpty())
                        seq.stack(MWMechanics::AiActivate(aiTarget.getCellRef().getRefId(), actor.aiShouldRepeat),
                            actorPtr, true);
                    break;
                case mwmp::BaseActorList::COMBAT:
                    if (!aiTarget.isEmpty())
                        seq.stack(MWMechanics::AiCombat(aiTarget), actorPtr, true);
                    break;
                case mwmp::BaseActorList::ESCORT:
                    if (!aiTarget.isEmpty())
                        seq.stack(MWMechanics::AiEscort(aiTarget.getCellRef().getRefId(), {}, actor.aiDuration,
                                      actor.aiCoordinates.pos[0], actor.aiCoordinates.pos[1],
                                      actor.aiCoordinates.pos[2], actor.aiShouldRepeat),
                            actorPtr, true);
                    break;
                case mwmp::BaseActorList::FOLLOW:
                    if (!aiTarget.isEmpty())
                    {
                        MWMechanics::AiFollow package(aiTarget);
                        package.allowAnyDistance(true);
                        seq.stack(package, actorPtr, true);
                    }
                    break;
                case mwmp::BaseActorList::TRAVEL:
                    seq.stack(MWMechanics::AiTravel(actor.aiCoordinates.pos[0], actor.aiCoordinates.pos[1],
                                  actor.aiCoordinates.pos[2], actor.aiShouldRepeat),
                        actorPtr, true);
                    break;
                case mwmp::BaseActorList::WANDER:
                    seq.stack(MWMechanics::AiWander(static_cast<int>(actor.aiDistance), actor.aiDuration, -1, {},
                                  actor.aiShouldRepeat),
                        actorPtr, true);
                    break;
                default:
                    break;
            }
        }
    }
}
