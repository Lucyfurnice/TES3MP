#ifndef TES3MP_HEADLESS_REPLICATION_HPP
#define TES3MP_HEADLESS_REPLICATION_HPP

#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/openmw-mp/Base/ServerAuthority.hpp>

#include <osg/Vec3f>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

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
    class HeadlessPlayerAnchorManager;

    struct AuthoritativeDeltaStats
    {
        std::uint64_t mPos = 0;
        std::uint64_t mAnim = 0;
        std::uint64_t mStats = 0;
        std::uint64_t mEquip = 0;
    };

    struct ConnectedObserver
    {
        ::Player* mPlayer = nullptr;
        osg::Vec3f mPosition;
        MWWorld::CellStore* mCell = nullptr;
        bool mIsExterior = true;
    };

    /// Collects connected player observers and broadcasts dirty-tracked authoritative actor & container deltas.
    class HeadlessReplicationSystem
    {
    public:
        explicit HeadlessReplicationSystem(MWWorld::World& world);

        void collectConnectedObservers(const std::unordered_set<unsigned short>& playersWithKnownCell,
            const HeadlessPlayerAnchorManager& anchors, std::vector<ConnectedObserver>& outObservers) const;

        void broadcastAuthoritativeActorDeltas(const std::unordered_set<unsigned short>& playersWithKnownCell,
            const mwmp::ServerAuthorityConfig& authority, RakNet::RakPeerInterface* peer, mwmp::Networking* networking,
            const HeadlessPlayerAnchorManager& anchors);

        /*
            Start of tes3mp addition

            Report a melee/ranged hit that the authoritative server resolved natively (NPC/creature -> player
            anchor) so the owning client can render the hit feedback (and mirror the attack completion). The
            actual health change is delivered separately through ID_PLAYER_STATS_DYNAMIC.
        */
        void sendAttackLandedToPlayer(::Player& victimPlayer, const MWWorld::Ptr& attacker, bool ranged, float damage,
            RakNet::RakPeerInterface* peer, mwmp::Networking* networking);
        /*
            End of tes3mp addition
        */

        std::uint64_t getAuthoritativeDeltaCount() const { return mAuthoritativeDeltaCount; }
        AuthoritativeDeltaStats getAuthoritativeDeltaStats() const { return mDeltaStats; }
        AuthoritativeDeltaStats consumeAuthoritativeDeltaStats(std::uint64_t& outTotalDeltas);

        void clear();

    private:
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
            std::uint64_t mEquipmentHash = 0;
            bool mEquipmentBroadcasted = false;
            bool mWasMoving = false;
            bool mAttacking = false;
            bool mInitialized = false;
        };

        MWWorld::World& mWorld;
        std::unordered_map<const MWWorld::LiveCellRefBase*, ActorSnapshot> mActorSnapshots;
        std::unordered_map<int, const MWWorld::LiveCellRefBase*> mActorSnapshotsByMpNum;
        std::unordered_map<const MWWorld::LiveCellRefBase*, std::uint64_t> mContainerSnapshots;
        std::uint64_t mAuthoritativeDeltaCount = 0;
        AuthoritativeDeltaStats mDeltaStats;
        std::size_t mStallBroadcastCycles = 0;
        std::size_t mContainerSweepCounter = 0;
    };
}

#endif
