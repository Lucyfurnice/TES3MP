#include "headlessserver.hpp"

#include <RakPeer.h>
#include <RakPeerInterface.h>

#include <Cell.hpp>
#include <CellController.hpp>
#include <MasterClient.hpp>
#include <Networking.hpp>
#include <Player.hpp>
#include <Script/Functions/Cells.hpp>
#include <Script/LangLua/LangLua.hpp>
#include <Script/Script.hpp>
#include <Utils.hpp>

#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/spellcasting.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
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
#include <components/esm3/loadspel.hpp>
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
            actor.direction = pos;
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

    std::filesystem::path HeadlessServer::resolveConfigPath(const std::filesystem::path& candidate) const
    {
        if (candidate.empty())
            return {};
        if (std::filesystem::exists(candidate))
            return candidate;
        if (candidate.is_relative())
        {
            const std::filesystem::path inLocal = mCfgMgr.getLocalPath() / candidate;
            if (std::filesystem::exists(inLocal))
                return inLocal;
            const std::filesystem::path inGlobal = mCfgMgr.getGlobalPath() / candidate;
            if (std::filesystem::exists(inGlobal))
                return inGlobal;
        }
        return {};
    }

    std::filesystem::path HeadlessServer::resolvePluginHome(
        const std::string& rawHome, const std::filesystem::path& cfgDir) const
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
        const std::filesystem::path nearLocal = mCfgMgr.getLocalPath() / rawPath;
        if (std::filesystem::exists(nearLocal))
            return nearLocal;
        return rawPath;
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

        for (auto& [pid, ptr] : mPlayerAnchors)
        {
            if (!ptr.isEmpty())
                mWorld.deleteObject(ptr);
        }
        mPlayerAnchors.clear();
        mPlayerValidation.clear();
        mActorSnapshots.clear();
        mPendingActorSyncs.clear();
        mBroadcastAccumulator = 0.0;

        if (mNetworking)
        {
            mNetworking->setActorSyncCallback(nullptr);
            mNetworking->setPlayerPositionCallback(nullptr);
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

    bool HeadlessServer::validatePlayerPosition(::Player& player)
    {
        if (!mConfig.mAuthority.authoritativeMode || !mConfig.mAuthority.validatePlayerMovement)
            return true;

        const unsigned short pid = player.getId();
        auto& state = mPlayerValidation[pid];
        const auto now = std::chrono::steady_clock::now();
        const osg::Vec3f newPos = player.position.asVec3();

        const bool sameCell = state.mInitialized && state.mLastCell.isExterior() == player.cell.isExterior()
            && (player.cell.isExterior()
                    ? (state.mLastCell.mData.mX == player.cell.mData.mX && state.mLastCell.mData.mY == player.cell.mData.mY)
                    : (state.mLastCell.mName == player.cell.mName));

        if (!state.mInitialized || player.exchangeFullInfo || !sameCell)
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
                    if (!playerAnchor.isEmpty())
                    {
                        if (auto action = targetPtr.getClass().activate(targetPtr, playerAnchor))
                            action->execute(playerAnchor);
                    }
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
                            const ESM::RefId itemId = ESM::RefId::stringRefId(item.refId);
                            if (objectList.action == mwmp::BaseObjectList::REMOVE)
                                store.remove(itemId, std::max(1, item.count));
                            else if (item.count > 0)
                                store.add(itemId, item.count, true);
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

            MWWorld::CellStore* targetCell = resolveCellStore(player->cell);
            if (!targetCell)
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

        mwmp::ActorPacketController* actorPackets = mNetworking->getActorPacketController();
        if (!actorPackets)
            return;

        const float posThreshSq = mConfig.mAuthority.actorPosDeltaSq;
        const float rotThresh = mConfig.mAuthority.actorRotDeltaRad;
        const float statThresh = mConfig.mAuthority.actorStatDelta;

        for (MWWorld::CellStore* cellStore : mWorld.getWorldScene().getActiveCells())
        {
            if (!cellStore)
                continue;

            ESM::Cell esmCell = cellFromStore(*cellStore);
            mwmp::BaseActorList posBatch;
            mwmp::BaseActorList animBatch;
            mwmp::BaseActorList statsBatch;
            mwmp::BaseActorList deathBatch;
            posBatch.cell = animBatch.cell = statsBatch.cell = deathBatch.cell = esmCell;

            cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getClass().isActor() || !ptr.getRefData().isEnabled() || isPlayerAnchorPtr(ptr))
                    return true;

                const MWWorld::LiveCellRefBase* key = ptr.getBase();
                const ESM::Position& esmPos = ptr.getRefData().getPosition();
                const osg::Vec3f pos = esmPos.asVec3();
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
                    snap = ActorSnapshot{
                        pos, rot, moveFlags, drawState, flying, health, magicka, fatigue, dead, dead, true
                    };
                    return true;
                }

                // Reset one-shot death flag only if the actor was explicitly resurrected.
                if (!dead && snap.mDeathBroadcasted)
                    snap.mDeathBroadcasted = false;

                const bool posDirty = (pos - snap.mPos).length2() >= posThreshSq
                    || std::abs(rot.z() - snap.mRot.z()) >= rotThresh
                    || std::abs(rot.x() - snap.mRot.x()) >= rotThresh;
                const bool animDirty
                    = (moveFlags != snap.mMovementFlags || drawState != snap.mDrawState || flying != snap.mIsFlying);
                const bool statsDirty = std::abs(health - snap.mHealth) >= statThresh
                    || std::abs(magicka - snap.mMagicka) >= statThresh
                    || std::abs(fatigue - snap.mFatigue) >= statThresh
                    || (dead != snap.mDead);
                const bool deathDirty = (dead && !snap.mDeathBroadcasted);

                if (!posDirty && !animDirty && !statsDirty && !deathDirty)
                    return true;

                const mwmp::BaseActor baseActor
                    = buildBaseActor(ptr, mWorld, esmCell, posDirty, statsDirty, deathDirty);

                if (posDirty)
                {
                    posBatch.baseActors.push_back(baseActor);
                    snap.mPos = pos;
                    snap.mRot = rot;
                    ++mAuthoritativeDeltaCount;
                }
                if (animDirty)
                {
                    animBatch.baseActors.push_back(baseActor);
                    snap.mMovementFlags = moveFlags;
                    snap.mDrawState = drawState;
                    snap.mIsFlying = flying;
                    ++mAuthoritativeDeltaCount;
                }
                if (statsDirty)
                {
                    statsBatch.baseActors.push_back(baseActor);
                    snap.mHealth = health;
                    snap.mMagicka = magicka;
                    snap.mFatigue = fatigue;
                    snap.mDead = dead;
                    ++mAuthoritativeDeltaCount;
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
                && deathBatch.baseActors.empty())
            {
                continue;
            }

            if (mPlayerAnchors.empty())
                continue;

            Cell* serverCell = CellController::get()->getCell(&esmCell);

            const RakNet::RakNetGUID serverGuid = mPeer ? mPeer->GetMyGUID() : RakNet::RakNetGUID();

            auto dispatchActorBatch = [&](unsigned char packetId, mwmp::BaseActorList& batch, bool updateCellStore) {
                if (batch.baseActors.empty())
                    return;
                batch.count = static_cast<unsigned int>(batch.baseActors.size());
                if (serverCell)
                {
                    if (updateCellStore)
                        serverCell->readActorList(packetId, &batch);
                    if (!serverCell->getPlayers().empty())
                    {
                        batch.guid = serverGuid;
                        mwmp::ActorPacket* pkt = actorPackets->GetPacket(packetId);
                        pkt->setActorList(&batch);
                        serverCell->sendToLoaded(pkt, &batch);
                    }
                }
            };

            dispatchActorBatch(ID_ACTOR_POSITION, posBatch, true);
            dispatchActorBatch(ID_ACTOR_ANIM_FLAGS, animBatch, false);
            dispatchActorBatch(ID_ACTOR_STATS_DYNAMIC, statsBatch, true);
            if (!deathBatch.baseActors.empty())
            {
                deathBatch.count = static_cast<unsigned int>(deathBatch.baseActors.size());
                if (serverCell && !serverCell->getPlayers().empty())
                {
                    Player* observer = *serverCell->begin();
                    deathBatch.guid = serverGuid;
                    mwmp::ActorPacket* pkt = actorPackets->GetPacket(ID_ACTOR_DEATH);
                    pkt->setActorList(&deathBatch);
                    serverCell->sendToLoaded(pkt, &deathBatch);
                    Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                        observer->getId(), serverCell->getShortDescription().c_str());
                }
            }
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
            if (player && player->getLoadState() == Player::POSTLOADED)
                outCells.push_back(player->cell);
        }
    }

    uint16_t HeadlessServer::getBoundPort() const
    {
        return mBoundPort;
    }

    std::size_t HeadlessServer::getConnectedPlayerCount() const
    {
        return mPlayerAnchors.size();
    }

    bool HeadlessServer::isAuthoritative() const
    {
        return mConfig.mAuthority.authoritativeMode;
    }

    std::uint64_t HeadlessServer::getAuthoritativeDeltaCount() const
    {
        return mAuthoritativeDeltaCount;
    }
}
