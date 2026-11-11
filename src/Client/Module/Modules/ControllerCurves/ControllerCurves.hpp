#pragma once

#include "src/Client/Module/IModule.hpp"
#include <cmath>
#include <utility>
#include <string>
#include <algorithm>

enum class ResponseCurveType {
    Standard = 0,
    Linear = 1,
    Dynamic = 2
};

class ControllerCurves : public IModule {
public:
    // Module Settings
    int curveMode = 1;           // 0: Standard, 1: Linear, 2: Dynamic
    float minDeadzone = 0.05f;   // Inner Deadzone (0.00 - 0.25)
    float maxDeadzone = 0.95f;   // Outer Deadzone (0.75 - 1.00)
    bool rawInput = true;        // Bypass OS input smoothing

    ControllerCurves();

    void onEnable() override;
    void onDisable() override;

    // Mathematical curve and deadzone processing
    static std::pair<float, float> processStick(float rawX, float rawY, int mode, float minDZ, float maxDZ);
};
