#include <apps/openmw/mwlua/postprocessingbindings.hpp>
#include <apps/openmw/mwlua/uibindings.hpp>

#include <apps/openmw/mwlua/context.hpp>

#include <components/lua/util.hpp>

#include <osg/Vec2f>

#include <sol/sol.hpp>

namespace MWLua
{
    sol::table initUserInterfacePackage(const Context& context)
    {
        sol::object cached = context.getTypePackage("openmw_ui");
        if (cached != sol::nil)
            return cached;

        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);
        api["showMessage"] = [](sol::variadic_args) {};
        api["updateAll"] = []() {};
        api["registerSettingsPage"] = [](sol::variadic_args) {};
        api["removeSettingsPage"] = [](sol::variadic_args) {};
        api["create"] = [lua](sol::variadic_args) mutable {
            sol::table element(lua, sol::create);
            element["layout"] = sol::table(lua, sol::create);
            element["update"] = []() {};
            element["destroy"] = []() {};
            return element;
        };
        api["content"] = [lua](sol::variadic_args) mutable { return sol::table(lua, sol::create); };
        api["texture"] = [](sol::variadic_args) { return sol::nil; };
        api["_getDefaultFontSize"] = []() { return 16; };
        api["_getMenuTransparency"] = []() { return 0.f; };
        api["_getAllUiModes"] = [lua]() mutable { return sol::table(lua, sol::create); };
        api["_getAllWindowIds"] = [lua]() mutable { return sol::table(lua, sol::create); };
        api["_setWindowDisabled"] = [](sol::variadic_args) {};
        api["_getAllowedWindows"] = [lua](sol::variadic_args) mutable { return sol::table(lua, sol::create); };
        api["_setUiModeStack"] = [](sol::variadic_args) {};
        api["_getUiModeStack"] = [lua]() mutable { return sol::table(lua, sol::create); };
        api["_isWindowVisible"] = [](sol::variadic_args) { return false; };
        api["_setHudVisibility"] = [](sol::variadic_args) {};
        api["_isHudVisible"] = []() { return false; };
        api["_showInteractiveMessage"] = [](sol::variadic_args) {};
        api["getConsoleMode"] = []() { return std::string(); };
        api["setConsoleMode"] = [](sol::variadic_args) {};
        api["setConsoleSelectedObject"] = [](sol::variadic_args) {};
        api["printToConsole"] = [](sol::variadic_args) {};
        api["screenSize"] = []() { return osg::Vec2f(0.f, 0.f); };
        api["layers"] = LuaUtil::makeReadOnly(sol::table(lua, sol::create));

        sol::table consoleColor(lua, sol::create);
        consoleColor["Default"] = 0;
        consoleColor["Error"] = 1;
        consoleColor["Success"] = 2;
        consoleColor["Info"] = 3;
        api["CONSOLE_COLOR"] = LuaUtil::makeReadOnly(consoleColor);

        sol::table alignment(lua, sol::create);
        alignment["Start"] = 0;
        alignment["Center"] = 1;
        alignment["End"] = 2;
        api["ALIGNMENT"] = LuaUtil::makeReadOnly(alignment);

        sol::table uiType(lua, sol::create);
        uiType["Image"] = "Image";
        uiType["Container"] = "Container";
        uiType["Widget"] = "Widget";
        uiType["Text"] = "Text";
        uiType["TextEdit"] = "TextEdit";
        uiType["Flex"] = "Flex";
        uiType["Window"] = "Window";
        api["TYPE"] = LuaUtil::makeReadOnly(uiType);

        sol::table readOnly = LuaUtil::makeReadOnly(api);
        return context.setTypePackage(readOnly, "openmw_ui");
    }

    sol::table initPostprocessingPackage(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);
        api["load"] = [](sol::variadic_args) { return sol::nil; };
        api["getChain"] = [context]() { return sol::table(context.sol(), sol::create); };
        return LuaUtil::makeReadOnly(api);
    }
}
