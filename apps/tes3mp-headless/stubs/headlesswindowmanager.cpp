#include "headlesswindowmanager.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/translation/translation.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace TES3MPHeadless
{
    HeadlessWindowManager::HeadlessWindowManager(
        const Translation::Storage& translationDataStorage, std::string versionDescription)
        : mTranslationDataStorage(translationDataStorage)
        , mVersionDescription(std::move(versionDescription))
    {
    }

    void HeadlessWindowManager::setStore(const MWWorld::ESMStore& store)
    {
        mStore = &store;
    }

    void HeadlessWindowManager::printToConsole(const std::string& msg, std::string_view /*color*/)
    {
        Log(Debug::Info) << "[Console] " << msg;
    }

    void HeadlessWindowManager::messageBox(std::string_view message, MWGui::ShowInDialogueMode /*showInDialogueMode*/)
    {
        if (!message.empty())
            Log(Debug::Verbose) << "[MessageBox] " << message;
    }

    void HeadlessWindowManager::scheduleMessageBox(std::string message, MWGui::ShowInDialogueMode showInDialogueMode)
    {
        messageBox(message, showInDialogueMode);
    }

    std::string_view HeadlessWindowManager::getGameSettingString(std::string_view id, std::string_view defaultValue)
    {
        if (!mStore)
            return defaultValue;
        const ESM::GameSetting* setting = mStore->get<ESM::GameSetting>().search(id);
        if (setting && setting->mValue.getType() == ESM::VT_String)
            return setting->mValue.getString();
        return defaultValue;
    }

    void HeadlessWindowManager::clear()
    {
        mSelectedSpell = ESM::RefId();
        mSelectedEnchantItem = MWWorld::Ptr();
        mSelectedWeapon = MWWorld::Ptr();
        mRestEnabled = true;
    }
}
