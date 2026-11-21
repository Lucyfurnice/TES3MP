#ifndef MWLUA_WORLDBINDINGS_H
#define MWLUA_WORLDBINDINGS_H

#include <sol/forward.hpp>
#ifdef TES3MP_HEADLESS
#include <functional>
#include <typeinfo>
#endif

namespace MWLua
{
    struct Context;

    sol::table initWorldPackage(const Context& context);
#ifdef TES3MP_HEADLESS
    void setHeadlessRecordCreatedCallback(std::function<void(const std::type_info&, const void*)> callback);
#endif
}

#endif // MWLUA_WORLDBINDINGS_H
