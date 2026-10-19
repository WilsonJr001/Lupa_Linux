#include "Reading.h"
#include "Camera.h"
#include <X11/keysym.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace Glassbrick {

static constexpr float TargetOpacity = 0.33f;

static int sign(double v) { return (v > 0) - (v < 0); }

// GLASSBRICK_DEBUG=1 imprime os eventos do modo leitura
static bool debugEnabled() {
    static const bool enabled = std::getenv("GLASSBRICK_DEBUG") != nullptr;
    return enabled;
}
#define READING_LOG(msg) do { if (debugEnabled()) std::cerr << "[Reading] " << msg << "\n"; } while (0)

Reading::Reading(Camera& camera, const SettingsManager& settings, IMagnifier& magnifier, int screenW, int screenH)
    : _camera(camera), _settingsManager(settings), _magnifier(magnifier), _screenW(screenW), _screenH(screenH) {
    _colourModePre = static_cast<int>(settings.getSettings().CurrentColourModeActual.getMode());
    _focusWidth = screenW;
    _focusX = _screenX;
}

void Reading::setMonitors(const std::vector<Rect2D>& monitors, int primary) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _monitors = monitors;
    _primary = (primary >= 0 && primary < static_cast<int>(monitors.size())) ? primary : 0;
}

// Reading.SetWindow: a janela ocupa o monitor onde está o cursor
// (Screen.FromPoint, que devolve o mais próximo se o ponto estiver fora de todos)
void Reading::setWindow() {
    if (_monitors.empty()) return;
    const Rect2D* best = &_monitors[_primary];
    double bestDist = 1e18;
    for (const auto& m : _monitors) {
        const double dx = std::max({m.x - _cursor.x, 0.0, _cursor.x - (m.x + m.width - 1)});
        const double dy = std::max({m.y - _cursor.y, 0.0, _cursor.y - (m.y + m.height - 1)});
        if (dx * dx + dy * dy < bestDist) {
            bestDist = dx * dx + dy * dy;
            best = &m;
        }
    }
    _screenX = static_cast<int>(best->x);
    _screenY = static_cast<int>(best->y);
    _screenW = static_cast<int>(best->width);
    _screenH = static_cast<int>(best->height);
    // FocusPanel: largura da tela, a partir da borda esquerda da janela
    _focusX = _screenX;
    _focusWidth = _screenW;
}

void Reading::setCursorFunctions(CursorProvider provider, CursorWarper warper) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cursorProvider = std::move(provider);
    _cursorWarper = std::move(warper);
}

// --------------------------------------------------------------------------
// Eventos (thread de entrada)
// --------------------------------------------------------------------------

void Reading::activateToggle() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_active) {
        setActive(false);
        _turnOffTimer = true;
        _turnOnTimer = false;
    } else {
        setActive(true);
        _turnOnTimer = true;
        _turnOffTimer = false;
    }
}

void Reading::turnOff() {
    setActive(false);
    _turnOffTimer = true;
    _turnOnTimer = false;
}

void Reading::onMouseClick() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_active) turnOff();
}

void Reading::onKeyDown(unsigned long keySym, bool shiftHeld) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Original: também ignora quando Camera.MoveActive (MoveStep ainda não portado)
    READING_LOG("tecla " << keySym << (shiftHeld ? " +shift" : "") << (_active ? "" : " (ignorada: inativo)"));
    if (!_active) return;
    _flag.shiftKey = shiftHeld;
    switch (keySym) {
        case XK_Escape: _turnOffTimer = true; break;
        case XK_a: _flag.speedMinus = true; break;
        case XK_d: _flag.speedPlus = true; break;
        case XK_e: _flag.linePlus = true; break;
        case XK_f: _flag.lineDown = true; break;
        case XK_q: _flag.lineMinus = true; break;
        case XK_s: _flag.moveDown = true; break;
        case XK_w: _flag.moveUp = true; break;
        default: _flag.pan = false; break;
    }
}

void Reading::onOtherKey() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_active) _flag.pan = false;
}

void Reading::onKeyUp(unsigned long keySym) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (keySym == XK_f) _flag.lineDown = false;
    else if (keySym == XK_r) _flag.lineUp = false;
}

// --------------------------------------------------------------------------
// Loop (thread do UpdateLoop)
// --------------------------------------------------------------------------

void Reading::updateFocusPosition() {
    _focusHeight = static_cast<int>((_lineHeight * 2) * std::pow(0.8, _camera.getZoomLevel()));
    _focusY = _cursor.y - _focusHeight - _screenY;
}

