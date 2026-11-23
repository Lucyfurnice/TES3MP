#include "headlessdialogue.hpp"

#include "headlesscombat.hpp"
#include "playeranchormanager.hpp"
#include "stubs/headlesswindowmanager.hpp"

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw/mwbase/dialoguemanager.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/aisequence.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <RakPeerInterface.h>

namespace TES3MPHeadless
{
    HeadlessDialogueController::HeadlessDialogueController(MWWorld::World& world)
        : mWorld(world)
    {
    }

    int HeadlessDialogueController::getPlayerGold(unsigned short pid) const
    {
        auto it = mPlayerGold.find(pid);
        return it != mPlayerGold.end() ? it->second : 0;
    }

    void HeadlessDialogueController::setPlayerGold(unsigned short pid, int gold)
    {
        mPlayerGold[pid] = gold;
    }

    bool HeadlessDialogueController::hasPlayerGold(unsigned short pid) const
    {
        return mPlayerGold.contains(pid);
    }

    void HeadlessDialogueController::clearPlayerGold(unsigned short pid)
    {
        mPlayerGold.erase(pid);
    }

    bool HeadlessDialogueController::hasLastBounty(unsigned short pid) const
    {
        return mAnchorLastBounty.contains(pid);
    }

    int HeadlessDialogueController::getLastBounty(unsigned short pid) const
    {
        auto it = mAnchorLastBounty.find(pid);
        return it != mAnchorLastBounty.end() ? it->second : 0;
    }

    void HeadlessDialogueController::setLastBounty(unsigned short pid, int bounty)
    {
        mAnchorLastBounty[pid] = bounty;
    }

    bool HeadlessDialogueController::hasResistedArrest(unsigned short pid) const
    {
        return mResistedArrest.contains(pid);
    }

    void HeadlessDialogueController::setResistedArrest(unsigned short pid, bool resisted)
    {
        if (resisted)
            mResistedArrest.insert(pid);
        else
            mResistedArrest.erase(pid);
    }

    void HeadlessDialogueController::clearCooldown(unsigned short pid)
    {
        mPlayerArrestCooldown.erase(pid);
    }

    void HeadlessDialogueController::clearPlayerState(unsigned short pid)
    {
        mPlayerArrestCooldown.erase(pid);
        mPlayerGold.erase(pid);
        mAnchorLastBounty.erase(pid);
        mResistedArrest.erase(pid);
    }

    void HeadlessDialogueController::clear()
    {
        mPlayerArrestCooldown.clear();
        mPlayerGold.clear();
        mAnchorLastBounty.clear();
        mResistedArrest.clear();
    }

