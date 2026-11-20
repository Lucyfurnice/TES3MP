#include <filesystem>

#include <QApplication>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <apps/browser/netutils/QueryClient.hpp>
#include "MainWindow.hpp"

std::string loadSettings(Settings::Manager& settings)
{
    Files::ConfigurationManager mCfgMgr;
    Settings::SettingsFileParser parser;
    // Create the settings manager and load default settings file
    const std::filesystem::path localdefault = mCfgMgr.getLocalPath() / "tes3mp-client-default.cfg";
    const std::filesystem::path globaldefault = mCfgMgr.getGlobalPath() / "tes3mp-client-default.cfg";

    // prefer local
    if (std::filesystem::exists(localdefault))
        parser.loadSettingsFile(localdefault, Settings::Manager::mDefaultSettings, false, false);
    else if (std::filesystem::exists(globaldefault))
        parser.loadSettingsFile(globaldefault, Settings::Manager::mDefaultSettings, false, false);
    else
        throw std::runtime_error("No default settings file found! Make sure the file \"tes3mp-client-default.cfg\" was properly installed.");

    // load user settings if they exist
    const std::filesystem::path settingspath = mCfgMgr.getUserConfigPath() / "tes3mp-client.cfg";
    if (std::filesystem::exists(settingspath))
        parser.loadSettingsFile(settingspath, Settings::Manager::mUserSettings, false, false);

    return Files::pathToUnicodeString(settingspath);
}

int main(int argc, char *argv[])
{
    Settings::Manager mgr;

    loadSettings(mgr);

    std::string addr = Settings::Manager::getString("address", "Master");
    int port = Settings::Manager::getInt("port", "Master");

    // Is this an attempt to connect to the official master server at the old port? If so,
    // redirect it to the correct port for the currently used fork of RakNet
    if (Misc::StringUtils::ciEqual(addr, "master.tes3mp.com") && port == 25560)
        port = 25561;

    // initialize resources, if needed
    // Q_INIT_RESOURCE(resfile);

    QueryClient::Get().SetServer(addr, port);
    QApplication app(argc, argv);
    MainWindow d;

    d.show();
    return app.exec();
}
