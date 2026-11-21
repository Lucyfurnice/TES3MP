#ifndef TES3MP_HEADLESS_SERVER_HPP
#define TES3MP_HEADLESS_SERVER_HPP

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Files
{
    struct ConfigurationManager;
}

namespace MWWorld
{
    class CellStore;
    class World;
}

namespace mwmp
{
    class Networking;
}

namespace RakNet
{
    class RakPeerInterface;
}

namespace TES3MPHeadless
{
    /// Configuration for the embedded tes3mp-server networking and script runtime.
    struct HeadlessServerConfig
    {
        std::filesystem::path mServerCfgPath;
        uint16_t mPortOverride = 0; // 0 = use value from server config
        bool mHasPortOverride = false;
    };

    /// Bridges tes3mp-server (RakNet, PacketController, Player/Cell registries, and LuaJIT ScriptController)
    /// into HeadlessEngine's fixed-timestep loop without blocking calls or touching MWLua's lua_State.
    class HeadlessServer
    {
    public:
        explicit HeadlessServer(Files::ConfigurationManager& cfgMgr, MWWorld::World& world,
            HeadlessServerConfig config);
        ~HeadlessServer();

        HeadlessServer(const HeadlessServer&) = delete;
        HeadlessServer& operator=(const HeadlessServer&) = delete;

        /// Initialize RakNet UDP socket, load tes3mp-server Lua/MoonScript plugins, and fire OnServerInit/OnServerPostInit.
        void init();

        /// Shutdown timers, scripts, MasterClient, RakNet peer, and TimedLog cleanly.
        void shutdown();

        /// Drain all pending RakNet packets without blocking and sync player/actor transforms into MWWorld.
        void pollNetwork();

        /// Advance tes3mp-server TimerAPI and flush periodic outbound state.
        void broadcastNetworkState();

        /// Return false when a server script or fatal error requests shutdown.
        bool isRunning() const;

        /// Append the ESM::Cell of every active POSTLOADED player (used by Phase 7 multi-cell residency).
        void collectPlayerCells(std::vector<ESM::Cell>& outCells) const;

        uint16_t getBoundPort() const;
        std::size_t getConnectedPlayerCount() const;

    private:
        struct QueuedActorSync
        {
            unsigned char mPacketId = 0;
            mwmp::BaseActorList mActorList;
        };

        std::filesystem::path resolveConfigPath(const std::filesystem::path& candidate) const;
        std::filesystem::path resolvePluginHome(const std::string& rawHome, const std::filesystem::path& cfgDir) const;
        MWWorld::CellStore* resolveCellStore(const ESM::Cell& cell) const;
        void syncPlayersToWorld();
        void syncActorsToWorld();

        Files::ConfigurationManager& mCfgMgr;
        MWWorld::World& mWorld;
        HeadlessServerConfig mConfig;

        RakNet::RakPeerInterface* mPeer = nullptr;
        std::unique_ptr<mwmp::Networking> mNetworking;
        std::unordered_map<unsigned short, MWWorld::Ptr> mPlayerAnchors;
        std::vector<QueuedActorSync> mPendingActorSyncs;
        uint16_t mBoundPort = 0;
        bool mLogInitialized = false;
        bool mScriptsLoaded = false;
        bool mShutDown = false;
    };
}

#endif
