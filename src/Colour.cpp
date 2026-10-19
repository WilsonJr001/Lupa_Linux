#include "Colour.h"
#include <cmath>

namespace LupaLinux {

Colour::Colour(const SettingsManager& settings, IMagnifier& magnifier)
    : _settingsManager(settings),
      _magnifier(magnifier),
      _target(4, 3, [](int i, int j) { return (i == j) ? 1.0f : 0.0f; }),
      _current(4, 3, [](int i, int j) { return (i == j) ? 1.0f : 0.0f; }),
      _speed(4, 3, [](int, int) { return 0.0f; }) {
    changeMatrixTargets();
}

void Colour::setDefault() {
    _target = Matrix(4, 3, [](int i, int j) { return (i == j) ? 1.0f : 0.0f; });
}

void Colour::setInvert(InvertMode type, float sat) {
    switch (type) {
        case InvertMode::Brightness: {
            float num = lerp(-0.25f, 0.5f, sat);
            float num2 = lerp(-0.25f, -0.75f, sat);
            float num3 = lerp(0.75f, 1.0f, sat);
            _target = Matrix(4, 3, {
                num,  num2, num2,
                num2, num,  num2,
                num2, num2, num,
                num3, num3, num3
            });
            break;
        }
        case InvertMode::Colour: {
            float num = lerp(-0.25f, -1.0f, sat);
            float num2 = lerp(-0.25f, 0.0f, sat);
            float num3 = lerp(0.75f, 1.0f, sat);
            _target = Matrix(4, 3, {
                num,  num2, num2,
                num2, num,  num2,
                num2, num2, num,
                num3, num3, num3
            });
            break;
        }
    }
}

void Colour::setAdjust(AdjustMode type, float amount, bool sim) {
    if (!sim) {
        amount = -amount;
    }
    switch (type) {
        case AdjustMode::Deuteranomaly:
            _target = Matrix(4, 3, {
                lerp(1.0f, 0.4f, amount), 0.35f * amount,           -0.1f * amount,
                0.6f * amount,           lerp(1.0f, 0.6f, amount),  0.2f * amount,
                0.0f,                    0.05f * amount,            lerp(1.0f, 0.9f, amount),
                0.0f,                    0.0f,                      0.0f
            });
            break;
        case AdjustMode::Protanomaly:
            _target = Matrix(4, 3, {
                lerp(1.0f, 0.1f, amount), 0.1f * amount,            0.05f * amount,
                0.95f * amount,          lerp(1.0f, 0.8f, amount),  -0.05f * amount,
                -0.05f * amount,         0.1f * amount,             lerp(1.0f, 1.0f, amount),
                0.0f,                    0.0f,                      0.0f
            });
            break;
        case AdjustMode::Tritanomaly:
            _target = Matrix(4, 3, {
                lerp(1.0f, 0.85f, amount), 0.0f,                     -0.05f * amount,
                0.0f,                     lerp(1.0f, 0.85f, amount), 0.95f * amount,
                0.15f * amount,           0.15f * amount,            lerp(1.0f, 0.1f, amount),
                0.0f,                     0.0f,                      0.0f
            });
            break;
        case AdjustMode::Monochromacy:
            _target = Matrix(4, 3, {
                lerp(1.0f, 0.309f, amount), 0.309f * amount,            0.309f * amount,
                0.609f * amount,           lerp(1.0f, 0.609f, amount), 0.609f * amount,
                0.082f * amount,           0.082f * amount,            lerp(1.0f, 0.082f, amount),
                0.0f,                      0.0f,                       0.0f
            });
            break;
    }
}

void Colour::setTint(TintColour tint, float amount) {
    float num = 0.0f;
    float num2 = 0.309f;
    float num3 = 0.609f;
    float num4 = 0.082f;
    float num5 = 0.5f;
    float num6 = num2 * num5;
    float num7 = num3 * num5;
    float num8 = num4 * num5;
    float num9 = lerp(1.0f, num2, amount);
    float num10 = lerp(1.0f, num3, amount);
    float num11 = lerp(1.0f, num4, amount);
    float num12 = lerp(1.0f, num6, amount);
    float num13 = lerp(1.0f, num7, amount);
    float num14 = lerp(1.0f, num8, amount);
    float num15 = num2 * amount;
    float num16 = num3 * amount;
    float num17 = num4 * amount;
    float num18 = num6 * amount;
    float num19 = num7 * amount;
    float num20 = num8 * amount;
    float num21 = lerp(1.0f, num, amount);
    float num22 = num * amount;
    int tintIdx = static_cast<int>(tint);

    const auto& s = _settingsManager.getSettings();
    if (s.ColourTintParty) {
        switch (s.ColourTransitionIndex) {
            case 0: _randomTimer = static_cast<int>(256.0 / _deltaTime); break;
            case 1: _randomTimer = static_cast<int>(128.0 / _deltaTime); break;
            case 2: _randomTimer = static_cast<int>(64.0 / _deltaTime); break;
            case 3: _randomTimer = static_cast<int>(32.0 / _deltaTime); break;
            case 4: _randomTimer = static_cast<int>(16.0 / _deltaTime); break;
            default: _randomTimer = 64; break;
        }
        std::uniform_int_distribution<int> dist(0, 12);
        do {
            tintIdx = dist(_rng);
        } while (std::abs(tintIdx - _randomPre) < 1 || tintIdx == 12);
        _randomPre = tintIdx;
    }

    switch (static_cast<TintColour>(tintIdx)) {
        case TintColour::Orange:
            _target = Matrix(4, 3, {
                num9,  num18, num22,
                num16, num13, num22,
                num17, num20, num21,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Yellow:
            _target = Matrix(4, 3, {
                num9,  num15, num22,
                num16, num10, num22,
                num17, num17, num21,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Lime:
            _target = Matrix(4, 3, {
                num12, num15, num22,
                num19, num10, num22,
                num20, num17, num21,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Green:
            _target = Matrix(4, 3, {
                num21, num15, num22,
                num22, num10, num22,
                num22, num17, num21,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Emerald:
            _target = Matrix(4, 3, {
                num21, num15, num18,
                num22, num10, num19,
                num22, num17, num14,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Cyan:
            _target = Matrix(4, 3, {
                num21, num15, num15,
                num22, num10, num16,
                num22, num17, num11,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Azure:
            _target = Matrix(4, 3, {
                num21, num18, num15,
                num22, num13, num16,
                num22, num20, num11,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Blue:
            _target = Matrix(4, 3, {
                num21, num22, num15,
                num22, num21, num16,
                num22, num22, num11,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Purple:
            _target = Matrix(4, 3, {
                num12, num22, num15,
                num19, num21, num16,
                num20, num22, num11,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Magenta:
            _target = Matrix(4, 3, {
                num9,  num22, num15,
                num16, num21, num16,
                num17, num22, num11,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Rose:
            _target = Matrix(4, 3, {
                num9,  num22, num18,
                num16, num21, num19,
                num17, num22, num14,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Red:
            _target = Matrix(4, 3, {
                num9,  num22, num22,
                num16, num21, num22,
                num17, num22, num21,
                0.0f,  0.0f,  0.0f
            });
            break;
        case TintColour::Black:
            _target = Matrix(4, 3, {
                num9,  num22, num22,
                num22, num9,  num22,
                num22, num22, num9,
                0.0f,  0.0f,  0.0f
            });
            break;
    }
}

void Colour::setContrast(ContrastMode type, float amount, bool invert) {
    switch (type) {
        case ContrastMode::FullColour:
            if (!invert) {
                float num13 = lerp(1.0f, 1.35f, amount);
                float num14 = lerp(0.0f, 0.325f, amount);
                float num15 = lerp(0.0f, -0.5f, amount);
                _target = Matrix(4, 3, {
                    num13, num14, num14,
                    num14, num13, num14,
                    num14, num14, num13,
                    num15, num15, num15
                });
            } else {
                float num16 = lerp(0.75f, -0.25f, amount);
                float num17 = lerp(-0.875f, -0.875f, amount);
                float num18 = lerp(1.0f, 1.5f, amount);
                _target = Matrix(4, 3, {
                    num16, num17, num17,
                    num17, num16, num17,
                    num17, num17, num16,
                    num18, num18, num18
                });
            }
            break;

        case ContrastMode::BlackAndWhite:
            if (!invert) {
                float num41 = lerp(1.0f, 0.375f, amount);
                float num42 = lerp(0.0f, 0.375f, amount);
                float num43 = lerp(0.0f, 0.0f, amount);
                _target = Matrix(4, 3, {
                    num41, num42, num42,
                    num42, num41, num42,
                    num42, num42, num41,
                    num43, num43, num43
                });
            } else {
                float num44 = lerp(0.5f, -0.375f, amount);
                float num45 = lerp(-0.75f, -0.375f, amount);
                float num46 = lerp(1.0f, 1.0f, amount);
                _target = Matrix(4, 3, {
                    num44, num45, num45,
                    num45, num44, num45,
                    num45, num45, num44,
                    num46, num46, num46
                });
            }
            break;

        case ContrastMode::BlackAndYellow:
            if (!invert) {
                float num19 = lerp(1.0f, 0.375f, amount);
                float num20 = lerp(1.0f, -0.375f, amount);
                float num21 = lerp(0.0f, 0.375f, amount);
                float num22 = lerp(0.0f, -0.375f, amount);
                float num23 = lerp(0.0f, 0.0f, amount);
                float num24 = 0.0f;
                _target = Matrix(4, 3, {
                    num19, num21, num22,
                    num21, num19, num22,
                    num21, num21, num20,
                    num23, num23, num24
                });
            } else {
                float num25 = lerp(0.5f, -0.375f, amount);
                float num26 = lerp(-0.75f, -0.375f, amount);
                float num27 = lerp(1.0f, 1.0f, amount);
                float num28 = lerp(1.0f, 0.0f, amount);
                _target = Matrix(4, 3, {
                    num25, num26, num26,
                    num26, num25, num26,
                    num26, num26, num25,
                    num27, num27, num28
                });
            }
            break;

        case ContrastMode::BlueAndYellow:
            if (!invert) {
                float num29 = lerp(1.0f, 0.375f, amount);
                float num30 = lerp(1.0f, -0.375f, amount);
                float num31 = lerp(0.0f, 0.375f, amount);
                float num32 = lerp(0.0f, -0.375f, amount);
                float num33 = lerp(0.0f, 0.0f, amount);
                float num34 = lerp(0.0f, 1.1f, amount);
                _target = Matrix(4, 3, {
                    num29, num31, num32,
                    num31, num29, num32,
                    num31, num31, num30,
                    num33, num33, num34
                });
            } else {
                float num35 = lerp(0.5f, -0.375f, amount);
                float num36 = lerp(0.5f, 0.375f, amount);
                float num37 = lerp(-0.75f, -0.375f, amount);
                float num38 = lerp(-0.75f, 0.375f, amount);
                float num39 = lerp(1.0f, 1.0f, amount);
                float num40 = lerp(1.0f, 0.0f, amount);
                _target = Matrix(4, 3, {
                    num35, num37, num38,
                    num37, num35, num38,
                    num37, num37, num36,
                    num39, num39, num40
                });
            }
            break;

        case ContrastMode::BlueAndWhite:
            if (!invert) {
                float num = lerp(1.0f, 0.375f, amount);
                float num2 = lerp(1.0f, 0.375f, amount);
                float num3 = lerp(0.0f, 0.375f, amount);
                float num4 = lerp(0.0f, 0.375f, amount);
                float num5 = lerp(0.0f, 0.0f, amount);
                float num6 = lerp(0.0f, 1.0f, amount);
                _target = Matrix(4, 3, {
                    num,  num3, num4,
                    num3, num,  num4,
                    num3, num3, num2,
                    num5, num5, num6
                });
            } else {
                float num7 = lerp(0.5f, -0.375f, amount);
                float num8 = lerp(0.5f, 0.0f, amount);
                float num9 = lerp(-0.75f, -0.375f, amount);
                float num10 = lerp(-0.75f, 0.0f, amount);
                float num11 = lerp(1.0f, 1.0f, amount);
                float num12 = lerp(1.0f, 1.0f, amount);
                _target = Matrix(4, 3, {
                    num7,  num9,  num10,
                    num9,  num7,  num10,
                    num9,  num9,  num8,
                    num11, num11, num12
                });
            }
            break;
    }
}

void Colour::changeMatrixTargets() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _colourChanged = true;
    const auto& s = _settingsManager.getSettings();
    switch (s.CurrentColourModeActual.getMode()) {
        case ColourMode::Invert:
            setInvert(s.ColourInvertType, s.ColourInvertSaturation);
            break;
        case ColourMode::Adjust:
            setAdjust(s.ColourAdjustType, s.ColourAdjustAmount, s.ColourAdjustSimulate);
            break;
        case ColourMode::Tint:
            setTint(s.ColourTint, s.ColourTintAmount);
            break;
        case ColourMode::Contrast:
            setContrast(s.ColourContrast, s.ColourContrastAmount, s.ColourContrastInvert);
            break;
        default:
            setDefault();
            break;
    }
}

void Colour::colourUpdateStep(double dt) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _deltaTime = dt;
    if (!_colourChanged) return;

    const auto& s = _settingsManager.getSettings();
    auto transitionOpt = s.getColourTransition();

    if (!transitionOpt.has_value()) {
        _current = _target;
    } else {
        float num = transitionOpt.value() * static_cast<float>(dt);
        _speed = 0.3f * (_speed + num * (_target - _current));
        _current += _speed;
    }

    _magnifier.updateColour(_current.toColourArray());

    if (s.CurrentColourModeActual.getMode() == ColourMode::Tint && s.ColourTintParty) {
        if (_randomTimer > 0) {
            _randomTimer--;
            return;
        }
        _randomTimer = 0;
        changeMatrixTargets();
    } else {
        if (std::abs(_target(0, 0) - _current(0, 0)) < 0.01f &&
            std::abs(_target(1, 1) - _current(1, 1)) < 0.01f &&
            std::abs(_target(2, 2) - _current(2, 2)) < 0.01f) {
            _current = _target;
            _speed = Matrix(4, 3, [](int, int) { return 0.0f; });
            // O original continua enviando a matriz todo quadro; aqui paramos
            // após enviar o valor final exato.
            _magnifier.updateColour(_current.toColourArray());
            _colourChanged = false;
        }
    }
}

} // namespace LupaLinux
