#ifndef TES3MP_HEADLESS_CELL_INSTANCE_MANAGER_HPP
#define TES3MP_HEADLESS_CELL_INSTANCE_MANAGER_HPP

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

namespace TES3MPHeadless
{
    /// Manages opt-in party-instanced interior dungeons (<BaseCell>#party_<id>), while leaving
    /// non-whitelisted interiors (houses, taverns, guilds, shops) shared across all players.
    class CellInstanceManager
    {
    public:
        unsigned int createParty(unsigned short leaderPid);
        bool addPlayerToParty(unsigned int partyId, unsigned short pid);
        bool removePlayerFromParty(unsigned short pid);
        unsigned int getPlayerPartyId(unsigned short pid) const;
        unsigned int getOrAssignSoloPartyId(unsigned short pid);
        std::vector<unsigned short> getPartyMembers(unsigned int partyId) const;

        void setInstancedDungeon(const std::string& cellName, bool instanced);
        bool isInstancedDungeon(const std::string& cellName) const;

        std::string getInstanceName(unsigned int partyId, const std::string& baseCellName) const;
        std::string getOrCreateDungeonInstance(MWWorld::World& world, unsigned int partyId,
            const std::string& baseCellName, mwmp::Networking* networking);

        void clear();

    private:
        unsigned int mNextPartyId = 1;
        std::unordered_set<std::string> mInstancedDungeons;
        std::unordered_map<unsigned short, unsigned int> mPlayerParty;
        std::unordered_map<unsigned int, std::vector<unsigned short>> mParties;
        std::unordered_set<std::string> mCreatedInstances;
    };
}

#endif // TES3MP_HEADLESS_CELL_INSTANCE_MANAGER_HPP
