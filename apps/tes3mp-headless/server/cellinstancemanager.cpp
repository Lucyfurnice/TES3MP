#include "cellinstancemanager.hpp"

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmp/RecordHelper.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/openmw-mp/Base/BaseWorldstate.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

#include <algorithm>

namespace TES3MPHeadless
{
    unsigned int CellInstanceManager::createParty(unsigned short leaderPid)
    {
        removePlayerFromParty(leaderPid);
        const unsigned int partyId = mNextPartyId++;
        mParties[partyId] = { leaderPid };
        mPlayerParty[leaderPid] = partyId;
        return partyId;
    }

    bool CellInstanceManager::addPlayerToParty(unsigned int partyId, unsigned short pid)
    {
        if (partyId == 0)
            return false;
        removePlayerFromParty(pid);
        auto& members = mParties[partyId];
        if (std::find(members.begin(), members.end(), pid) == members.end())
            members.push_back(pid);
        mPlayerParty[pid] = partyId;
        return true;
    }

    bool CellInstanceManager::removePlayerFromParty(unsigned short pid)
    {
        auto it = mPlayerParty.find(pid);
        if (it == mPlayerParty.end())
            return false;

        const unsigned int partyId = it->second;
        mPlayerParty.erase(it);

        if (auto partyIt = mParties.find(partyId); partyIt != mParties.end())
        {
            auto& members = partyIt->second;
            members.erase(std::remove(members.begin(), members.end(), pid), members.end());
            if (members.empty())
                mParties.erase(partyIt);
        }
        return true;
    }

    unsigned int CellInstanceManager::getPlayerPartyId(unsigned short pid) const
    {
        auto it = mPlayerParty.find(pid);
        return it != mPlayerParty.end() ? it->second : 0;
    }

    unsigned int CellInstanceManager::getOrAssignSoloPartyId(unsigned short pid)
    {
        if (unsigned int existing = getPlayerPartyId(pid); existing != 0)
            return existing;
        return createParty(pid);
    }

    std::vector<unsigned short> CellInstanceManager::getPartyMembers(unsigned int partyId) const
    {
        auto it = mParties.find(partyId);
        return it != mParties.end() ? it->second : std::vector<unsigned short>{};
    }

    void CellInstanceManager::setInstancedDungeon(const std::string& cellName, bool instanced)
    {
        if (cellName.empty())
            return;
        const std::string key = Misc::StringUtils::lowerCase(cellName);
        if (instanced)
            mInstancedDungeons.insert(key);
        else
            mInstancedDungeons.erase(key);
    }

    bool CellInstanceManager::isInstancedDungeon(const std::string& cellName) const
    {
        if (cellName.empty())
            return false;
        return mInstancedDungeons.find(Misc::StringUtils::lowerCase(cellName)) != mInstancedDungeons.end();
    }

    std::string CellInstanceManager::getInstanceName(unsigned int partyId, const std::string& baseCellName) const
    {
        return baseCellName + "#party_" + std::to_string(partyId);
    }

    std::string CellInstanceManager::getOrCreateDungeonInstance(MWWorld::World& world, unsigned int partyId,
        const std::string& baseCellName, mwmp::Networking* networking)
    {
        if (partyId == 0 || baseCellName.empty())
            return baseCellName;

        const std::string instanceName = getInstanceName(partyId, baseCellName);
        const std::string instanceKey = Misc::StringUtils::lowerCase(instanceName);

        const ESM::Cell* baseCell = world.getStore().get<ESM::Cell>().search(ESM::RefId::stringRefId(baseCellName));
        if (!baseCell || baseCell->isExterior())
            return baseCellName;

        mwmp::CellRecord cellRec{};
        cellRec.baseId = baseCellName;
        cellRec.data.mName = instanceName;
        cellRec.data.mData.mFlags |= ESM::Cell::Interior;
        cellRec.data.updateId();

        if (mCreatedInstances.find(instanceKey) == mCreatedInstances.end())
        {
            RecordHelper::overrideRecord(cellRec);
            mCreatedInstances.insert(instanceKey);
            Log(Debug::Info) << "Created instanced dungeon cell '" << instanceName << "' from '" << baseCellName
                             << "' for party " << partyId;
        }

        if (networking)
        {
            mwmp::BaseWorldstate ws;
            ws.recordsType = mwmp::RECORD_TYPE::CELL;
            ws.cellRecords.push_back(cellRec);
            if (mwmp::WorldstatePacket* pkt
                = networking->getWorldstatePacketController()->GetPacket(ID_RECORD_DYNAMIC))
            {
                for (unsigned short memberPid : getPartyMembers(partyId))
                {
                    if (::Player* member = Players::getPlayer(memberPid))
                    {
                        ws.guid = member->guid;
                        pkt->setWorldstate(&ws);
                        pkt->Send(false);
                    }
                }
            }
        }

        return instanceName;
    }

    void CellInstanceManager::clear()
    {
        mNextPartyId = 1;
        mInstancedDungeons.clear();
        mPlayerParty.clear();
        mParties.clear();
        mCreatedInstances.clear();
    }
}
