#include "headlessserver.hpp"

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
#include <Script/LangLua/LangLua.hpp>
#include <Script/Script.hpp>
#include <Utils.hpp>

#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwlua/worldbindings.hpp>
#include <apps/openmw/mwmechanics/aiactivate.hpp>
#include <apps/openmw/mwmechanics/aicombat.hpp>
#include <apps/openmw/mwmechanics/aiescort.hpp>
#include <apps/openmw/mwmechanics/aifollow.hpp>
#include <apps/openmw/mwmechanics/aitravel.hpp>
#include <apps/openmw/mwmechanics/aiwander.hpp>
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/movement.hpp>
#include <apps/openmw/mwmechanics/spellcasting.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwmp/RecordHelper.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/raycasting.hpp>
#include <apps/openmw/mwworld/action.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
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

        ESM::Cell cellFromStore(const MWWorld::CellStore& store)
        {
            ESM::Cell out;
            out.blank();
            if (store.isExterior())
            {
                out.mData.mFlags &= ~ESM::Cell::Interior;
                out.mData.mX = store.getCell()->getGridX();
                out.mData.mY = store.getCell()->getGridY();
            }
            else
            {
                out.mData.mFlags |= ESM::Cell::Interior;
                out.mName = store.getCell()->getNameId();
            }
            return out;
        }

        unsigned int readMovementFlags(const MWMechanics::CreatureStats& stats)
        {
            unsigned int flags = 0;
            for (const auto bit : { MWMechanics::CreatureStats::Flag_ForceRun,
                     MWMechanics::CreatureStats::Flag_ForceSneak, MWMechanics::CreatureStats::Flag_Run,
                     MWMechanics::CreatureStats::Flag_Sneak, MWMechanics::CreatureStats::Flag_ForceJump,
                     MWMechanics::CreatureStats::Flag_ForceMoveJump })
            {
                if (stats.getMovementFlag(bit))
                    flags |= static_cast<unsigned int>(bit);
            }
            return flags;
        }

        mwmp::BaseActor buildBaseActor(const MWWorld::Ptr& ptr, const MWWorld::World& world, const ESM::Cell& cell,
            bool includePos, bool includeStats, bool includeDeath)
        {
            mwmp::BaseActor actor;
            actor.refId = ptr.getCellRef().getRefId().getRefIdString();
            actor.refNum = ptr.getCellRef().getRefNum().mIndex;
            actor.mpNum = ptr.getCellRef().getMpNum();
            actor.cell = cell;

            const ESM::Position& pos = ptr.getRefData().getPosition();
            actor.position = pos;
            const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
            actor.direction.pos[0] = movement.mPosition[0];
            actor.direction.pos[1] = movement.mPosition[1];
            actor.direction.pos[2] = movement.mPosition[2];
            actor.direction.rot[0] = movement.mRotation[0];
            actor.direction.rot[1] = movement.mRotation[1];
            actor.direction.rot[2] = movement.mRotation[2];
            actor.hasPositionData = includePos;

            const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
            actor.movementFlags = readMovementFlags(stats);
            actor.drawState = static_cast<char>(stats.getDrawState());
            actor.isFlying = world.isFlying(ptr);

            stats.getDynamic(0).writeState(actor.creatureStats.mDynamic[0]);
            stats.getDynamic(1).writeState(actor.creatureStats.mDynamic[1]);
            stats.getDynamic(2).writeState(actor.creatureStats.mDynamic[2]);
            actor.creatureStats.mDead = stats.isDead();
            actor.creatureStats.mDeathAnimationFinished = stats.isDeathAnimationFinished();
            actor.hasStatsDynamicData = includeStats;

            if (includeDeath)
            {
                actor.deathState = stats.isDeathAnimationFinished() ? 2 : 1;
                actor.isInstantDeath = false;
                actor.killer.isPlayer = false;
                actor.killer.refNum = 0;
                actor.killer.mpNum = 0;
            }
            return actor;
        }

        std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }

        std::uint64_t hashEquipmentSlot(std::uint64_t h, const std::string& refId, int count)
        {
            h = hashCombine(h, std::hash<std::string>{}(refId));
            return hashCombine(h, static_cast<std::uint64_t>(count));
        }

        std::uint64_t computeEquipmentHash(const MWWorld::Ptr& ptr)
        {
            std::uint64_t outHash = 1469598103934665603ULL;
            if (!ptr.getClass().hasInventoryStore(ptr))
                return outHash;

            MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                const MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);
                if (it != inv.end())
                    outHash = hashEquipmentSlot(outHash, it->getCellRef().getRefId().serializeText(), it->getCellRef().getCount());
                else
                    outHash = hashEquipmentSlot(outHash, std::string(), 0);
            }
            return outHash;
        }

        void populateActorEquipment(const MWWorld::Ptr& ptr, mwmp::BaseActor& actor, std::uint64_t& outHash)
        {
            outHash = 1469598103934665603ULL;
            if (!ptr.getClass().hasInventoryStore(ptr))
                return;

            MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                mwmp::Item& item = actor.equipmentItems[slot];
                item = {};
                const MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);
                if (it != inv.end())
                {
                    item.refId = it->getCellRef().getRefId().serializeText();
                    item.count = it->getCellRef().getCount();
                    item.charge = it->getCellRef().getCharge();
                    item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                }
                outHash = hashEquipmentSlot(outHash, item.refId, item.count);
            }
        }

        std::uint64_t computeContainerHash(MWWorld::ContainerStore& store)
        {
            std::uint64_t h = 1469598103934665603ULL;
            for (MWWorld::ConstContainerStoreIterator it = store.cbegin(); it != store.cend(); ++it)
            {
                h = hashCombine(h, std::hash<std::string>{}(it->getCellRef().getRefId().serializeText()));
                h = hashCombine(h, static_cast<std::uint64_t>(it->getCellRef().getCount()));
            }
            return h;
        }

        mwmp::BaseObject buildContainerObject(const MWWorld::Ptr& ptr, MWWorld::ContainerStore& store)
        {
            mwmp::BaseObject obj;
            obj.refId = ptr.getCellRef().getRefId().serializeText();
            obj.refNum = ptr.getCellRef().getRefNum().mIndex;
            obj.mpNum = ptr.getCellRef().getMpNum();
            obj.position = ptr.getRefData().getPosition();
            for (MWWorld::ConstContainerStoreIterator it = store.cbegin(); it != store.cend(); ++it)
            {
                mwmp::ContainerItem ci;
                ci.refId = it->getCellRef().getRefId().serializeText();
                ci.count = it->getCellRef().getCount();
                ci.charge = it->getCellRef().getCharge();
                ci.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                ci.soul = it->getCellRef().getSoul().serializeText();
                ci.actionCount = ci.count;
                obj.containerItems.push_back(std::move(ci));
            }
            return obj;
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
    {
    }

    HeadlessServer::~HeadlessServer()
    {
        shutdown();
    }

    namespace
    {
        std::filesystem::path resolveConfigPathImpl(
            Files::ConfigurationManager& cfgMgr, const std::filesystem::path& candidate)
        {
            if (candidate.empty())
                return {};
            if (std::filesystem::exists(candidate))
                return candidate;
            if (candidate.is_relative())
            {
                const std::filesystem::path inLocal = cfgMgr.getLocalPath() / candidate;
                if (std::filesystem::exists(inLocal))
                    return inLocal;
                const std::filesystem::path inGlobal = cfgMgr.getGlobalPath() / candidate;
                if (std::filesystem::exists(inGlobal))
                    return inGlobal;
            }
            return {};
        }

        std::filesystem::path resolvePluginHomeImpl(
            Files::ConfigurationManager& cfgMgr, const std::string& rawHome, const std::filesystem::path& cfgDir)
        {
            if (rawHome.empty())
                return {};
            const std::filesystem::path rawPath(rawHome);
            if (std::filesystem::exists(rawPath))
                return rawPath;
            if (!cfgDir.empty())
            {
                const std::filesystem::path nearCfg = cfgDir / rawPath;
                if (std::filesystem::exists(nearCfg))
                    return nearCfg;
            }
            const std::filesystem::path nearLocal = cfgMgr.getLocalPath() / rawPath;
            if (std::filesystem::exists(nearLocal))
                return nearLocal;
            return rawPath;
        }
    }

    std::filesystem::path HeadlessServer::resolveServerHome(
        Files::ConfigurationManager& cfgMgr, const std::filesystem::path& serverCfgPath)
    {
        Settings::CategorySettingValueMap serverSettings;
        Settings::SettingsFileParser parser;

        std::filesystem::path defaultCfg = resolveConfigPathImpl(
            cfgMgr, serverCfgPath.empty() ? std::filesystem::path("tes3mp-server-default.cfg") : serverCfgPath);
        if (defaultCfg.empty())
            defaultCfg = resolveConfigPathImpl(cfgMgr, "tes3mp-server-default.cfg");

        std::filesystem::path cfgDir;
        try
        {
            if (!defaultCfg.empty())
            {
                parser.loadSettingsFile(defaultCfg, serverSettings, false, true);
                cfgDir = defaultCfg.parent_path();
                const std::filesystem::path siblingOverride = cfgDir / "tes3mp-server.cfg";
                if (siblingOverride != defaultCfg && std::filesystem::exists(siblingOverride))
                    parser.loadSettingsFile(siblingOverride, serverSettings, false, true);
            }
            const std::filesystem::path userCfg = cfgMgr.getUserConfigPath() / "tes3mp-server.cfg";
            if (std::filesystem::exists(userCfg) && userCfg != defaultCfg)
                parser.loadSettingsFile(userCfg, serverSettings, false, true);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to read server config while resolving server home: " << e.what();
        }

        const std::string rawPluginHome = getCfgString(serverSettings, "Plugins", "home", "./server");
        std::filesystem::path home = resolvePluginHomeImpl(cfgMgr, rawPluginHome, cfgDir);
        if (home.empty())
            home = cfgMgr.getLocalPath() / "server";
        return home;
    }

    std::filesystem::path HeadlessServer::resolveConfigPath(const std::filesystem::path& candidate) const
    {
        return resolveConfigPathImpl(mCfgMgr, candidate);
    }

    std::filesystem::path HeadlessServer::resolvePluginHome(
        const std::string& rawHome, const std::filesystem::path& cfgDir) const
    {
        return resolvePluginHomeImpl(mCfgMgr, rawHome, cfgDir);
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
            if (!mConfig.mAuthority.authoritativeMode)
                mPendingActorSyncs.push_back(QueuedActorSync{ packetId, list });
        });
        mNetworking->setPlayerPositionCallback(
            [this](::Player& player) { return validatePlayerPosition(player); });
        mNetworking->setPlayerCellChangeCallback(
            [this](::Player& player) { handlePlayerCellChange(player); });
        mNetworking->setPlayerAttackCallback(
            [this](::Player& player, mwmp::Attack& attack) { return handlePlayerAttack(player, attack); });
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
        MWLua::setHeadlessRecordCreatedCallback(
            [this](const std::type_info& type, const void* recordPtr) { handleLuaRecordCreated(type, recordPtr); });

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
        MWLua::setHeadlessRecordCreatedCallback(nullptr);
        mCellInstances.clear();

        for (auto& [pid, ptr] : mPlayerAnchors)
        {
            if (!ptr.isEmpty())
                mWorld.deleteObject(ptr);
        }
        mPlayerAnchors.clear();
        mPlayerValidation.clear();
        mActorSnapshots.clear();
        mContainerSnapshots.clear();
        mPendingActorSyncs.clear();
        mBroadcastAccumulator = 0.0;
        mAuthoritativeDeltaCount = 0;
        mDeltaStats = {};
        mStallBroadcastCycles = 0;
        mContainerSweepCounter = 0;

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

    MWWorld::CellStore* HeadlessServer::resolveCellStore(const ESM::Cell& cell) const
    {
        MWWorld::WorldModel& worldModel = mWorld.getWorldModel();
        if (cell.isExterior())
        {
            return &worldModel.getExterior(
                ESM::ExteriorCellLocation(cell.mData.mX, cell.mData.mY, ESM::Cell::sDefaultWorldspaceId), true);
        }
        return worldModel.findInterior(cell.mName, true);
    }

    MWWorld::Ptr HeadlessServer::resolveTargetPtr(const mwmp::Target& target, const MWWorld::CellStore* hintCell) const
    {
        if (target.isPlayer)
        {
            if (::Player* targetPlayer = Players::getPlayer(target.guid))
            {
                auto it = mPlayerAnchors.find(targetPlayer->getId());
                if (it != mPlayerAnchors.end())
                    return it->second;
            }
            return MWWorld::Ptr();
        }

        if (hintCell)
        {
            MWWorld::Ptr found
                = const_cast<MWWorld::CellStore*>(hintCell)->searchExact(target.refNum, target.mpNum, target.refId);
            if (!found.isEmpty())
                return found;
        }

        for (MWWorld::CellStore* activeCell : mWorld.getWorldScene().getActiveCells())
        {
            MWWorld::Ptr found = activeCell->searchExact(target.refNum, target.mpNum, target.refId);
            if (!found.isEmpty())
                return found;
        }
        return MWWorld::Ptr();
    }

    bool HeadlessServer::isPlayerAnchorPtr(const MWWorld::Ptr& ptr) const
    {
        if (ptr == mWorld.getPlayerPtr())
            return true;
        for (const auto& [pid, anchor] : mPlayerAnchors)
        {
            if (anchor == ptr)
                return true;
        }
        return false;
    }

    void HeadlessServer::collectConnectedObservers(std::vector<ConnectedObserver>& outObservers) const
    {
        outObservers.clear();
        TPlayers* players = Players::getPlayers();
        if (!players)
            return;

        for (const auto& [guid, player] : *players)
        {
            if (!player || player->getLoadState() != Player::POSTLOADED)
                continue;
            // Never treat the default (0, 0) cell of a freshly connected player as real until the client
            // confirms its cell via ID_PLAYER_CELL_CHANGE.
            if (mPlayersWithKnownCell.find(player->getId()) == mPlayersWithKnownCell.end())
                continue;

            ConnectedObserver obs;
            obs.mPlayer = player;
            obs.mPosition = player->position.asVec3();
            obs.mIsExterior = player->cell.isExterior();
            obs.mCell = resolveCellStore(player->cell);
            if (obs.mIsExterior)
            {
                const ESM::ExteriorCellLocation posCell
                    = ESM::positionToExteriorCellLocation(obs.mPosition.x(), obs.mPosition.y(), ESM::Cell::sDefaultWorldspaceId);
                if (posCell.mX != player->cell.mData.mX || posCell.mY != player->cell.mData.mY)
                {
                    bool usedAnchor = false;
                    auto anchorIt = mPlayerAnchors.find(player->getId());
                    if (anchorIt != mPlayerAnchors.end() && !anchorIt->second.isEmpty()
                        && anchorIt->second.isInCell() && anchorIt->second.getCell()->isExterior())
                    {
                        const osg::Vec3f anchorPos = anchorIt->second.getRefData().getPosition().asVec3();
                        const ESM::ExteriorCellLocation anchorCell = ESM::positionToExteriorCellLocation(
                            anchorPos.x(), anchorPos.y(), ESM::Cell::sDefaultWorldspaceId);
                        if (anchorCell.mX == player->cell.mData.mX && anchorCell.mY == player->cell.mData.mY)
                        {
                            obs.mPosition = anchorPos;
                            usedAnchor = true;
                        }
                    }
                    if (!usedAnchor)
                    {
                        const float cellSize = ESM::getCellSize(ESM::Cell::sDefaultWorldspaceId);
                        obs.mPosition.x() = (player->cell.mData.mX + 0.5f) * cellSize;
                        obs.mPosition.y() = (player->cell.mData.mY + 0.5f) * cellSize;
                    }
                }
            }
            outObservers.push_back(obs);
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

    bool HeadlessServer::handlePlayerAttack(::Player& player, mwmp::Attack& attack)
    {
        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validateCombat)
            return true;

        auto anchorIt = mPlayerAnchors.find(player.getId());
        if (anchorIt == mPlayerAnchors.end() || anchorIt->second.isEmpty())
            return true;

        MWWorld::Ptr attackerPtr = anchorIt->second;
        MWWorld::Ptr victimPtr = resolveTargetPtr(attack.target, attackerPtr.isInCell() ? attackerPtr.getCell() : nullptr);

        if (!attack.pressed && !attack.isHit)
            return true;

        if (victimPtr.isEmpty() || !victimPtr.getClass().isActor())
        {
            attack.isHit = false;
            attack.damage = 0.f;
            return true;
        }

        MWMechanics::CreatureStats& victimStats = victimPtr.getClass().getCreatureStats(victimPtr);
        if (victimStats.isDead())
        {
            attack.isHit = false;
            attack.damage = 0.f;
            return false;
        }

        MWWorld::InventoryStore& inv = attackerPtr.getClass().getInventoryStore(attackerPtr);
        MWWorld::ContainerStoreIterator weaponIt = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        MWWorld::Ptr weapon;
        if (weaponIt != inv.end() && weaponIt->getType() == ESM::Weapon::sRecordId)
            weapon = *weaponIt;

        if (attack.type == mwmp::Attack::MELEE)
        {
            const float reach = MWMechanics::getMeleeWeaponReach(attackerPtr, weapon);
            if (!MWMechanics::isInMeleeReach(attackerPtr, victimPtr, reach))
            {
                Log(Debug::Verbose) << "Authoritative combat rejected out-of-reach melee attack from "
                                    << player.npc.mName;
                attack.isHit = false;
                attack.damage = 0.f;
                return false;
            }
        }

        const MWPhysics::RayCastingInterface* rayCasting = mWorld.getRayCasting();
        if (rayCasting && !rayCasting->getLineOfSight(attackerPtr, victimPtr))
        {
            const osg::Vec3f from = attackerPtr.getRefData().getPosition().asVec3() + osg::Vec3f(0.f, 0.f, 96.f);
            const osg::Vec3f to = victimPtr.getRefData().getPosition().asVec3() + osg::Vec3f(0.f, 0.f, 64.f);
            const MWPhysics::RayCastingResult los = rayCasting->castRay(
                from, to, { attackerPtr }, {}, MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap);
            if (los.mHit && los.mHitObject != victimPtr)
            {
                Log(Debug::Verbose) << "Authoritative combat blocked obstructed attack from " << player.npc.mName;
                attack.isHit = false;
                attack.damage = 0.f;
                return false;
            }
        }

        const float strength = std::clamp(attack.attackStrength > 0.f ? attack.attackStrength : 0.75f, 0.f, 1.f);
        MWMechanics::applyFatigueLoss(attackerPtr, weapon, strength);

        const ESM::RefId skillId = !weapon.isEmpty() ? weapon.getClass().getEquipmentSkill(weapon) : ESM::Skill::HandToHand;
        const float skillValue = attackerPtr.getClass().getSkill(attackerPtr, skillId);
        const float hitChance = MWMechanics::getHitChance(attackerPtr, victimPtr, static_cast<int>(skillValue));
        const bool hitSuccess = Misc::Rng::roll0to99(mWorld.getPrng()) < hitChance;

        attack.success = hitSuccess;
        attack.isHit = hitSuccess;
        if (!hitSuccess)
        {
            attack.damage = 0.f;
            return true;
        }

        float damage = 0.f;
        bool healthDmg = true;
        if (!weapon.isEmpty())
        {
            const auto& weapData = weapon.get<ESM::Weapon>()->mBase->mData;
            const unsigned char* atkData = (attack.attackAnimation == "slash")
                ? weapData.mSlash.data()
                : (attack.attackAnimation == "thrust") ? weapData.mThrust.data() : weapData.mChop.data();
            damage = atkData[0] + (atkData[1] - atkData[0]) * strength;
            MWMechanics::adjustWeaponDamage(damage, weapon, attackerPtr);
            MWMechanics::resistNormalWeapon(victimPtr, attackerPtr, weapon, damage);
        }
        else
        {
            MWMechanics::getHandToHandDamage(attackerPtr, victimPtr, damage, healthDmg, strength);
        }

        const float armor = std::max(0.f, victimPtr.getClass().getArmorRating(victimPtr, false));
        if (armor > 0.f && damage > 0.f)
        {
            const float reduction = damage / (damage + armor);
            damage *= std::max(0.25f, reduction);
        }

        if (MWMechanics::blockMeleeAttack(attackerPtr, victimPtr, weapon, damage, strength))
        {
            attack.block = true;
            damage = 0.f;
        }

        attack.damage = damage;
        const bool wasDead = victimStats.isDead();
        const ESM::RefId weaponId = !weapon.isEmpty() ? weapon.getCellRef().getRefId() : ESM::RefId();
        victimPtr.getClass().onHit(victimPtr, { { healthDmg ? "health" : "fatigue", damage } }, weaponId, attackerPtr,
            true,
            attack.type == mwmp::Attack::RANGED ? MWMechanics::DamageSourceType::Ranged
                                                : MWMechanics::DamageSourceType::Melee);

        if (attack.target.isPlayer)
        {
            if (::Player* victimPlayer = Players::getPlayer(attack.target.guid))
            {
                victimStats.getDynamic(0).writeState(victimPlayer->creatureStats.mDynamic[0]);
                victimStats.getDynamic(2).writeState(victimPlayer->creatureStats.mDynamic[2]);
                mwmp::PlayerPacket* statsPkt
                    = mNetworking->getPlayerPacketController()->GetPacket(ID_PLAYER_STATS_DYNAMIC);
                statsPkt->setPlayer(victimPlayer);
                statsPkt->Send(victimPlayer->guid);
                statsPkt->Send(true);

                if (!wasDead && victimStats.isDead())
                {
                    victimPlayer->creatureStats.mDead = true;
                    victimPlayer->deathState = 1;
                    victimPlayer->killer.isPlayer = true;
                    victimPlayer->killer.guid = player.guid;
                    mwmp::PlayerPacket* deathPkt
                        = mNetworking->getPlayerPacketController()->GetPacket(ID_PLAYER_DEATH);
                    deathPkt->setPlayer(victimPlayer);
                    deathPkt->Send(victimPlayer->guid);
                    deathPkt->Send(true);
                    Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(victimPlayer->getId());
                }
            }
        }

        return true;
    }

    bool HeadlessServer::handlePlayerCast(::Player& player, mwmp::Cast& cast)
    {
        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validateCombat)
            return true;

        auto anchorIt = mPlayerAnchors.find(player.getId());
        if (anchorIt == mPlayerAnchors.end() || anchorIt->second.isEmpty())
            return true;

        MWWorld::Ptr casterPtr = anchorIt->second;
        MWWorld::Ptr targetPtr = resolveTargetPtr(cast.target, casterPtr.isInCell() ? casterPtr.getCell() : nullptr);

        if (!cast.spellId.empty())
        {
            const ESM::Spell* spell
                = mWorld.getStore().get<ESM::Spell>().search(ESM::RefId::stringRefId(cast.spellId));
            if (!spell)
                return false;

            if (!targetPtr.isEmpty() && targetPtr != casterPtr && mWorld.getRayCasting()
                && !mWorld.getRayCasting()->getLineOfSight(casterPtr, targetPtr))
            {
                cast.isHit = false;
                return false;
            }

            MWMechanics::CastSpell spellCast(casterPtr, targetPtr);
            if (!targetPtr.isEmpty())
                spellCast.mHitPosition = targetPtr.getRefData().getPosition().asVec3();
            cast.success = spellCast.cast(spell);
        }

        return true;
    }

    bool HeadlessServer::handleObjectAction(unsigned char packetId, ::Player& player, mwmp::BaseObjectList& objectList)
    {
        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validateObjects)
            return true;

        if (objectList.packetOrigin == mwmp::SERVER_SCRIPT)
            return true;

        MWWorld::CellStore* cellStore = resolveCellStore(objectList.cell);
        if (!cellStore || !mWorld.getWorldScene().isCellActive(*cellStore))
        {
            Log(Debug::Warning) << "Rejected object packet " << static_cast<int>(packetId) << " from "
                                << player.npc.mName << " targeting inactive cell "
                                << Utils::getCellDescription(objectList.cell);
            return false;
        }

        MWWorld::Ptr playerAnchor;
        if (auto it = mPlayerAnchors.find(player.getId()); it != mPlayerAnchors.end())
            playerAnchor = it->second;

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
                    MWWorld::ManualRef ref(mWorld.getStore(), ESM::RefId::stringRefId(obj.refId), std::max(1, obj.count));
                    MWWorld::Ptr placed = mWorld.placeObject(ref.getPtr(), cellStore, obj.position);
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
                                const ESM::RefId itemId = ESM::RefId::stringRefId(item.refId);
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

            MWWorld::CellStore* targetCell = resolveCellStore(player->cell);
            if (!targetCell || !mWorld.getWorldScene().isCellActive(*targetCell))
                continue;

            auto anchorIt = mPlayerAnchors.find(pid);
            if (anchorIt == mPlayerAnchors.end() || anchorIt->second.isEmpty())
            {
                MWWorld::ManualRef ref(mWorld.getStore(), mWorld.getPlayerPtr(), 1);
                MWWorld::Ptr anchor = mWorld.placeObject(ref.getPtr(), targetCell, player->position);
                if (!anchor.isEmpty())
                {
                    mWorld.enable(anchor);
                    anchor.getClass().getCreatureStats(anchor).getAiSequence().clear();
                    mPlayerAnchors[pid] = anchor;
                }
            }
            else
            {
                MWWorld::Ptr anchor = anchorIt->second;
                if (anchor.getCell() != targetCell
                    || (anchor.getRefData().getPosition().asVec3() - player->position.asVec3()).length2() > 0.0001f)
                {
                    anchor = mWorld.moveObject(anchor, targetCell, player->position.asVec3());
                    anchorIt->second = anchor;
                }
                mWorld.rotateObject(
                    anchor, osg::Vec3f(player->position.rot[0], player->position.rot[1], player->position.rot[2]));
            }
        }

        for (auto it = mPlayersWithKnownCell.begin(); it != mPlayersWithKnownCell.end();)
        {
            if (activeIds.find(*it) == activeIds.end()
                && (!Players::getPlayers() || !Players::getPlayer(*it)))
            {
                mCellInstances.removePlayerFromParty(*it);
                it = mPlayersWithKnownCell.erase(it);
            }
            else
                ++it;
        }

        for (auto it = mPlayerAnchors.begin(); it != mPlayerAnchors.end();)
        {
            if (activeIds.find(it->first) == activeIds.end())
            {
                if (!it->second.isEmpty())
                    mWorld.deleteObject(it->second);
                mPlayerValidation.erase(it->first);
                it = mPlayerAnchors.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void HeadlessServer::syncActorsToWorld()
    {
        if (mPendingActorSyncs.empty() || mConfig.mAuthority.authoritativeMode)
        {
            mPendingActorSyncs.clear();
            return;
        }

        for (const QueuedActorSync& sync : mPendingActorSyncs)
        {
            for (const mwmp::BaseActor& actor : sync.mActorList.baseActors)
            {
                const ESM::Cell& targetEsmCell
                    = (sync.mPacketId == ID_ACTOR_CELL_CHANGE) ? actor.cell : sync.mActorList.cell;
                MWWorld::CellStore* targetCell = resolveCellStore(targetEsmCell);
                MWWorld::CellStore* sourceCell = resolveCellStore(sync.mActorList.cell);
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
        if (!mConfig.mAuthority.authoritativeMode || !mNetworking)
            return;

        // Pause actor delta broadcasting entirely while no client is connected.
        std::vector<ConnectedObserver> connected;
        collectConnectedObservers(connected);
        if (connected.empty())
            return;

        mwmp::ActorPacketController* actorPackets = mNetworking->getActorPacketController();
        if (!actorPackets)
            return;

        const float posThreshSq = mConfig.mAuthority.actorPosDeltaSq;
        const float rotThresh = mConfig.mAuthority.actorRotDeltaRad;
        const float statThresh = mConfig.mAuthority.actorStatDelta;
        const float broadcastRadius = std::max(mConfig.mAuthority.actorSimulationRadius, mConfig.mAuthority.actorBroadcastRadius);
        const float broadcastRadiusSq = broadcastRadius * broadcastRadius;
        const std::size_t maxBatch = std::max<std::size_t>(1, mConfig.mAuthority.maxActorsPerBroadcast);
        const bool sweepContainersThisCycle = ((mContainerSweepCounter++ % 20) == 0);
        const std::uint64_t initialDeltaCount = mAuthoritativeDeltaCount;
        std::size_t evaluatedActors = 0;

        for (MWWorld::CellStore* cellStore : mWorld.getWorldScene().getActiveCells())
        {
            if (!cellStore)
                continue;

            ESM::Cell esmCell = cellFromStore(*cellStore);
            // Legacy CellController state is optional: it only exists if a client sent ID_PLAYER_CELL_STATE.
            Cell* serverCell = CellController::get()->findCell(&esmCell);

            const bool isExterior = cellStore->isExterior();

            // Candidate recipients for this cell, using live player positions.
            std::vector<const ConnectedObserver*> candidates;
            for (const ConnectedObserver& obs : connected)
            {
                if (isExterior ? obs.mIsExterior : (obs.mCell == cellStore))
                    candidates.push_back(&obs);
            }
            if (serverCell)
            {
                // Also include anyone the legacy CellController knows has this cell loaded.
                for (const ::Player* pl : serverCell->getPlayers())
                {
                    if (!pl)
                        continue;
                    for (const ConnectedObserver& obs : connected)
                    {
                        if (obs.mPlayer == pl
                            && std::find(candidates.begin(), candidates.end(), &obs) == candidates.end())
                            candidates.push_back(&obs);
                    }
                }
            }
            if (candidates.empty())
                continue;

            std::vector<bool> recipientInRange(candidates.size(), !isExterior);

            mwmp::BaseActorList posBatch;
            mwmp::BaseActorList animBatch;
            mwmp::BaseActorList statsBatch;
            mwmp::BaseActorList equipBatch;
            mwmp::BaseActorList deathBatch;
            posBatch.cell = animBatch.cell = statsBatch.cell = equipBatch.cell = deathBatch.cell = esmCell;

            mwmp::BaseObjectList containerBatch;
            containerBatch.cell = esmCell;
            containerBatch.action = mwmp::BaseObjectList::SET;
            containerBatch.containerSubAction = mwmp::BaseObjectList::NONE;
            containerBatch.packetOrigin = mwmp::SERVER_SCRIPT;

            cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || isPlayerAnchorPtr(ptr))
                    return true;

                const bool isActor = ptr.getClass().isActor();
                const bool isContainer = ptr.getType() == ESM::Container::sRecordId;
                if (!isActor && !isContainer)
                    return true;

                const ESM::Position& esmPos = ptr.getRefData().getPosition();
                const osg::Vec3f pos = esmPos.asVec3();
                if (isExterior)
                {
                    bool inVisibilityRadius = false;
                    for (std::size_t i = 0; i < candidates.size(); ++i)
                    {
                        if ((candidates[i]->mPosition - pos).length2() <= broadcastRadiusSq)
                        {
                            inVisibilityRadius = true;
                            recipientInRange[i] = true;
                        }
                    }
                    if (!inVisibilityRadius)
                        return true;
                }

                const MWWorld::LiveCellRefBase* key = ptr.getBase();

                if (isContainer)
                {
                    const bool isNewContainer = (mContainerSnapshots.find(key) == mContainerSnapshots.end());
                    if ((isNewContainer || sweepContainersThisCycle) && containerBatch.baseObjects.size() < maxBatch)
                    {
                        MWWorld::ContainerStore& store = const_cast<MWWorld::Ptr&>(ptr).getClass().getContainerStore(ptr);
                        if (!store.isResolved())
                            store.resolve();
                        const std::uint64_t cHash = computeContainerHash(store);
                        auto [cIt, cInserted] = mContainerSnapshots.try_emplace(key, cHash);
                        if (cInserted || cIt->second != cHash)
                        {
                            cIt->second = cHash;
                            containerBatch.baseObjects.push_back(buildContainerObject(ptr, store));
                            ++mAuthoritativeDeltaCount;
                        }
                    }
                    return true;
                }

                ++evaluatedActors;

                const osg::Vec3f rot = esmPos.asRotationVec3();
                const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                const unsigned int moveFlags = readMovementFlags(stats);
                const char drawState = static_cast<char>(stats.getDrawState());
                const bool flying = mWorld.isFlying(ptr);
                const float health = stats.getHealth().getCurrent();
                const float magicka = stats.getMagicka().getCurrent();
                const float fatigue = stats.getFatigue().getCurrent();
                const bool dead = stats.isDead();

                auto [it, inserted] = mActorSnapshots.try_emplace(key);
                ActorSnapshot& snap = it->second;
                if (inserted || !snap.mInitialized)
                {
                    if (ptr.getClass().hasInventoryStore(ptr))
                    {
                        MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
                        inv.resolve();
                        if (!dead)
                            inv.autoEquip();
                    }
                    snap = ActorSnapshot{
                        pos, rot, moveFlags, drawState, flying, health, magicka, fatigue, dead, dead, 0, false, false, true
                    };
                    if (ptr.getClass().hasInventoryStore(ptr) && equipBatch.baseActors.size() < maxBatch)
                    {
                        mwmp::BaseActor equipActor = buildBaseActor(ptr, mWorld, esmCell, false, false, false);
                        populateActorEquipment(ptr, equipActor, snap.mEquipmentHash);
                        snap.mEquipmentBroadcasted = true;
                        equipBatch.baseActors.push_back(std::move(equipActor));
                        ++mAuthoritativeDeltaCount;
                        ++mDeltaStats.mEquip;
                    }
                    return true;
                }

                // Reset one-shot death flag only if the actor was explicitly resurrected.
                if (!dead && snap.mDeathBroadcasted)
                    snap.mDeathBroadcasted = false;

                // Resolve and broadcast corpse inventory once dead.
                if (dead && ptr.getClass().hasInventoryStore(ptr) && containerBatch.baseObjects.size() < maxBatch)
                {
                    MWWorld::ContainerStore& store = const_cast<MWWorld::Ptr&>(ptr).getClass().getContainerStore(ptr);
                    store.resolve();
                    const std::uint64_t cHash = computeContainerHash(store);
                    auto [cIt, cInserted] = mContainerSnapshots.try_emplace(key, cHash);
                    if (cInserted || cIt->second != cHash)
                    {
                        cIt->second = cHash;
                        containerBatch.baseObjects.push_back(buildContainerObject(ptr, store));
                        ++mAuthoritativeDeltaCount;
                    }
                }

                const bool batchCapacityRemaining = posBatch.baseActors.size() < maxBatch
                    && animBatch.baseActors.size() < maxBatch
                    && statsBatch.baseActors.size() < maxBatch;

                const float dx = pos.x() - snap.mPos.x();
                const float dy = pos.y() - snap.mPos.y();
                const float horizLenSq = dx * dx + dy * dy;
                const bool isMovingNow = horizLenSq > 0.25f;
                const bool stoppedMoving = snap.mWasMoving && !isMovingNow;

                const bool posDirty = batchCapacityRemaining
                    && ((pos - snap.mPos).length2() >= posThreshSq
                        || std::abs(rot.z() - snap.mRot.z()) >= rotThresh
                        || std::abs(rot.x() - snap.mRot.x()) >= rotThresh
                        || stoppedMoving);
                const bool animDirty = batchCapacityRemaining
                    && (moveFlags != snap.mMovementFlags || drawState != snap.mDrawState || flying != snap.mIsFlying);
                const bool statsDirty = batchCapacityRemaining
                    && (std::abs(health - snap.mHealth) >= statThresh
                        || std::abs(magicka - snap.mMagicka) >= statThresh
                        || std::abs(fatigue - snap.mFatigue) >= statThresh
                        || (dead != snap.mDead));
                const bool deathDirty = (dead && !snap.mDeathBroadcasted);

                bool equipDirty = false;
                std::uint64_t currentEquipHash = snap.mEquipmentHash;
                if (ptr.getClass().hasInventoryStore(ptr) && equipBatch.baseActors.size() < maxBatch)
                {
                    currentEquipHash = computeEquipmentHash(ptr);
                    equipDirty = (!snap.mEquipmentBroadcasted || currentEquipHash != snap.mEquipmentHash);
                }

                if (!posDirty && !animDirty && !statsDirty && !deathDirty && !equipDirty)
                    return true;

                if (equipDirty)
                {
                    mwmp::BaseActor equipActor = buildBaseActor(ptr, mWorld, esmCell, false, false, false);
                    populateActorEquipment(ptr, equipActor, snap.mEquipmentHash);
                    equipBatch.baseActors.push_back(std::move(equipActor));
                    snap.mEquipmentBroadcasted = true;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mEquip;
                }

                if (!posDirty && !animDirty && !statsDirty && !deathDirty)
                    return true;

                mwmp::BaseActor baseActor
                    = buildBaseActor(ptr, mWorld, esmCell, posDirty, statsDirty, deathDirty);

                if (posDirty)
                {
                    if (isMovingNow)
                    {
                        const float invLen = 1.0f / std::sqrt(horizLenSq);
                        const float nx = dx * invLen;
                        const float ny = dy * invLen;
                        const float sinZ = std::sin(rot.z());
                        const float cosZ = std::cos(rot.z());
                        baseActor.direction.pos[0] = nx * cosZ - ny * sinZ;
                        baseActor.direction.pos[1] = nx * sinZ + ny * cosZ;
                    }
                    else
                    {
                        baseActor.direction.pos[0] = 0.0f;
                        baseActor.direction.pos[1] = 0.0f;
                        baseActor.direction.pos[2] = 0.0f;
                    }
                    posBatch.baseActors.push_back(baseActor);
                    snap.mPos = pos;
                    snap.mRot = rot;
                    snap.mWasMoving = isMovingNow;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mPos;
                }
                if (animDirty)
                {
                    animBatch.baseActors.push_back(baseActor);
                    snap.mMovementFlags = moveFlags;
                    snap.mDrawState = drawState;
                    snap.mIsFlying = flying;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mAnim;
                }
                if (statsDirty)
                {
                    statsBatch.baseActors.push_back(baseActor);
                    snap.mHealth = health;
                    snap.mMagicka = magicka;
                    snap.mFatigue = fatigue;
                    snap.mDead = dead;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mStats;
                }
                if (deathDirty)
                {
                    deathBatch.baseActors.push_back(baseActor);
                    snap.mDead = true;
                    snap.mDeathBroadcasted = true;
                    ++mAuthoritativeDeltaCount;
                }
                return true;
            });

            if (posBatch.baseActors.empty() && animBatch.baseActors.empty() && statsBatch.baseActors.empty()
                && equipBatch.baseActors.empty() && deathBatch.baseActors.empty()
                && containerBatch.baseObjects.empty())
            {
                continue;
            }

            const RakNet::RakNetGUID serverGuid = mPeer ? mPeer->GetMyGUID() : RakNet::RakNetGUID();

            std::vector<::Player*> recipients;
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                ::Player* pl = candidates[i]->mPlayer;
                if (recipientInRange[i] && pl && !pl->npc.mName.empty())
                    recipients.push_back(pl);
            }

            auto sendToRecipients = [&](unsigned char packetId, mwmp::BaseActorList& batch) {
                batch.guid = serverGuid;
                mwmp::ActorPacket* pkt = actorPackets->GetPacket(packetId);
                for (::Player* pl : recipients)
                {
                    pkt->setActorList(&batch);
                    pkt->Send(pl->guid);
                }
            };

            auto dispatchActorBatch = [&](unsigned char packetId, mwmp::BaseActorList& batch, bool updateCellStore) {
                if (batch.baseActors.empty())
                    return;
                batch.count = static_cast<unsigned int>(batch.baseActors.size());
                if (serverCell && updateCellStore)
                    serverCell->readActorList(packetId, &batch);
                sendToRecipients(packetId, batch);
            };

            dispatchActorBatch(ID_ACTOR_POSITION, posBatch, true);
            dispatchActorBatch(ID_ACTOR_ANIM_FLAGS, animBatch, false);
            dispatchActorBatch(ID_ACTOR_STATS_DYNAMIC, statsBatch, true);
            dispatchActorBatch(ID_ACTOR_EQUIPMENT, equipBatch, true);
            if (!containerBatch.baseObjects.empty())
            {
                if (mwmp::ObjectPacket* objPkt = mNetworking->getObjectPacketController()->GetPacket(ID_CONTAINER))
                {
                    containerBatch.guid = serverGuid;
                    for (::Player* pl : recipients)
                    {
                        objPkt->setObjectList(&containerBatch);
                        objPkt->Send(pl->guid);
                    }
                }
            }
            if (!deathBatch.baseActors.empty())
            {
                deathBatch.count = static_cast<unsigned int>(deathBatch.baseActors.size());
                sendToRecipients(ID_ACTOR_DEATH, deathBatch);
                ::Player* observer = !recipients.empty() ? recipients.front() : candidates.front()->mPlayer;
                if (observer)
                {
                    Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                        observer->getId(), Utils::getCellDescription(esmCell).c_str());
                }
            }
        }

        if (mAuthoritativeDeltaCount == initialDeltaCount)
        {
            if (++mStallBroadcastCycles >= 60)
            {
                mStallBroadcastCycles = 0;
                const ConnectedObserver& obs0 = connected.front();
                const std::string obsCell = obs0.mPlayer ? Utils::getCellDescription(obs0.mPlayer->cell) : "<none>";
                Log(Debug::Warning) << "Authoritative broadcast stall (3s, players=" << connected.size()
                                    << ", anchors=" << mPlayerAnchors.size()
                                    << ", anchor0=" << obsCell << " (" << obs0.mPosition.x() << ", "
                                    << obs0.mPosition.y() << ", " << obs0.mPosition.z() << ")"
                                    << ", activeCells=" << mWorld.getWorldScene().getActiveCells().size()
                                    << ", evaluatedActors=" << evaluatedActors << ")";
            }
        }
        else
        {
            mStallBroadcastCycles = 0;
        }
    }

    void HeadlessServer::pollNetwork()
    {
        if (!mNetworking)
            return;

        mNetworking->pollOnce();
        syncPlayersToWorld();
        syncActorsToWorld();
    }

    void HeadlessServer::broadcastNetworkState(float dt)
    {
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
        collectConnectedObservers(connected);
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
        collectConnectedObservers(connected);
        return connected.size();
    }

    bool HeadlessServer::isAuthoritative() const
    {
        return mConfig.mAuthority.authoritativeMode;
    }

    std::uint64_t HeadlessServer::getAuthoritativeDeltaCount() const
    {
        return mAuthoritativeDeltaCount;
    }

    HeadlessServer::AuthoritativeDeltaStats HeadlessServer::consumeAuthoritativeDeltaStats(
        std::uint64_t& outTotalDeltas)
    {
        outTotalDeltas = mAuthoritativeDeltaCount;
        const AuthoritativeDeltaStats snapshot = mDeltaStats;
        mAuthoritativeDeltaCount = 0;
        mDeltaStats = {};
        return snapshot;
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
        MWWorld::CellStore* cellStore = resolveCellStore(objectList.cell);
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
                        mWorld.getStore(), ESM::RefId::stringRefId(obj.refId), std::max(1, obj.count));
                    MWWorld::Ptr placed = mWorld.placeObject(ref.getPtr(), cellStore, obj.position);
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
                                const ESM::RefId itemId = ESM::RefId::stringRefId(item.refId);
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

        MWWorld::CellStore* cellStore = resolveCellStore(actorList.cell);
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
                aiTarget = resolveTargetPtr(actor.aiTarget, cellStore);
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
