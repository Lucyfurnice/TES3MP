#ifndef TES3MP_HEADLESS_SERVER_HPP
#define TES3MP_HEADLESS_SERVER_HPP

#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/Base/ServerAuthority.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

namespace Files
{
    struct ConfigurationManager;
}

namespace MWWorld
{
    class CellStore;
    struct LiveCellRefBase;
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
        mwmp::ServerAuthorityConfig mAuthority;
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

        /// Resolve the server plugin home directory ([Plugins] home, default "./server") from the same
        /// tes3mp-server config files init() reads. Usable before the World/HeadlessServer exist.
        static std::filesystem::path resolveServerHome(
            Files::ConfigurationManager& cfgMgr, const std::filesystem::path& serverCfgPath);

        /// Initialize RakNet UDP socket, load tes3mp-server Lua/MoonScript plugins, and fire OnServerInit/OnServerPostInit.
        void init();

        /// Shutdown timers, scripts, MasterClient, RakNet peer, and TimedLog cleanly.
        void shutdown();

        /// Drain all pending RakNet packets without blocking and sync player/actor transforms into MWWorld.
        void pollNetwork();

        /// Advance tes3mp-server TimerAPI and broadcast authoritative MWMechanics/MWWorld actor deltas.
        void broadcastNetworkState(float dt);

        /// Return false when a server script or fatal error requests shutdown.
        bool isRunning() const;

        /// Append the ESM::Cell of every active POSTLOADED player (used by Phase 7 multi-cell residency).
        void collectPlayerCells(std::vector<ESM::Cell>& outCells) const;

        /// Populate active observer positions and exterior AI/physics simulation radius for distance culling.
        void collectSimulationObservers(
            std::vector<MWBase::MechanicsManager::SimulationObserver>& outObservers) const;
        float getActorSimulationRadius() const;

        uint16_t getBoundPort() const;
        std::size_t getConnectedPlayerCount() const;
        bool isAuthoritative() const;
        std::uint64_t getAuthoritativeDeltaCount() const;

    private:
        struct QueuedActorSync
        {
            unsigned char mPacketId = 0;
            mwmp::BaseActorList mActorList;
        };

        struct ActorSnapshot
        {
            osg::Vec3f mPos;
            osg::Vec3f mRot;
            unsigned int mMovementFlags = 0;
            char mDrawState = 0;
            bool mIsFlying = false;
            float mHealth = 0.f;
            float mMagicka = 0.f;
            float mFatigue = 0.f;
            bool mDead = false;
            bool mDeathBroadcasted = false;
            bool mInitialized = false;
        };

        struct PlayerValidationState
        {
            ESM::Cell mLastCell;
            osg::Vec3f mLastPos;
            std::chrono::steady_clock::time_point mLastUpdate;
            bool mInitialized = false;
        };

        struct ConnectedObserver
        {
            ::Player* mPlayer = nullptr;
            osg::Vec3f mPosition;
            MWWorld::CellStore* mCell = nullptr;
            bool mIsExterior = true;
        };

        std::filesystem::path resolveConfigPath(const std::filesystem::path& candidate) const;
        std::filesystem::path resolvePluginHome(const std::string& rawHome, const std::filesystem::path& cfgDir) const;
        MWWorld::CellStore* resolveCellStore(const ESM::Cell& cell) const;
        MWWorld::Ptr resolveTargetPtr(const mwmp::Target& target, const MWWorld::CellStore* hintCell) const;
        bool isPlayerAnchorPtr(const MWWorld::Ptr& ptr) const;
        /// Live connected players (POSTLOADED and with a cell confirmed by ID_PLAYER_CELL_CHANGE), using the
        /// latest ID_PLAYER_POSITION coordinates rather than the (possibly lagging) world anchor positions.
        void collectConnectedObservers(std::vector<ConnectedObserver>& outObservers) const;

        void syncPlayersToWorld();
        void syncActorsToWorld();
        void broadcastAuthoritativeActorDeltas();

        bool validatePlayerPosition(::Player& player);
        void handlePlayerCellChange(::Player& player);
        bool handlePlayerAttack(::Player& player, mwmp::Attack& attack);
        bool handlePlayerCast(::Player& player, mwmp::Cast& cast);
        bool handleObjectAction(unsigned char packetId, ::Player& player, mwmp::BaseObjectList& objectList);

        Files::ConfigurationManager& mCfgMgr;
        MWWorld::World& mWorld;
        HeadlessServerConfig mConfig;

        RakNet::RakPeerInterface* mPeer = nullptr;
        std::unique_ptr<mwmp::Networking> mNetworking;
        std::unordered_map<unsigned short, MWWorld::Ptr> mPlayerAnchors;
        std::unordered_map<unsigned short, PlayerValidationState> mPlayerValidation;
        std::unordered_set<unsigned short> mPlayersWithKnownCell;
        std::unordered_map<const MWWorld::LiveCellRefBase*, ActorSnapshot> mActorSnapshots;
        std::vector<QueuedActorSync> mPendingActorSyncs;
        uint16_t mBoundPort = 0;
        double mBroadcastAccumulator = 0.0;
        std::uint64_t mAuthoritativeDeltaCount = 0;
        bool mLogInitialized = false;
        bool mScriptsLoaded = false;
        bool mShutDown = false;
    };
}

#endif