void Reading::onMouseMove() {
    // HookManager_MouseMove: Refresh() repinta sem as linhas-guia
    READING_LOG("mouse movido para " << _cursor.x << "," << _cursor.y);
    updateFocusPosition();
    _flag.mouseMoved = true;
    _linesVisible = false;
}

void Reading::showLines() {
    // Flag.DrawLines + Invalidate(): as linhas ficam na posição do momento do desenho
    _linesVisible = true;
    _linesBaseY = static_cast<float>(_focusY + _focusHeight);
    READING_LOG("linhas-guia a partir de y=" << _linesBaseY << " altura " << _lineHeight);
}

void Reading::moveCursorY(int dy) {
    _cursor.y += dy;
    if (_cursorWarper) _cursorWarper(_cursor.x, _cursor.y);
}

void Reading::turnOnTick() {
    if (_visible) {
        _opacity += 0.01f + (TargetOpacity - _opacity) * 0.01f;
        if (_opacity >= TargetOpacity) {
            _opacity = TargetOpacity;
            _turnOnTimer = false;
        }
    } else {
        setActive(true);
        _camera.setCursorMoved();
        setWindow();
        _visible = true;
        updateFocusPosition();
        _colourModePre = 0; // força recalcular a cor de fundo
    }
}

void Reading::turnOffTick() {
    setActive(false);
    if (!_visible) return;
    _flag.returnFlag = true;
    _opacity = std::max(0.0f, _opacity - (0.01f + _opacity * 0.01f));
    if (_opacity <= 0.0f && _speed == 0.0f) {
        _opacity = 0.0f;
        _turnOffTimer = false;
        _visible = false;
        _linesVisible = false;
    }
}

void Reading::updateLoop(double dt) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (_cursorProvider) {
        Point2D cur = _cursorProvider();
        if (_cursorKnown && cur != _cursor && _active) {
            _cursor = cur;
            onMouseMove();
        }
        _cursor = cur;
        _cursorKnown = true;
    }

    // TurnOnTimer / TurnOffTimer (no original, timers de ~1 tick por quadro)
    if (_turnOnTimer) turnOnTick();
    if (_turnOffTimer) turnOffTick();

    // UpdateLoop.cs só chama Reading.UpdateLoop quando a janela está visível
    if (_visible) logicStep(dt);

    publishOverlay();
}

