#ifndef TES3MP_HEADLESS_CONFIG_HPP
#define TES3MP_HEADLESS_CONFIG_HPP

#include <filesystem>
#include <string>

namespace Files
{
    struct ConfigurationManager;
}

namespace TES3MPHeadless
{
    std::filesystem::path resolveServerConfigPath(
        Files::ConfigurationManager& cfgMgr, const std::filesystem::path& candidate);

    std::filesystem::path resolveServerPluginHome(
        Files::ConfigurationManager& cfgMgr, const std::string& rawHome, const std::filesystem::path& cfgDir);

    /// Resolve the server plugin home directory ([Plugins] home, default "./server") from the
    /// tes3mp-server config files. Usable before World/HeadlessServer exist.
    std::filesystem::path resolveServerHome(
        Files::ConfigurationManager& cfgMgr, const std::filesystem::path& serverCfgPath);
}

#endif
