#include <components/openmw-mp/TimedLog.hpp>
#include <apps/openmw/mwclass/creature.hpp>

#include "../mwbase/environment.hpp"

#include "../mwclass/npc.hpp"

#include "../mwmechanics/creaturestats.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/worldimp.hpp"

#include "PlayerList.hpp"
#include "Main.hpp"
#include "DedicatedPlayer.hpp"
#include "CellController.hpp"
#include "GUIController.hpp"


using namespace mwmp;

std::map <RakNet::RakNetGUID, DedicatedPlayer *> PlayerList::playerList;

void PlayerList::update(float dt)
{
    for (auto &playerEntry : playerList)
    {
        DedicatedPlayer *player = playerEntry.second;
        if (player == nullptr) continue;

        player->update(dt);
    }
}

DedicatedPlayer *PlayerList::newPlayer(RakNet::RakNetGUID guid)
{
    LOG_APPEND(TimedLog::LOG_INFO, "- Creating new DedicatedPlayer with guid %s", guid.ToString());

    playerList[guid] = new DedicatedPlayer(guid);

    LOG_APPEND(TimedLog::LOG_INFO, "- There are now %i DedicatedPlayers", playerList.size());

    return playerList[guid];
}

void PlayerList::deletePlayer(RakNet::RakNetGUID guid)
{
    if (playerList[guid]->reference)
        playerList[guid]->deleteReference();

    delete playerList[guid];
    playerList.erase(guid);
}

void PlayerList::cleanUp()
{
    for (auto &playerEntry : playerList)
        delete playerEntry.second;
}

DedicatedPlayer *PlayerList::getPlayer(RakNet::RakNetGUID guid)
{
    return playerList[guid];
}

DedicatedPlayer *PlayerList::getPlayer(const MWWorld::Ptr &ptr)
{
    const ESM::RefId refId = ptr.getCellRef().getRefId();

    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (playerEntry.second->getPtr() == ptr
            || (!refId.empty() && playerEntry.second->getPtr().getCellRef().getRefId() == refId))
            return playerEntry.second;
    }

    return nullptr;
}

DedicatedPlayer* PlayerList::getPlayer(ESM::RefNum actorRefNum)
{
    for (auto& playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWWorld::Ptr playerPtr = playerEntry.second->getPtr();
        ESM::RefNum playerRefNum = playerPtr.getCellRef().getRefNum();

        if (actorRefNum == playerRefNum)
            return playerEntry.second;
    }

    return nullptr;
}

std::vector<RakNet::RakNetGUID> PlayerList::getPlayersInCell(const ESM::Cell& cell)
{
    std::vector<RakNet::RakNetGUID> playersInCell;

    for (auto& playerEntry : playerList)
    {
        if (playerEntry.first != RakNet::UNASSIGNED_CRABNET_GUID)
        {
            if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
            {
                playersInCell.push_back(playerEntry.first);
            }
        }
    }

    return playersInCell;
}

bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr &ptr)
{
    if (ptr.mRef == nullptr)
        return false;

    // DedicatedPlayers are placed via World::placeObject which assigns a generated RefNum (mIndex != 0),
    // while their mpNum remains 0 (unlike DedicatedActors).
    if (ptr.getCellRef().getMpNum() != 0)
        return false;

    return (getPlayer(ptr) != nullptr);
}

void PlayerList::enableMarkers(const ESM::Cell& cell)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        if (Main::get().getCellController()->isSameCell(cell, playerEntry.second->cell))
        {
            playerEntry.second->enableMarker();
        }
    }
}

/*
    Go through all DedicatedPlayers checking if their mHitAttemptActorId matches this one
    and set it to -1 if it does

    This resets the combat target for a DedicatedPlayer's followers in Actors::update()
*/
void PlayerList::clearHitAttemptActorId(ESM::RefNum actorRefNum)
{
    for (auto &playerEntry : playerList)
    {
        if (playerEntry.second == nullptr || playerEntry.second->getPtr().mRef == nullptr)
            continue;

        MWMechanics::CreatureStats &playerCreatureStats = playerEntry.second->getPtr().getClass().getCreatureStats(playerEntry.second->getPtr());

        if (playerCreatureStats.getHitAttemptActor() == actorRefNum)
            playerCreatureStats.setHitAttemptActor({});
    }
}
