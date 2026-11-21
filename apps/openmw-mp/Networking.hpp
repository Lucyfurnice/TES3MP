#ifndef OPENMW_NETWORKING_HPP
#define OPENMW_NETWORKING_HPP

#include <functional>
#include <components/openmw-mp/Base/ServerAuthority.hpp>
#include <components/openmw-mp/Controllers/SystemPacketController.hpp>
#include <components/openmw-mp/Controllers/PlayerPacketController.hpp>
#include <components/openmw-mp/Controllers/ActorPacketController.hpp>
#include <components/openmw-mp/Controllers/ObjectPacketController.hpp>
#include <components/openmw-mp/Controllers/WorldstatePacketController.hpp>
#include <components/openmw-mp/Packets/PacketPreInit.hpp>
#include "Player.hpp"

class MasterClient;
namespace  mwmp
{
    class Networking
    {
    public:
        Networking(RakNet::RakPeerInterface *peer);
        ~Networking();

        void newPlayer(RakNet::RakNetGUID guid);
        void disconnectPlayer(RakNet::RakNetGUID guid);
        void kickPlayer(RakNet::RakNetGUID guid, bool sendNotification = true);
        
        void banAddress(const char *ipAddress);
        void unbanAddress(const char *ipAddress);
        RakNet::SystemAddress getSystemAddress(RakNet::RakNetGUID guid);

        void processSystemPacket(RakNet::Packet *packet);
        void processPlayerPacket(RakNet::Packet *packet);
        void processActorPacket(RakNet::Packet *packet);
        void processObjectPacket(RakNet::Packet *packet);
        void processWorldstatePacket(RakNet::Packet *packet);
        void update(RakNet::Packet *packet, RakNet::BitStream &bsIn);

        unsigned short numberOfConnections() const;
        unsigned int maxConnections() const;
        int getAvgPing(RakNet::AddressOrGUID) const;
        unsigned short getPort() const;

        int mainLoop();
        void pollOnce();
        void tickTimers();
        void terminateTimers();
        bool isRunning() const;
        int getExitCode() const;
        void setActorSyncCallback(std::function<void(unsigned char, const BaseActorList&)> callback);

        void stopServer(int code);

        SystemPacketController *getSystemPacketController() const;
        PlayerPacketController *getPlayerPacketController() const;
        ActorPacketController *getActorPacketController() const;
        ObjectPacketController *getObjectPacketController() const;
        WorldstatePacketController *getWorldstatePacketController() const;

        BaseActorList *getReceivedActorList();
        BaseObjectList *getReceivedObjectList();
        BaseWorldstate *getReceivedWorldstate();

        int getCurrentMpNum();
        void setCurrentMpNum(int value);
        int incrementMpNum();

        bool getDataFileEnforcementState();
        void setDataFileEnforcementState(bool state);

        bool getScriptErrorIgnoringState();
        void setScriptErrorIgnoringState(bool state);

        bool getAuthoritativeServer() const;
        void setAuthoritativeServer(bool state);

        void setPlayerAttackCallback(std::function<bool(Player&, Attack&)> callback);
        void setPlayerCastCallback(std::function<bool(Player&, Cast&)> callback);
        void setObjectActionCallback(std::function<bool(unsigned char, Player&, BaseObjectList&)> callback);
        void setPlayerPositionCallback(std::function<bool(Player&)> callback);
        void setPlayerCellChangeCallback(std::function<void(Player&)> callback);

        bool notifyPlayerAttack(Player& player);
        bool notifyPlayerCast(Player& player);
        bool notifyObjectAction(unsigned char packetId, Player& player, BaseObjectList& objectList);
        bool notifyPlayerPosition(Player& player);
        void notifyPlayerCellChange(Player& player);

        MasterClient *getMasterClient();
        void InitQuery(std::string queryAddr, unsigned short queryPort);
        void setServerPassword(std::string passw) noexcept;
        bool isPassworded() const;

        static const Networking &get();
        static Networking *getPtr();

        void postInit();

        PacketPreInit::PluginContainer &getSamples();
    private:
        bool preInit(RakNet::Packet *packet, RakNet::BitStream &bsIn);
        std::string serverPassword;
        static Networking *sThis;

        RakNet::RakPeerInterface *peer;
        RakNet::BitStream bsOut;
        TPlayers *players;
        MasterClient *mclient;

        BaseSystem baseSystem;
        BaseActorList baseActorList;
        BaseObjectList baseObjectList;
        BaseWorldstate baseWorldstate;

        SystemPacketController *systemPacketController;
        PlayerPacketController *playerPacketController;
        ActorPacketController *actorPacketController;
        ObjectPacketController *objectPacketController;
        WorldstatePacketController *worldstatePacketController;

        bool running;
        int exitCode;
        bool authoritativeServer = false;
        PacketPreInit::PluginContainer samples;
        std::function<void(unsigned char, const BaseActorList&)> actorSyncCallback;
        std::function<bool(Player&, Attack&)> playerAttackCallback;
        std::function<bool(Player&, Cast&)> playerCastCallback;
        std::function<bool(unsigned char, Player&, BaseObjectList&)> objectActionCallback;
        std::function<bool(Player&)> playerPositionCallback;
        std::function<void(Player&)> playerCellChangeCallback;
    };
}


#endif //OPENMW_NETWORKING_HPP