    bool HeadlessDialogueController::executeDialogueChoice(const MWWorld::Ptr& targetPtr,
        const MWWorld::Ptr& playerAnchor, int& playerBounty, unsigned char choiceType, const std::string& topicId,
        const std::string& actorRefId, unsigned short pid, HeadlessPlayerAnchorManager& anchors,
        HeadlessCombatController& combat)
    {
        if (playerAnchor.isEmpty() || targetPtr.isEmpty() || !targetPtr.getClass().isActor())
            return false;

        struct HeadlessDialogueCallback final : public MWBase::DialogueManager::ResponseCallback
        {
            void addResponse(std::string_view, std::string_view) override {}
        } callback;

        MWWorld::Ptr worldPlayer = mWorld.getPlayerPtr();
        static const ESM::RefId sGoldId = ESM::RefId::stringRefId("gold_001");
        if (!worldPlayer.isEmpty())
        {
            if (playerAnchor.isInCell())
            {
                mWorld.moveObject(
                    worldPlayer, playerAnchor.getCell(), playerAnchor.getRefData().getPosition().asVec3(), false);
            }
            if (worldPlayer.getClass().isNpc())
                worldPlayer.getClass().getNpcStats(worldPlayer).setBounty(playerBounty);
            if (worldPlayer.getClass().hasInventoryStore(worldPlayer))
            {
                MWWorld::ContainerStore& store = worldPlayer.getClass().getContainerStore(worldPlayer);
                const int existingGold = store.count(sGoldId);
                const int targetGold = getPlayerGold(pid);
                if (existingGold > targetGold)
                    store.remove(sGoldId, existingGold - targetGold);
                else if (targetGold > existingGold)
                    store.add(sGoldId, targetGold - existingGold);
            }
        }

        auto dmg = MWBase::Environment::get().getDialogueManager();
        if (dmg->startDialogue(targetPtr, &callback))
        {
            const auto& choices = dmg->getChoices();
            Log(Debug::Info) << "[HeadlessServer] startDialogue choices=" << choices.size()
                             << " worldPlayerBounty="
                             << (!worldPlayer.isEmpty() && worldPlayer.getClass().isNpc()
                                     ? worldPlayer.getClass().getNpcStats(worldPlayer).getBounty()
                                     : -1);
            for (const auto& [ctext, cid] : choices)
                Log(Debug::Info) << "[HeadlessServer]   choice id=" << cid << " text='" << ctext << "'";
            if (choiceType == mwmp::DialogueChoiceType::TOPIC)
            {
                if (topicId.rfind("__CHOICE__:", 0) == 0)
                {
                    int choiceId = 0;
                    try
                    {
                        choiceId = std::stoi(topicId.substr(11));
                    }
                    catch (...)
                    {
                        choiceId = 0;
                    }
                    dmg->questionAnswered(choiceId, &callback);
                    if (choiceId == 3)
                        setResistedArrest(pid, true);
                    if (auto* winMgr
                        = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
                        winMgr->setPlayerDialogueOpen(pid, false);
                    mPlayerArrestCooldown[pid] = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    Log(Debug::Info) << "[HeadlessServer] Executed ID_OBJECT_DIALOGUE_CHOICE choice=" << choiceId
                                     << " actor=" << actorRefId << " pid=" << pid;
                }
                else if (!topicId.empty())
                {
                    dmg->keywordSelected(topicId, &callback);
                    Log(Debug::Info) << "[HeadlessServer] Executed ID_OBJECT_DIALOGUE_CHOICE topic='" << topicId
                                     << "' actor=" << actorRefId << " pid=" << pid;
                }
            }
            dmg->goodbyeSelected();
        }

        if (!worldPlayer.isEmpty())
        {
            MWMechanics::AiSequence& seq = targetPtr.getClass().getCreatureStats(targetPtr).getAiSequence();
            if (seq.isInCombat(worldPlayer))
            {
                setResistedArrest(pid, true);
                seq.stopCombat();
                seq.stopPursuit();
                MWBase::Environment::get().getMechanicsManager()->startCombat(targetPtr, playerAnchor, nullptr);
                const MWMechanics::CreatureStats& aStats = playerAnchor.getClass().getCreatureStats(playerAnchor);
                const bool hasPhys
                    = mWorld.getPhysicsSystem() && mWorld.getPhysicsSystem()->getActor(playerAnchor) != nullptr;
                Log(Debug::Info) << "[HeadlessServer] Remapped dialogue StartCombat onto playerAnchor for actor="
                                 << actorRefId << " pid=" << pid << " cell='"
                                 << (playerAnchor.isInCell() ? playerAnchor.getCell()->getCell()->getDescription() : "<none>")
                                 << "' anchorDead=" << aStats.isDead()
                                 << " anchorHP=" << aStats.getHealth().getCurrent() << " hasPhys=" << hasPhys;
            }

            if (worldPlayer.getClass().isNpc() && worldPlayer.getClass().getNpcStats(worldPlayer).getBounty() <= 0)
                setResistedArrest(pid, false);

            if (worldPlayer.getClass().hasInventoryStore(worldPlayer) && hasPlayerGold(pid))
                setPlayerGold(pid, worldPlayer.getClass().getContainerStore(worldPlayer).count(sGoldId));

            if (worldPlayer.getClass().isNpc() && playerBounty > 0
                && worldPlayer.getClass().getNpcStats(worldPlayer).getBounty() == 0)
            {
                playerBounty = 0;
                if (playerAnchor.getClass().isNpc())
                    playerAnchor.getClass().getNpcStats(playerAnchor).setBounty(0);
                combat.calmGuardsForAnchor(playerAnchor);
            }
        }
        return true;
    }

    void HeadlessDialogueController::processPendingArrests(RakNet::RakPeerInterface* peer,
        mwmp::Networking* networking, HeadlessPlayerAnchorManager& anchors)
    {
        if (!networking)
            return;
        mwmp::ActorPacketController* actorPackets = networking->getActorPacketController();
        if (!actorPackets)
            return;

        auto* winMgr = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager());
        if (!winMgr)
            return;

        for (const MWWorld::Ptr& guardPtr : winMgr->consumePendingDialogueActors())
        {
            if (guardPtr.isEmpty() || !guardPtr.isInCell())
                continue;
            ::Player* targetPlayer = nullptr;
            float bestDist2 = 512.f * 512.f;
            const osg::Vec3f guardPos = guardPtr.getRefData().getPosition().asVec3();
            for (const auto& [pid, anchor] : anchors.getAnchors())
            {
                if (anchor.isEmpty() || !anchor.isInCell() || anchor.getCell() != guardPtr.getCell())
                    continue;
                ::Player* pl = Players::getPlayers() ? Players::getPlayer(pid) : nullptr;
                if (!pl || pl->npcStats.mBounty <= 0)
                    continue;
                const float d2 = (anchor.getRefData().getPosition().asVec3() - guardPos).length2();
                if (d2 < bestDist2)
                {
                    bestDist2 = d2;
                    targetPlayer = pl;
                }
            }
            if (targetPlayer && hasResistedArrest(targetPlayer->getId()))
            {
                MWWorld::Ptr targetAnchor = anchors.getAnchorPtr(targetPlayer->getId());
                if (!targetAnchor.isEmpty() && !targetAnchor.getClass().getCreatureStats(targetAnchor).isDead())
                {
                    MWMechanics::AiSequence& gSeq = guardPtr.getClass().getCreatureStats(guardPtr).getAiSequence();
                    gSeq.stopPursuit();
                    MWBase::Environment::get().getMechanicsManager()->startCombat(guardPtr, targetAnchor, nullptr);
                    Log(Debug::Info) << "[HeadlessServer] Skipped repeat arrest dialogue; escalated guard="
                                     << guardPtr.getCellRef().getRefId().serializeText()
                                     << " to AiCombat for pid=" << targetPlayer->getId();
                }
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto cdIt = targetPlayer ? mPlayerArrestCooldown.find(targetPlayer->getId())
                                           : mPlayerArrestCooldown.end();
            const bool onCooldown = (cdIt != mPlayerArrestCooldown.end() && now < cdIt->second);
            if (targetPlayer && !winMgr->isPlayerDialogueOpen(targetPlayer->getId()) && !onCooldown)
            {
                if (mwmp::ActorPacket* aiPkt = actorPackets->GetPacket(ID_ACTOR_AI))
                {
                    mwmp::BaseActorList arrestList;
                    arrestList.guid = peer ? peer->GetMyGUID() : RakNet::RakNetGUID();
                    arrestList.cell = guardPtr.getCell()->getCell()->getEsm3();
                    arrestList.action = mwmp::BaseActorList::SET;
                    mwmp::BaseActor baseActor;
                    baseActor.refId = guardPtr.getCellRef().getRefId().serializeText();
                    baseActor.refNum = guardPtr.getCellRef().getRefNum().mIndex;
                    baseActor.mpNum = guardPtr.getCellRef().getMpNum();
                    baseActor.aiAction = mwmp::BaseActorList::ARREST;
                    baseActor.hasAiTarget = false;
                    arrestList.baseActors.push_back(baseActor);
                    arrestList.count = 1;
                    aiPkt->setActorList(&arrestList);
                    aiPkt->Send(targetPlayer->guid);
                    winMgr->setPlayerDialogueOpen(targetPlayer->getId(), true);
                    mPlayerArrestCooldown[targetPlayer->getId()] = now + std::chrono::seconds(2);
                    Log(Debug::Info) << "[HeadlessServer] Sent ID_ACTOR_AI (ARREST) for guard="
                                     << baseActor.refId << " to pid=" << targetPlayer->getId();
                }
            }
        }
    }
}
