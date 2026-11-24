#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/esm3/creaturestats.hpp>
#include "PacketActorStatsDynamic.hpp"

using namespace mwmp;

PacketActorStatsDynamic::PacketActorStatsDynamic(RakNet::RakPeerInterface *peer) : ActorPacket(peer)
{
    packetID = ID_ACTOR_STATS_DYNAMIC;
}

void PacketActorStatsDynamic::Actor(BaseActor &actor, bool send)
{
    RW(actor.creatureStats.mDynamic, send);

    /*
        Start of tes3mp change

        Replicate the death flags alongside the dynamic stats. Without these, the shadow
        SimpleCreatureStats on the receiving side is left uninitialised (or stale), and a
        stats packet can silently resurrect an actor whose ID_ACTOR_DEATH was already applied.
    */
    RW(actor.creatureStats.mDead, send);
    RW(actor.creatureStats.mDeathAnimationFinished, send);
    /*
        End of tes3mp change
    */

    actor.hasStatsDynamicData = true;
}
