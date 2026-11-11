#pragma once

#include "../Module.hpp"
#include <cmath>
#include <algorithm>
#include <utility>

class ControllerCurves : public Module {
public:
    ControllerCurves() : Module("Controller Curves", "Custom joystick response curves, deadzones, and raw input settings.",
        IDR_SETTINGS_PNG, "", false, {"controller", "gamepad", "joystick", "curves", "aim"}) {
    };

    void onEnable() override {
        Module::onEnable();
    }

    void onDisable() override {
        Module::onDisable();
    }

    void defaultConfig() override {
        Module::defaultConfig();
        // Valores por defecto
        setDef("curve_mode", 1);          // 0: Standard, 1: Linear, 2: Dynamic
        setDef("min_deadzone", 0.05f);    // Zona muerta mínima
        setDef("max_deadzone", 0.95f);    // Zona muerta máxima
        setDef("raw_input", true);        // Entrada directa activada
    }

    void settingsRender(float settingsOffset) override {
        initSettingsPage();

        addHeader("Response Curve");
        // Dibuja los sliders y botones en el menú de Flarial
        addSlider("Min Deadzone", "Eliminates stick drift", "min_deadzone", 0.00f, 0.25f, 0.01f);
        addSlider("Max Deadzone", "Threshold for 100% turn speed", "max_deadzone", 0.75f, 1.00f, 0.01f);
        addToggle("Raw Controller Input", "Bypasses OS smoothing for lower latency", "raw_input");

        FlarialGUI::UnsetScrollView();
        resetPadding();
    }

    // Motor matemático para transformar la señal del stick
    static std::pair<float, float> processStick(float rawX, float rawY, int mode, float minDZ, float maxDZ) {
        float magnitude = std::sqrt(rawX * rawX + rawY * rawY);
        
        // Zona muerta mínima: frena el drift
        if (magnitude <= 0.0001f || magnitude <= minDZ) {
            return { 0.0f, 0.0f };
        }

        // Normalización entre la zona mínima y máxima
        float normalizedMagnitude = (magnitude - minDZ) / (maxDZ - minDZ);
        normalizedMagnitude = std::clamp(normalizedMagnitude, 0.0f, 1.0f);

        float newMagnitude = normalizedMagnitude;

        // Curvas de respuesta
        if (mode == 0) {
            newMagnitude = std::pow(normalizedMagnitude, 2.0f); // Standard / Vanilla
        } else if (mode == 1) {
            newMagnitude = normalizedMagnitude;                 // Linear (1:1)
        } else if (mode == 2) {
            newMagnitude = std::pow(normalizedMagnitude, 1.6f);  // Dynamic
        }

        float dirX = rawX / magnitude;
        float dirY = rawY / magnitude;
        return { dirX * newMagnitude, dirY * newMagnitude };
    }
};
