#include "Cells.hpp"

#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <apps/openmw-mp/Script/ScriptFunctions.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Networking.hpp>

#include <iostream>

static std::string tempCellDescription;
static std::string tempInstanceCellName;
#ifdef TES3MP_HEADLESS
static std::function<void(const char *, bool)> sSetCellPinnedHandler;
static std::function<unsigned int(unsigned short)> sCreatePartyHandler;
static std::function<bool(unsigned int, unsigned short)> sAddPlayerToPartyHandler;
static std::function<bool(unsigned short)> sRemovePlayerFromPartyHandler;
static std::function<unsigned int(unsigned short)> sGetPlayerPartyIdHandler;
static std::function<void(const char *, bool)> sSetInstancedDungeonHandler;
static std::function<std::string(unsigned int, const char *)> sGetOrCreateDungeonInstanceHandler;
#endif

unsigned int CellFunctions::GetCellStateChangesSize(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cellStateChanges.size();
}

unsigned int CellFunctions::GetCellStateType(unsigned short pid, unsigned int index) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cellStateChanges.at(index).type;
}

const char *CellFunctions::GetCellStateDescription(unsigned short pid, unsigned int index) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, "");

    if (index >= player->cellStateChanges.size())
        return "invalid";

    tempCellDescription = Utils::getCellDescription(player->cellStateChanges.at(index).cell);
    return tempCellDescription.c_str();
}

const char *CellFunctions::GetCell(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    tempCellDescription = Utils::getCellDescription(player->cell).c_str();
    return tempCellDescription.c_str();
}

int CellFunctions::GetExteriorX(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mX;
}

int CellFunctions::GetExteriorY(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);
    return player->cell.mData.mY;
}

bool CellFunctions::IsInExterior(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->cell.isExterior();
}

const char *CellFunctions::GetRegion(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, 0);

    return player->cell.mRegion.getRefIdString().c_str();
}

bool CellFunctions::IsChangingRegion(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, false);

    return player->isChangingRegion;
}

void CellFunctions::SetCell(unsigned short pid, const char *cellDescription) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %s", player->npc.mName.c_str(),
                       Utils::getCellDescription(player->cell).c_str(), cellDescription);

    player->cell = Utils::getCellFromDescription(cellDescription);
#ifdef TES3MP_HEADLESS
    if (mwmp::Networking::getPtr())
        mwmp::Networking::getPtr()->notifyPlayerCellChange(*player);
#endif
}

void CellFunctions::SetExteriorCell(unsigned short pid, int x, int y) noexcept
{
    Player *player;
    GET_PLAYER(pid, player,);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Script is moving %s from %s to %i,%i", player->npc.mName.c_str(),
                       Utils::getCellDescription(player->cell).c_str(), x, y);

    // If the player is currently in an interior, turn off the interior flag
    // from the cell
    if (!player->cell.isExterior())
        player->cell.mData.mFlags &= ~ESM::Cell::Interior;

    player->cell.mData.mX = x;
    player->cell.mData.mY = y;
#ifdef TES3MP_HEADLESS
    if (mwmp::Networking::getPtr())
        mwmp::Networking::getPtr()->notifyPlayerCellChange(*player);
#endif
}

void CellFunctions::SendCell(unsigned short pid) noexcept
{
    Player *player;
    GET_PLAYER(pid, player, );

    mwmp::PlayerPacket *packet = mwmp::Networking::get().getPlayerPacketController()->GetPacket(ID_PLAYER_CELL_CHANGE);
    packet->setPlayer(player);

    packet->Send(false);
}

unsigned int CellFunctions::CreateParty(unsigned short leaderPid) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sCreatePartyHandler)
        return sCreatePartyHandler(leaderPid);
#endif
    return 0;
}

bool CellFunctions::AddPlayerToParty(unsigned int partyId, unsigned short pid) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sAddPlayerToPartyHandler)
        return sAddPlayerToPartyHandler(partyId, pid);
#endif
    return false;
}

bool CellFunctions::RemovePlayerFromParty(unsigned short pid) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sRemovePlayerFromPartyHandler)
        return sRemovePlayerFromPartyHandler(pid);
#endif
    return false;
}

unsigned int CellFunctions::GetPlayerPartyId(unsigned short pid) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sGetPlayerPartyIdHandler)
        return sGetPlayerPartyIdHandler(pid);
#endif
    return 0;
}

void CellFunctions::SetInstancedDungeon(const char *cellName, bool instanced) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sSetInstancedDungeonHandler && cellName)
        sSetInstancedDungeonHandler(cellName, instanced);
#endif
}

const char *CellFunctions::GetOrCreateDungeonInstance(unsigned int partyId, const char *baseCellName) noexcept
{
#ifdef TES3MP_HEADLESS
    if (sGetOrCreateDungeonInstanceHandler && baseCellName)
    {
        tempInstanceCellName = sGetOrCreateDungeonInstanceHandler(partyId, baseCellName);
        return tempInstanceCellName.c_str();
    }
#endif
    tempInstanceCellName = baseCellName ? baseCellName : "";
    return tempInstanceCellName.c_str();
}

#ifdef TES3MP_HEADLESS
void CellFunctions::SetCellPinned(const char *cellDescription, bool pinned) noexcept
{
    if (sSetCellPinnedHandler && cellDescription)
        sSetCellPinnedHandler(cellDescription, pinned);
}

void CellFunctions::SetCellPinnedHandler(std::function<void(const char *, bool)> handler)
{
    sSetCellPinnedHandler = std::move(handler);
}

void CellFunctions::SetPartyHandlers(
    std::function<unsigned int(unsigned short)> createParty,
    std::function<bool(unsigned int, unsigned short)> addPlayer,
    std::function<bool(unsigned short)> removePlayer,
    std::function<unsigned int(unsigned short)> getPartyId,
    std::function<void(const char *, bool)> setInstancedDungeon,
    std::function<std::string(unsigned int, const char *)> getOrCreateInstance)
{
    sCreatePartyHandler = std::move(createParty);
    sAddPlayerToPartyHandler = std::move(addPlayer);
    sRemovePlayerFromPartyHandler = std::move(removePlayer);
    sGetPlayerPartyIdHandler = std::move(getPartyId);
    sSetInstancedDungeonHandler = std::move(setInstancedDungeon);
    sGetOrCreateDungeonInstanceHandler = std::move(getOrCreateInstance);
}
#endif
