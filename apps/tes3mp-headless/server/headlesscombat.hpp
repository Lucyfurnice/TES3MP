#ifndef TES3MP_HEADLESS_COMBAT_HPP
#define TES3MP_HEADLESS_COMBAT_HPP

#include <apps/openmw/mwworld/ptr.hpp>

#include <osg/Vec3f>

#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace MWMechanics
{
    class CreatureStats;
}

namespace MWWorld
{
    class CellStore;
    class World;
}

namespace mwmp
{
    class Attack;
    class Networking;
    class Target;
}

namespace TES3MPHeadless
{
    class HeadlessPlayerAnchorManager;

    /// Handles authoritative melee/ranged hit validation, damage dispatch to player anchors,
    /// `ID_PLAYER_STATS_DYNAMIC` / `ID_PLAYER_DEATH` emission, and combat disengagement.
    class HeadlessCombatController
    {
    public:
        explicit HeadlessCombatController(MWWorld::World& world);

        bool handlePlayerAttack(::Player& player, mwmp::Attack& attack, HeadlessPlayerAnchorManager& anchors,
            mwmp::Networking* networking, bool validateCombat);

        bool dispatchAnchorHealthAndDeath(::Player& player, MWWorld::Ptr& anchor, MWMechanics::CreatureStats& anchorStats,
            float prevHealth, mwmp::Networking* networking);

        bool hasLastHealth(unsigned short pid) const;
        float getLastHealth(unsigned short pid) const;
        void setLastHealth(unsigned short pid, float health);
        void clearPlayerHealth(unsigned short pid);
        void clear();

        void disengageActorsTargeting(const MWWorld::Ptr& anchor, bool guardsOnly = false);
        void calmGuardsForAnchor(const MWWorld::Ptr& anchor);

        void setPvpConfig(bool pvpEnabled, std::vector<std::string> pvpFreeCells, std::vector<std::string> pvpFreeRegions);
        bool isPvpAllowed(const MWWorld::CellStore* cell, const osg::Vec3f& pos) const;

        MWWorld::Ptr resolveTargetPtr(const mwmp::Target& target, const MWWorld::CellStore* hintCell,
            const HeadlessPlayerAnchorManager& anchors) const;

    private:
        MWWorld::World& mWorld;
        std::unordered_map<unsigned short, float> mAnchorLastHealth;
        bool mPvpEnabled = true;
        std::vector<std::string> mPvpFreeCells;
        std::vector<std::string> mPvpFreeRegions;
    };
}

#endif
