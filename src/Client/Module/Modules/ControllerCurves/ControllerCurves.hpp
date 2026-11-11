#pragma once

#include "../Module.hpp"
#include "Events/Game/TurnDeltaEvent.hpp"
#include "../../../../Assets/Assets.hpp"
#include <utility>

class ControllerCurves : public Module {
public:
    ControllerCurves() : Module("Controller Curves", "Custom joystick response curves, deadzones, and raw input settings.", IDR_SETTINGS_PNG, "") {
    }

    void onEnable() override;

    void onDisable() override;

    void defaultConfig() override;

    void settingsRender(float settingsOffset) override;

    // Direct camera turn delta event listener
    void onTurnDeltaEvent(TurnDeltaEvent& event);

    // Mathematical joystick processor
    static std::pair<float, float> processStick(float rawX, float rawY, int mode, float minDZ, float maxDZ);
};