void Reading::logicStep(double dt) {
    const auto& s = _settingsManager.getSettings();

    if (_active) {
        updateFocusPosition();

        if (_camera.getZoomLevel() != _camera.getZoomTarget()) {
            _flag.zoomChanged = true;
        } else if (_flag.zoomChanged) {
            showLines();
            _flag.zoomChanged = false;
        }

        const ColourMode mode = s.CurrentColourModeActual.getMode();
        if (static_cast<int>(mode) != _colourModePre) {
            _inverted = mode == ColourMode::Invert || (mode == ColourMode::Contrast && s.ColourContrastInvert);
            std::copy(_backColour, _backColour + 3, _backColourPre);
            _colourModePre = static_cast<int>(mode);
            _invertAmount = 0.0f;
            _invertColour = true;
        }
        if (_invertColour) {
            _invertAmount += 0.025f;
            if (_invertAmount >= 1.0f) {
                _invertAmount = 1.0f;
                _invertColour = false;
            }
            // Fundo branco quando invertido: depois da inversão ele fica escuro
            const float target = _inverted ? 1.0f : 0.0f;
            for (int i = 0; i < 3; ++i) {
                _backColour[i] = target * _invertAmount + _backColourPre[i] * (1.0f - _invertAmount);
            }
            _linesVisible = false; // mudar BackColor repinta o formulário
        }

        if (_flag.lineDown) {
            if (!_flag.newLine) {
                _flag.newLine = true;
                _flag.returnFlag = true;
                _lineOffset = _lineHeight;
            }
            _flag.lineDown = false;
        } else if (_flag.lineUp) {
            if (!_flag.newLine) {
                _flag.newLine = true;
                _flag.returnFlag = true;
                _lineOffset = -_lineHeight;
            }
            _flag.lineUp = false;
        }

        if (_flag.newLine) {
            if (std::abs(_lineOffset) > 1) {
                moveCursorY(2 * sign(_lineOffset));
                _lineOffset -= 2 * sign(_lineOffset);
            } else {
                _lineOffset = 0;
                _flag.newLine = false;
                showLines();
            }
        }

        if (_flag.linePlus) {
            setLineHeight(_lineHeight + 1);
            _flag.linePlus = false;
            showLines();
        } else if (_flag.lineMinus) {
            setLineHeight(_lineHeight - 1);
            _flag.lineMinus = false;
            showLines();
        }
    }

    if (_flag.returnFlag) {
        // Volta suavemente o deslocamento horizontal (pan) para zero
        const double readingOffset = _camera.getReadingOffset();
        bool done = false;
        if (_throttle <= 1.0f) _throttle += 0.02f;
        else _throttle = 1.0f;

        if (std::abs(readingOffset) < 1.0) {
            _camera.setReadingOffset(0.0);
            done = true;
        } else {
            _camera.setReadingOffset(readingOffset - (readingOffset * (_throttle * 0.5f) + sign(readingOffset)) * dt);
        }
        if (_flag.speedPlus || _flag.speedMinus) done = true;
        if (done) {
            _flag.returnFlag = false;
            _speed = 0.0f;
            _throttle = 0.0f;
        }
        return;
    }

    if (!_active) return;

    if (_flag.speedPlus) {
        if (_flag.pan) {
            _speedTarget = std::min(4, _speedTarget + 1);
        } else {
            if (_speedTarget <= 0) _speedTarget = 1;
            _flag.pan = true;
        }
        _flag.speedPlus = false;
    } else if (_flag.speedMinus) {
        if (_flag.pan) {
            _speedTarget = std::max(-4, _speedTarget - 1);
            if (_speedTarget == 0) _flag.pan = false;
        } else {
            if (_speedTarget >= 0) _speedTarget = -1;
            _flag.pan = true;
        }
        _flag.speedMinus = false;
    } else if (_flag.moveDown || _flag.moveUp) {
        const bool down = _flag.moveDown;
        if (_flag.pan) {
            _flag.pan = false;
        } else if (_camera.getReadingOffset() == 0.0) {
            if (_flag.shiftKey) {
                (down ? _flag.lineDown : _flag.lineUp) = true;
            } else {
                moveCursorY(down ? 2 : -2);
                updateFocusPosition();
                showLines();
            }
        } else {
            _flag.returnFlag = true;
            _lineOffset = down ? _lineHeight : -_lineHeight;
        }
        _flag.moveDown = false;
        _flag.moveUp = false;
    } else if (_flag.mouseMoved) {
        if (_flag.pan) {
            _flag.pan = false;
        } else if (_camera.getReadingOffset() != 0.0) {
            _flag.returnFlag = true;
        }
        _lineOffset = _lineHeight;
        _flag.mouseMoved = false;
    }

    const float wanted = _flag.pan ? static_cast<float>(_speedTarget) : 0.0f;
    const float diff = wanted - _speed;
    if (std::abs(diff) > 0.2f) _speed += 0.2f * sign(diff);
    else _speed = wanted;

    const double step = std::pow(2.0, std::abs(_speed)) * sign(_speed);
    // O original soma sempre (inclusive 0); só chamamos quando há movimento,
    // para não forçar a Camera a recalcular todo quadro.
    if (step != 0.0) {
        _camera.setReadingOffset(_camera.getReadingOffset() + step / (_camera.getZoomFactor() * 0.5 + 0.5));
    }
    _flag.mouseMoved = false;
}

void Reading::publishOverlay() {
    ReadingOverlay o;
    o.visible = _visible;
    o.opacity = _opacity;
    std::copy(_backColour, _backColour + 3, o.back);
    o.areaX = _screenX;
    o.areaY = _screenY;
    o.areaWidth = _screenW;
    o.areaHeight = _screenH;
    // _focusY e as linhas são relativos ao topo da janela, como no original
    o.bandX = _focusX;
    o.bandY = _focusY + _screenY;
    o.bandWidth = _focusWidth;
    o.bandHeight = _focusHeight;
    o.linesVisible = _linesVisible;
    o.linesBaseY = _linesBaseY + static_cast<float>(_screenY);
    o.lineHeight = _lineHeight;
    o.lineOpacity = LineOpacity;
    const float lc = _inverted ? 0.0f : 1.0f;
    o.lineColour[0] = o.lineColour[1] = o.lineColour[2] = lc;
    o.doubleLines = _camera.getZoomLevel() < 1.0;
    _magnifier.setReadingOverlay(o);
}

} // namespace Glassbrick
