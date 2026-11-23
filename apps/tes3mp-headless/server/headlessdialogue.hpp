#ifndef TES3MP_HEADLESS_DIALOGUE_HPP
#define TES3MP_HEADLESS_DIALOGUE_HPP

#include <apps/openmw/mwworld/ptr.hpp>

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MWWorld
{
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
    class HeadlessCombatController;
    class HeadlessPlayerAnchorManager;

    /// Handles server-side dialogue choice execution, `StartCombat` remapping onto player anchors,
    /// `PayFine`/`GoToJail` gold & bounty sync, and arrest cooldown / resist-arrest escalation.
    class HeadlessDialogueController
    {
    public:
        explicit HeadlessDialogueController(MWWorld::World& world);

        bool executeDialogueChoice(const MWWorld::Ptr& targetPtr, const MWWorld::Ptr& playerAnchor, int& playerBounty,
            unsigned char choiceType, const std::string& topicId, const std::string& actorRefId, unsigned short pid,
            HeadlessPlayerAnchorManager& anchors, HeadlessCombatController& combat);

        void processPendingArrests(RakNet::RakPeerInterface* peer, mwmp::Networking* networking,
            HeadlessPlayerAnchorManager& anchors);

        int getPlayerGold(unsigned short pid) const;
        void setPlayerGold(unsigned short pid, int gold);
        bool hasPlayerGold(unsigned short pid) const;
        void clearPlayerGold(unsigned short pid);

        bool hasLastBounty(unsigned short pid) const;
        int getLastBounty(unsigned short pid) const;
        void setLastBounty(unsigned short pid, int bounty);

        bool hasResistedArrest(unsigned short pid) const;
        void setResistedArrest(unsigned short pid, bool resisted);

        void clearCooldown(unsigned short pid);
        void clearPlayerState(unsigned short pid);
        void clear();

    private:
        MWWorld::World& mWorld;
        std::unordered_map<unsigned short, std::chrono::steady_clock::time_point> mPlayerArrestCooldown;
        std::unordered_map<unsigned short, int> mPlayerGold;
        std::unordered_map<unsigned short, int> mAnchorLastBounty;
        std::unordered_set<unsigned short> mResistedArrest;
    };
}

#endif
