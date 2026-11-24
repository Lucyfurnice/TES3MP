#ifndef OPENMW_PROCESSORPLAYERDEATH_HPP
#define OPENMW_PROCESSORPLAYERDEATH_HPP

#include "../PlayerProcessor.hpp"
#include <chrono>

namespace mwmp
{
    class ProcessorPlayerDeath : public PlayerProcessor
    {
    public:
        ProcessorPlayerDeath()
        {
            BPP_INIT(ID_PLAYER_DEATH)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            /*
                Start of tes3mp change

                Report a player's death to the server scripts exactly once per death.

                The server announces a death it resolved itself (HeadlessCombatController) and the
                client also echoes ID_PLAYER_DEATH for every death it enqueues, including the one it
                just received from the server. Processing each of those echoes re-ran
                OnPlayerDeath, which made the kill message repeat and - once the client had cleared
                its killer after the first echo - finally reported a bogus "committed suicide".
            */
            if (player.creatureStats.mDead)
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Player is already dead; ignoring duplicate death report");
                return;
            }

            player.creatureStats.mDead = true;

            packet.Send(true);

            Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(player.getId());
            /*
                End of tes3mp change
            */
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERDEATH_HPP
