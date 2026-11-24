#ifndef TES3MP_HEADLESS_RANGEDDEBUG_HPP
#define TES3MP_HEADLESS_RANGEDDEBUG_HPP

#include <cstdlib>

#include <components/debug/debuglog.hpp>

/*
    Start of tes3mp addition

    Toggleable diagnostics for the ranged attack pipeline (NPC archers -> player anchors).

    Enable with the TES3MP_RANGED_DEBUG environment variable (any non-empty value):

        set TES3MP_RANGED_DEBUG=1
        tes3mp-headless.exe

    The flag is read once and cached, so it is cheap enough to leave the checks in release builds.
    All output goes through the normal Log() stream, so it obeys --log-level like every other log line.
*/
namespace TES3MPHeadless
{
    inline bool rangedDebugEnabled()
    {
        static const bool enabled = std::getenv("TES3MP_RANGED_DEBUG") != nullptr;
        return enabled;
    }
}

// Usage: TES3MP_RANGED_LOG << "message " << value;
#define TES3MP_RANGED_LOG if (!TES3MPHeadless::rangedDebugEnabled()) {} else Log(Debug::Info)

/*
    End of tes3mp addition
*/

#endif
