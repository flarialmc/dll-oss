#include "ControllerCurves.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

void ControllerCurves::onEnable() {
    // Listen to camera turn events on enable
    Listen(this, TurnDeltaEvent, &ControllerCurves::onTurnDeltaEvent)
    Module::onEnable();
}

void ControllerCurves::onDisable() {
    // Stop listening to turn events on disable
    Deafen(this, TurnDeltaEvent, &ControllerCurves::onTurnDeltaEvent)
    Module::onDisable();
}

void ControllerCurves::defaultConfig() {
    Module::defaultConfig("all");
    
    // Default module configuration
    setDef("curve_mode", 1);          // 0: Standard, 1: Linear, 2: Dynamic
    setDef("min_deadzone", 0.05f);    // 5% Inner deadzone
    setDef("max_deadzone", 0.95f);    // 95% Outer deadzone
    setDef("raw_input", true);        // Raw input bypass
}

void ControllerCurves::settingsRender(float settingsOffset) {
    initSettingsPage();

    addHeader("Response Curve Settings");

    // Native Flarial dropdown selector
    addDropdown("Response Curve", "Changes camera acceleration for 1:1 raw aiming consistency.",
        std::vector<std::string>{"Standard", "Linear", "Dynamic"}, "curve_mode", true);

    // Deadzone slider controls
    addSlider("Min Deadzone", "Eliminates stick drift and sets center stick sensitivity.", "min_deadzone", 0.00f, 0.25f, 0.01f);
    addSlider("Max Deadzone", "Threshold for 100% turn speed without hitting plastic edges.", "max_deadzone", 0.75f, 1.00f, 0.01f);

    // Raw input toggle switch
    addToggle("Raw Controller Input", "Bypasses OS smoothing for lower latency.", "raw_input");

    FlarialGUI::UnsetScrollView();
    resetPadding();
}

// Intercepts camera turn delta in real time
void ControllerCurves::onTurnDeltaEvent(TurnDeltaEvent& event) {
    if (!this->isEnabled()) return;

    // Process turn delta coordinates through response curves and deadzones
    auto [newX, newY] = processStick(
        event.delta.x,
        event.delta.y,
        getOps<int>("curve_mode"),
        getOps<float>("min_deadzone"),
        getOps<float>("max_deadzone")
    );

    // Apply transformed delta to camera
    event.delta.x = newX;
    event.delta.y = newY;
}

// Mathematical calculation engine
std::pair<float, float> ControllerCurves::processStick(float rawX, float rawY, int mode, float minDZ, float maxDZ) {
    float magnitude = std::sqrt(rawX * rawX + rawY * rawY);

    // Inner Deadzone: Kill stick drift if stick is centered
    if (magnitude <= 0.0001f || magnitude <= minDZ) {
        return { 0.0f, 0.0f };
    }

    // Normalization between inner (minDZ) and outer (maxDZ) thresholds
    float normalizedMagnitude = (magnitude - minDZ) / (maxDZ - minDZ);
    normalizedMagnitude = std::clamp(normalizedMagnitude, 0.0f, 1.0f);

    float newMagnitude = normalizedMagnitude;

    // Apply selected curve profile
    if (mode == 0) {
        // Standard (Vanilla exponential Minecraft curve)
        newMagnitude = std::pow(normalizedMagnitude, 2.0f);
    } else if (mode == 1) {
        // Linear (1:1 direct raw aiming)
        newMagnitude = normalizedMagnitude;
    } else if (mode == 2) {
        // Dynamic (Fine micro-adjustments in center, rapid acceleration at edges)
        newMagnitude = std::pow(normalizedMagnitude, 1.6f);
    }

    // Reconstruct directional vector with transformed magnitude
    float dirX = rawX / magnitude;
    float dirY = rawY / magnitude;

    return { dirX * newMagnitude, dirY * newMagnitude };
}
