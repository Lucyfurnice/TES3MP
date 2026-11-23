#ifndef TES3MP_HEADLESS_PLAYER_ANCHOR_MANAGER_HPP
#define TES3MP_HEADLESS_PLAYER_ANCHOR_MANAGER_HPP

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadcell.hpp>

#include <osg/Vec3f>

#include <chrono>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ESM
{
    struct Position;
}

namespace MWWorld
{
    class CellStore;
    class World;
}

namespace TES3MPHeadless
{
    struct PlayerValidationState
    {
        ESM::Cell mLastCell;
        osg::Vec3f mLastPos;
        std::chrono::steady_clock::time_point mLastUpdate;
        bool mInitialized = false;
    };

    /// Owns server-side player anchor NPCs (`mPlayerAnchors[pid]`), their `PhysicsSystem` lifecycle,
    /// `pid <-> Ptr` lookup, anchor resurrection, and tracked health/bounty/resist-arrest state.
    class HeadlessPlayerAnchorManager
    {
    public:
        explicit HeadlessPlayerAnchorManager(MWWorld::World& world);

        MWWorld::Ptr getAnchorPtr(unsigned short pid) const;
        std::optional<unsigned short> resolvePidFromPtr(const MWWorld::Ptr& ptr) const;
        bool isPlayerAnchorPtr(const MWWorld::Ptr& ptr) const;

        MWWorld::Ptr spawnOrMoveAnchor(unsigned short pid, MWWorld::CellStore* targetCell, const ESM::Position& pos);
        void ensurePhysicsActor(const MWWorld::Ptr& anchor);
        void resurrectAnchor(MWWorld::Ptr& anchor, std::string_view playerName);
        void removeAnchor(unsigned short pid);

        const std::unordered_map<unsigned short, MWWorld::Ptr>& getAnchors() const { return mPlayerAnchors; }

        void clear();

        static MWWorld::CellStore* resolveCellStore(MWWorld::World& world, const ESM::Cell& cell);

    private:
        MWWorld::World& mWorld;
        std::unordered_map<unsigned short, MWWorld::Ptr> mPlayerAnchors;
    };
}

#endif
