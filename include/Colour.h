#pragma once

#include "GlassbrickTypes.h"
#include "GlassbrickSettings.h"
#include "IMagnifier.h"
#include <mutex>
#include <random>

namespace Glassbrick {

// Motor de Cores e Acessibilidade (equivalente a Colour.cs)
class Colour {
private:
    const SettingsManager& _settingsManager;
    IMagnifier& _magnifier;

    bool _colourChanged = true;
    static constexpr float ColourFric = 0.3f;

    int _randomTimer = 0;
    double _deltaTime = 1.0;
    int _randomPre = 0;
    std::mt19937 _rng{1337};

    Matrix _target;
    Matrix _current;
    Matrix _speed;

    // changeMatrixTargets() vem da thread de entrada, colourUpdateStep() do loop
    std::recursive_mutex _mutex;

    static float lerp(float value1, float value2, float amount) {
        return value1 + (value2 - value1) * amount;
    }

    void setDefault();
    void setInvert(InvertMode type, float sat);
    void setAdjust(AdjustMode type, float amount, bool sim);
    void setTint(TintColour tint, float amount);
    void setContrast(ContrastMode type, float amount, bool invert);

public:
    Colour(const SettingsManager& settings, IMagnifier& magnifier);

    void changeMatrixTargets();
    void colourUpdateStep(double dt);

    void notifyChanged() {
        _colourChanged = true;
    }

    const Matrix& getCurrentMatrix() const { return _current; }
    const Matrix& getTargetMatrix() const { return _target; }
};

} // namespace Glassbrick
