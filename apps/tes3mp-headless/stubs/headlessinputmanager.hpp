#ifndef TES3MP_HEADLESS_INPUTMANAGER_HPP
#define TES3MP_HEADLESS_INPUTMANAGER_HPP

#include <apps/openmw/mwbase/inputmanager.hpp>

#include <initializer_list>
#include <string>
#include <unordered_map>

namespace TES3MPHeadless
{
    /// Headless no-op implementation of MWBase::InputManager.
    class HeadlessInputManager final : public MWBase::InputManager
    {
    public:
        HeadlessInputManager() = default;

        void clear() override { mControlSwitches.clear(); }
        void update(float dt, bool disableControls, bool disableEvents = false) override {}
        void changeInputMode(bool guiMode) override {}
        void processChangedSettings(const std::set<std::pair<std::string, std::string>>& changed) override {}

        void setDragDrop(bool dragDrop) override {}
        bool isGamepadGuiCursorEnabled() override { return false; }
        void setGamepadGuiCursorEnabled(bool enabled) override {}

        void toggleControlSwitch(std::string_view sw, bool value) override
        {
            mControlSwitches[std::string(sw)] = value;
        }
        bool getControlSwitch(std::string_view sw) override
        {
            auto it = mControlSwitches.find(std::string(sw));
            return it == mControlSwitches.end() ? true : it->second;
        }

        std::string_view getActionDescription(int action) const override { return {}; }
        std::string getActionKeyBindingName(int action) const override { return {}; }
        std::string getActionControllerBindingName(int action) const override { return {}; }
        bool actionIsActive(int action) const override { return false; }
        float getActionValue(int action) const override { return 0.f; }
        bool isControllerButtonPressed(SDL_GameControllerButton button) const override { return false; }
        float getControllerAxisValue(SDL_GameControllerAxis axis) const override { return 0.f; }
        int getMouseMoveX() const override { return 0; }
        int getMouseMoveY() const override { return 0; }
        void warpMouseToWidget(MyGUI::Widget* widget) override {}

        const std::initializer_list<int>& getActionKeySorting() override
        {
            static const std::initializer_list<int> empty{};
            return empty;
        }
        const std::initializer_list<int>& getActionControllerSorting() override
        {
            static const std::initializer_list<int> empty{};
            return empty;
        }
        int getNumActions() override { return 0; }
        void enableDetectingBindingMode(int action, bool keyboard) override {}
        void resetToDefaultKeyBindings() override {}
        void resetToDefaultControllerBindings() override {}

        bool joystickLastUsed() override { return false; }
        void setJoystickLastUsed(bool enabled) override {}
        std::string getControllerButtonIcon(int button) override { return {}; }
        std::string getControllerAxisIcon(int axis) override { return {}; }

        size_t countSavedGameRecords() const override { return 0; }
        void write(ESM::ESMWriter& writer, Loading::Listener& progress) override {}
        void readRecord(ESM::ESMReader& reader, uint32_t type) override {}

        void resetIdleTime() override {}
        bool isIdle() const override { return true; }
        void executeAction(int action) override {}
        bool controlsDisabled() override { return false; }
        void saveBindings() override {}

    private:
        std::unordered_map<std::string, bool> mControlSwitches;
    };
}

#endif
