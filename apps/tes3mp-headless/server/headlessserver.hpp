#ifndef TES3MP_HEADLESS_SERVER_HPP
#define TES3MP_HEADLESS_SERVER_HPP

#include "cellinstancemanager.hpp"
#include "headlesscombat.hpp"
#include "headlessdialogue.hpp"
#include "headlessreplication.hpp"
#include "playeranchormanager.hpp"

#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Base/BaseWorldstate.hpp>
#include <components/openmw-mp/Base/ServerAuthority.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <typeinfo>
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
        bool mSmokeArrestCombat = false;
    };

    /// Bridges tes3mp-server (RakNet, PacketController, Player/Cell registries, and LuaJIT ScriptController)
    /// into HeadlessEngine's fixed-timestep loop without blocking calls or touching MWLua's lua_State.
    class HeadlessServer
    {
    public:
        using AuthoritativeDeltaStats = TES3MPHeadless::AuthoritativeDeltaStats;

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

        AuthoritativeDeltaStats getAuthoritativeDeltaStats() const
        {
            return mReplication.getAuthoritativeDeltaStats();
        }
        /// Returns cumulative delta counts since the previous status sample and resets the interval counters.
        AuthoritativeDeltaStats consumeAuthoritativeDeltaStats(std::uint64_t& outTotalDeltas)
        {
            return mReplication.consumeAuthoritativeDeltaStats(outTotalDeltas);
        }

    private:
        struct QueuedActorSync
        {
            unsigned char mPacketId = 0;
            mwmp::BaseActorList mActorList;
        };

        std::filesystem::path resolveConfigPath(const std::filesystem::path& candidate) const;
        std::filesystem::path resolvePluginHome(const std::string& rawHome, const std::filesystem::path& cfgDir) const;

        void syncPlayersToWorld();
        void syncPlayerAnchorState(const ::Player& player, MWWorld::Ptr& anchor);
        void syncActorsToWorld();
        void broadcastAuthoritativeActorDeltas();

        bool validatePlayerPosition(::Player& player);
        void handlePlayerCellChange(::Player& player);
        bool handlePlayerCast(::Player& player, mwmp::Cast& cast);
        bool handleObjectAction(unsigned char packetId, ::Player& player, mwmp::BaseObjectList& objectList);

        void applyDynamicRecords(const mwmp::BaseWorldstate& worldstate);
        void handleLuaRecordCreated(const std::type_info& type, const void* recordPtr);
        void handleServerObjectEvent(unsigned char packetId, const mwmp::BaseObjectList& objectList);
        void handleServerActorAI(const mwmp::BaseActorList& actorList);
        void stepSmokeArrestCombat();

        Files::ConfigurationManager& mCfgMgr;
        MWWorld::World& mWorld;
        HeadlessServerConfig mConfig;

        RakNet::RakPeerInterface* mPeer = nullptr;
        std::unique_ptr<mwmp::Networking> mNetworking;
        HeadlessPlayerAnchorManager mAnchors;
        HeadlessCombatController mCombat;
        HeadlessDialogueController mDialogue;
        HeadlessReplicationSystem mReplication;
        std::unordered_map<unsigned short, PlayerValidationState> mPlayerValidation;
        std::unordered_set<unsigned short> mPlayersWithKnownCell;
        std::vector<QueuedActorSync> mPendingActorSyncs;
        CellInstanceManager mCellInstances;
        uint16_t mBoundPort = 0;
        double mBroadcastAccumulator = 0.0;
        bool mLogInitialized = false;
        bool mScriptsLoaded = false;
        bool mShutDown = false;
        bool mSmokeInitialized = false;
        bool mSmokeArrestTriggered = false;
        bool mSmokeWeaponDrawn = false;
        bool mSmokeLastAttacking = false;
        bool mSmokeDamageObserved = false;
        int mSmokeBounty = 40;
        MWWorld::Ptr mSmokeGuard;
        MWWorld::Ptr mSmokeAnchor;
    };
}

#endif
