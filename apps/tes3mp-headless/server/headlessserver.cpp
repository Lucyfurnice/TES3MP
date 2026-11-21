#include "headlessserver.hpp"

#include <RakPeer.h>
#include <RakPeerInterface.h>

#include <MasterClient.hpp>
#include <Networking.hpp>
#include <Player.hpp>
#include <Script/Functions/Cells.hpp>
#include <Script/LangLua/LangLua.hpp>
#include <Script/Script.hpp>
#include <Utils.hpp>

#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

#include <components/debug/debuglog.hpp>
#include <components/esm/exteriorcelllocation.hpp>
#include <components/files/configurationmanager.hpp>
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
        {
            defaultCfg = resolveConfigPath("tes3mp-server-default.cfg");
        }

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
        mNetworking->setActorSyncCallback([this](unsigned char packetId, const mwmp::BaseActorList& list) {
            mPendingActorSyncs.push_back(QueuedActorSync{ packetId, list });
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
                         << " (maxPlayers=" << maxPlayers << ", plugins=" << (mScriptsLoaded ? pluginsCsv : "<none>")
                         << ")";
    }

    void HeadlessServer::shutdown()
    {
        if (mShutDown)
            return;
        mShutDown = true;

        for (auto& [pid, ptr] : mPlayerAnchors)
        {
            if (!ptr.isEmpty())
                mWorld.deleteObject(ptr);
        }
        mPlayerAnchors.clear();
        mPendingActorSyncs.clear();

        CellFunctions::SetCellPinnedHandler(nullptr);

        if (mNetworking)
        {
            mNetworking->setActorSyncCallback(nullptr);
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
        if (mPendingActorSyncs.empty())
            return;

        for (const QueuedActorSync& sync : mPendingActorSyncs)
        {
            for (const mwmp::BaseActor& actor : sync.mActorList.baseActors)
            {
                const ESM::Cell& targetEsmCell = (sync.mPacketId == ID_ACTOR_CELL_CHANGE) ? actor.cell : sync.mActorList.cell;
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

    void HeadlessServer::pollNetwork()
    {
        if (!mNetworking)
            return;

        mNetworking->pollOnce();
        syncPlayersToWorld();
        syncActorsToWorld();
    }

    void HeadlessServer::broadcastNetworkState()
    {
        if (!mNetworking)
            return;

        mNetworking->tickTimers();
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
}
