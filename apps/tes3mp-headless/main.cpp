#include "headlessengine.hpp"

#include <boost/program_options.hpp>

#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/settings/settings.hpp>
#include <components/version/version.hpp>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    TES3MPHeadless::HeadlessEngine* gEngineInstance = nullptr;

    void handleSignal(int /*sig*/)
    {
        if (gEngineInstance)
            gEngineInstance->requestStop();
    }

    boost::program_options::options_description makeHeadlessOptionsDescription()
    {
        namespace bpo = boost::program_options;
        bpo::options_description desc("TES3MP Headless Simulation Options");

        Files::ConfigurationManager::addCommonOptions(desc);
        auto add = desc.add_options();
        add("help", "Print help message");
        add("version", "Print version information and exit");
        add("data", bpo::value<Files::MaybeQuotedPathContainer>()->default_value(Files::MaybeQuotedPathContainer(), "data")->multitoken()->composing(),
            "Set data directories (later directories have higher priority)");
        add("data-local", bpo::value<Files::MaybeQuotedPathContainer::value_type>()->default_value(Files::MaybeQuotedPathContainer::value_type(), ""),
            "Set local data directory");
        add("fallback-archive", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing(),
            "Add BSA archive file");
        add("content", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing(),
            "Load content file (.esm/.esp/.omwgame/.omwaddon/.omwscripts)");
        add("groundcover", bpo::value<std::vector<std::string>>()->default_value({}, "")->multitoken()->composing(),
            "Load groundcover content file");
        add("encoding", bpo::value<std::string>()->default_value("win1252"),
            "Character encoding for legacy ESM strings (win1250, win1251, win1252)");
        add("start-cell", bpo::value<std::string>()->default_value(""),
            "Initial cell name or exterior grid to load on startup");
        add("server-cfg", bpo::value<std::string>()->default_value("tes3mp-server-default.cfg"),
            "Path to tes3mp-server configuration file (default tes3mp-server-default.cfg)");
        add("port", bpo::value<uint16_t>()->default_value(25565),
            "UDP port override for TES3MP networking");
        add("tick-rate", bpo::value<double>()->default_value(60.0),
            "Fixed simulation tick rate in Hz (default 60)");
        add("max-ticks", bpo::value<uint64_t>()->default_value(0),
            "Exit cleanly after N simulation ticks (0 = run until signal)");
        add("cell-radius", bpo::value<int>()->default_value(1),
            "Exterior cell residency radius around each player (default 1 = 3x3 neighborhood)");
        add("exterior-cell-radius", bpo::value<int>(),
            "Override exterior cell residency radius around each player (0 = active cell only, 1 = 3x3)");
        add("cell-grace-period", bpo::value<double>()->default_value(5.0),
            "Seconds before empty unpinned cells unload (default 5.0)");
        add("authoritative", bpo::value<bool>()->default_value(true),
            "Enable authoritative server NPC AI, combat, and object simulation (default true)");
        add("profile-ticks", bpo::bool_switch()->default_value(false),
            "Log per-phase tick timing breakdown (pollNetwork, World, Mechanics, Lua, broadcast)");
        add("random-seed", bpo::value<unsigned int>()->default_value(0),
            "RNG seed (0 = time-based/default)");
        add("script-blacklist-warnings", bpo::value<int>()->default_value(0),
            "MWScript warning verbosity (0, 1, 2)");
        add("fallback", bpo::value<Fallback::FallbackMap>()->default_value({}, "")->multitoken()->composing(),
            "Fallback INI entries from openmw.cfg");

        return desc;
    }

    int runHeadless(int argc, char** argv)
    {
        namespace bpo = boost::program_options;

        Files::ConfigurationManager cfgMgr;
        bpo::options_description desc = makeHeadlessOptionsDescription();
        bpo::variables_map variables;

        Files::parseArgs(argc, argv, variables, desc);
        bpo::notify(variables);

        if (variables.count("help"))
        {
            Debug::getRawStdout() << desc << std::endl;
            return 0;
        }

        if (variables.count("version"))
        {
            Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
            return 0;
        }

        cfgMgr.processPaths(variables, std::filesystem::current_path());
        cfgMgr.readConfiguration(variables, desc);
        Debug::setupLogging(cfgMgr.getLogPath(), "tes3mp-headless");

        Settings::Manager settings;
        settings.load(cfgMgr);

        if (variables.count("fallback"))
            Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);

        TES3MPHeadless::HeadlessConfig config;
        config.mResourceDir = variables["resources"].as<Files::MaybeQuotedPath>();
        if (config.mResourceDir.empty())
            config.mResourceDir = std::filesystem::current_path() / "resources";

        const auto dataDirs = asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>());
        const auto localDir = variables["data-local"].as<Files::MaybeQuotedPathContainer::value_type>();
        config.mDataDirs = std::move(dataDirs);
        if (!localDir.empty())
            config.mDataDirs.push_back(localDir);

        config.mArchives = variables["fallback-archive"].as<std::vector<std::string>>();
        config.mContentFiles = variables["content"].as<std::vector<std::string>>();
        config.mGroundcoverFiles = variables["groundcover"].as<std::vector<std::string>>();
        config.mEncoding = ToUTF8::calculateEncoding(variables["encoding"].as<std::string>());
        config.mStartCell = variables["start-cell"].as<std::string>();
        config.mServerCfgPath = variables["server-cfg"].as<std::string>();
        config.mPort = variables["port"].as<uint16_t>();
        config.mHasPortOverride = variables.count("port") && !variables["port"].defaulted();
        config.mTickRate = variables["tick-rate"].as<double>();
        config.mMaxTicks = variables["max-ticks"].as<uint64_t>();
        config.mCellRadius = std::max(0, variables["cell-radius"].as<int>());
        if (variables.count("exterior-cell-radius"))
            config.mCellRadius = std::max(0, variables["exterior-cell-radius"].as<int>());
        config.mCellGracePeriod = std::max(0.0, variables["cell-grace-period"].as<double>());
        config.mAuthoritative = variables["authoritative"].as<bool>();
        config.mProfileTicks = variables["profile-ticks"].as<bool>();
        config.mRandomSeed = variables["random-seed"].as<unsigned int>();
        config.mWarningsMode = variables["script-blacklist-warnings"].as<int>();

        TES3MPHeadless::HeadlessEngine engine(cfgMgr, std::move(config));
        gEngineInstance = &engine;
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        try
        {
            engine.init();
            engine.run();
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Fatal error: " << e.what();
            gEngineInstance = nullptr;
            return 1;
        }

        gEngineInstance = nullptr;
        return 0;
    }
}

int main(int argc, char** argv)
{
    return Debug::wrapApplication(&runHeadless, argc, argv, "TES3MP-Headless");
}
