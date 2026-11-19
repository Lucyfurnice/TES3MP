#ifndef OPENMW_PROCESSORACTORAI_HPP
#define OPENMW_PROCESSORACTORAI_HPP

#include "../ActorProcessor.hpp"

namespace mwmp
{
    class ProcessorActorAI : public ActorProcessor
    {
    public:
        ProcessorActorAI()
        {
            BPP_INIT(ID_ACTOR_AI)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                Script::Call<Script::CallbackIdentity("OnActorAI")>(player.getId(), Utils::getCellDescription(actorList.cell).c_str());
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAI_HPP
