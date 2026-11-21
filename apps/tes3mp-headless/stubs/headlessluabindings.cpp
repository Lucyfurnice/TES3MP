#include <apps/openmw/mwlua/postprocessingbindings.hpp>
#include <apps/openmw/mwlua/uibindings.hpp>

#include <apps/openmw/mwlua/context.hpp>

#include <sol/sol.hpp>

namespace MWLua
{
    sol::table initUserInterfacePackage(const Context& context)
    {
        sol::table api(context.mLua->unsafeState(), sol::create);
        return api;
    }

    sol::table initPostprocessingPackage(const Context& context)
    {
        sol::table api(context.mLua->unsafeState(), sol::create);
        return api;
    }
}
