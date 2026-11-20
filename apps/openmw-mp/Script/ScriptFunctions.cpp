#include "ScriptFunctions.hpp"
#include "API/PublicFnAPI.hpp"
#include <cstdarg>
#include <iostream>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

template<typename... Types>
constexpr char TypeString<Types...>::value[];
constexpr ScriptFunctionIdentityData ScriptFunctions::functionIdentities[];
const ScriptFunctionData ScriptFunctions::functions[sizeof(ScriptFunctions::functionIdentities) / sizeof(ScriptFunctions::functionIdentities[0])]{
        TES3MP_SCRIPT_FUNCTION_LIST
};
constexpr ScriptCallbackData ScriptFunctions::callbacks[];

void ScriptFunctions::MakePublic(ScriptFunc _public, const char *name, char ret_type, const char *def) noexcept
{
    Public::MakePublic(_public, name, ret_type, def);
}

boost::any ScriptFunctions::CallPublic(const char *name, va_list args) noexcept
{
    std::vector<boost::any> params;

    try
    {
        std::string def = Public::GetDefinition(name);
        Utils::getArguments(params, args, def);

        return Public::Call(name, params);
    }
    catch (...) {}

    return 0;
}
