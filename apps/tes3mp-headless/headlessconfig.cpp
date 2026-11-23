#include "headlessconfig.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/settings/parser.hpp>

namespace TES3MPHeadless
{
    namespace
    {
        std::string getCfgString(const Settings::CategorySettingValueMap& settings, const std::string& category,
            const std::string& key, const std::string& fallback)
        {
            auto it = settings.find(std::make_pair(category, key));
            if (it == settings.end() || it->second.empty())
                return fallback;
            return it->second;
        }
    }

    std::filesystem::path resolveServerConfigPath(
        Files::ConfigurationManager& cfgMgr, const std::filesystem::path& candidate)
    {
        if (candidate.empty())
            return {};
        if (std::filesystem::exists(candidate))
            return candidate;
        if (candidate.is_relative())
        {
            const std::filesystem::path inLocal = cfgMgr.getLocalPath() / candidate;
            if (std::filesystem::exists(inLocal))
                return inLocal;
            const std::filesystem::path inGlobal = cfgMgr.getGlobalPath() / candidate;
            if (std::filesystem::exists(inGlobal))
                return inGlobal;
        }
        return {};
    }

    std::filesystem::path resolveServerPluginHome(
        Files::ConfigurationManager& cfgMgr, const std::string& rawHome, const std::filesystem::path& cfgDir)
    {
        if (rawHome.empty())
            return {};
        const std::filesystem::path rawPath(rawHome);
        if (std::filesystem::exists(rawPath))
            return rawPath;
        if (!cfgDir.empty())
        {
            const std::filesystem::path nearCfg = cfgDir / rawPath;
            if (std::filesystem::exists(nearCfg))
                return nearCfg;
        }
        const std::filesystem::path nearLocal = cfgMgr.getLocalPath() / rawPath;
        if (std::filesystem::exists(nearLocal))
            return nearLocal;
        return rawPath;
    }

    std::filesystem::path resolveServerHome(
        Files::ConfigurationManager& cfgMgr, const std::filesystem::path& serverCfgPath)
    {
        Settings::CategorySettingValueMap serverSettings;
        Settings::SettingsFileParser parser;

        std::filesystem::path defaultCfg = resolveServerConfigPath(
            cfgMgr, serverCfgPath.empty() ? std::filesystem::path("tes3mp-server-default.cfg") : serverCfgPath);
        if (defaultCfg.empty())
            defaultCfg = resolveServerConfigPath(cfgMgr, "tes3mp-server-default.cfg");

        std::filesystem::path cfgDir;
        try
        {
            if (!defaultCfg.empty())
            {
                parser.loadSettingsFile(defaultCfg, serverSettings, false, true);
                cfgDir = defaultCfg.parent_path();
                const std::filesystem::path siblingOverride = cfgDir / "tes3mp-server.cfg";
                if (siblingOverride != defaultCfg && std::filesystem::exists(siblingOverride))
                    parser.loadSettingsFile(siblingOverride, serverSettings, false, true);
            }
            const std::filesystem::path userCfg = cfgMgr.getUserConfigPath() / "tes3mp-server.cfg";
            if (std::filesystem::exists(userCfg) && userCfg != defaultCfg)
                parser.loadSettingsFile(userCfg, serverSettings, false, true);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to read server config while resolving server home: " << e.what();
        }

        const std::string rawPluginHome = getCfgString(serverSettings, "Plugins", "home", "./server");
        std::filesystem::path home = resolveServerPluginHome(cfgMgr, rawPluginHome, cfgDir);
        if (home.empty())
            home = cfgMgr.getLocalPath() / "server";
        return home;
    }
}
