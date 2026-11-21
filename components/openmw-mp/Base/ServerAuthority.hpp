#ifndef OPENMW_SERVER_AUTHORITY_HPP
#define OPENMW_SERVER_AUTHORITY_HPP

#include <cstdint>

namespace mwmp
{
    /// Protocol-independent authoritative server simulation settings and delta broadcast thresholds.
    /// Kept self-contained in components/openmw-mp so client and server branches can share or cherry-pick cleanly.
    struct ServerAuthorityConfig
    {
        bool authoritativeMode = true;
        bool simulateNpcAI = true;
        bool validateCombat = true;
        bool validateObjects = true;
        bool validatePlayerMovement = true;

        float actorPosDeltaSq = 4.0f;       // >= 2.0 world units displacement triggers ID_ACTOR_POSITION
        float actorRotDeltaRad = 0.05f;     // >= 0.05 rad rotation delta
        float actorStatDelta = 0.5f;        // >= 0.5 HP/Magicka/Fatigue delta triggers ID_ACTOR_STATS_DYNAMIC
        double networkBroadcastInterval = 1.0 / 20.0; // 20 Hz outbound authoritative actor broadcast cap
        float actorSimulationRadius = 6144.0f; // exterior AI/mechanics/physics activation radius (~0.75 cell)
        float actorBroadcastRadius = 7168.0f;  // exterior delta broadcast visibility radius around observing players
        unsigned int maxActorsPerBroadcast = 64; // cap actor entries per outbound cell batch
        float maxPlayerSpeed = 3000.0f;     // max accepted speed (units/s) before sanity clamp
        float playerTeleportAllowance = 512.0f;
        float maxInteractDistance = 600.0f; // server-side max reach for object/door/container interactions
    };
}

#endif // OPENMW_SERVER_AUTHORITY_HPP
