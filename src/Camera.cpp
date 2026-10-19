#include "Camera.h"
#include <algorithm>
#include <cmath>

namespace Glassbrick {

// Constantes do original
static constexpr double ZoomSnap = 0.01;
static constexpr double ZoomFactorMin = 1.0100001096725464; // (double)1.01f
static constexpr double RegionFriction = 0.38999998569488525; // (double)0.39f

static double clampValue(double value, double min, double max) {
    if (value < min) value = min;
    if (value > max) value = max;
    return value;
}

static int sign(double v) { return (v > 0) - (v < 0); }

Camera::Camera(SettingsManager& settings, IMagnifier& magnifier, int screenW, int screenH)
    : _settingsManager(settings), _magnifier(magnifier) {
    _zoom.factor = 1.0;
    _zoom.level = 0.0;
    _zoom.on = false;
    _zoom.saved = 1.0;
    _zoom.target = 0.0;

    _monitors = {Rect2D(0, 0, screenW, screenH)};
    _region.current = _monitors[0];
    _region.focused = _region.current;
    _region.rectangle = _region.current;
    _view = Rect2D(0.0, 0.0, _region.rectangle.width, _region.rectangle.height);

    if (settings.getSettings().CurrentRenderMode == RenderMode::Win8Fullscreen) {
        _track.snap = 0.4f;
    }
}

void Camera::setCursorProvider(CursorProvider provider) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cursorProvider = std::move(provider);
}

void Camera::setCursorWarper(CursorWarper warper) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _cursorWarper = std::move(warper);
}

// Rectangle.Contains do .NET: a borda direita/inferior não pertence ao retângulo
static bool containsPoint(const Rect2D& r, const Point2D& p) {
    return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

void Camera::setMonitors(const std::vector<Rect2D>& monitors, int primary) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (monitors.empty()) return;
    _monitors = monitors;
    _primary = (primary >= 0 && primary < static_cast<int>(monitors.size())) ? primary : 0;
    // Como SystemEvents_DisplaySettingsChanged -> ChangedScreen
    changedScreen();
}

static bool isEmptyRect(const Rect2D& r) {
    return r.width <= 0.0 && r.height <= 0.0;
}

// --------------------------------------------------------------------------
// Ações
// --------------------------------------------------------------------------

void Camera::zoomIn(float delta) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_zoom.on) {
        _zoom.on = true;
        _zoom.target = 0.0;
    }
    _zoom.target += settings().getZoomAmount() * delta;
    if (_zoom.target > 5.0) _zoom.target = 5.0;
    _flags.needsUpdate = true;
}

void Camera::zoomOut(float delta) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _zoom.target -= settings().getZoomAmount() * delta;
    if (_zoom.target < 0.0) _zoom.target = 0.0;
    _flags.needsUpdate = true;
}

void Camera::zoomOnToggle() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    zoomOnToggleLocked();
}

void Camera::zoomOnToggleLocked() {
    if (_zoom.on) {
        _zoom.saved = _zoom.target;
        _zoom.on = false;
    } else {
        _zoom.target = _zoom.saved;
        _zoom.on = true;
    }
    _flags.needsUpdate = true;
    setLastZoomTime();
}

void Camera::trackModifiersHeld() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_track.areKeysHeld) _track.areKeysPressed = true;
    _track.areKeysHeld = true;
    _flags.needsUpdate = true;
}

void Camera::trackModifiersReleased() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _track.areKeysPressed = false;
    _track.areKeysHeld = false;
}

void Camera::resetZoom() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _zoom.target = 0.0;
    _flags.needsUpdate = true;
}

void Camera::onCursorClicked() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _flags.hasCursorClicked = true;
    _flags.needsUpdate = true;
}

void Camera::onKeyDown(unsigned int keyCode, bool isAlt) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _flags.isKeyDown = true;
    if (isAlt) _flags.isControlDown = true; // no original: Keys.LMenu (Alt), apesar do nome
    // KeyCodePrevious nunca é zerado no original: repetir a mesma tecla não conta
    if (keyCode != _keyCodePrevious) {
        _flags.hasKeyPressed = true;
        _keyCodePrevious = keyCode;
    }
}

void Camera::onCharTyped() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (settings().TrackTyping) {
        _focus.caretChanged = true;
        _flags.needsUpdate = true;
    }
}

void Camera::onKeyUp() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // MoveCursorKeyUp só mexe no estado do MoveStep, que é código morto
    _track.areKeysPressed = false;
    _track.areKeysHeld = false;
}

void Camera::onGuiFocusChanged(const Rect2D& rect) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!settings().TrackFocus) return;
    if (!isEmptyRect(rect) && _focus.guiRectanglePre != rect) {
        _focus.guiRectanglePre = _focus.guiRectangle;
        _focus.guiRectangle = rect;
        _focus.guiChanged = true;
        _flags.needsUpdate = true;
    }
}

void Camera::onTextSelectionChanged(const Rect2D& rect) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!settings().TrackTyping) return;
    if (_focus.textRectanglePre != rect) {
        _focus.textRectanglePre = _focus.textRectangle;
        _focus.textRectangle = rect;
        _focus.textChanged = true;
        _focus.textKeyPressed = true;
        _flags.needsUpdate = true;
    }
}

void Camera::onCaretMoved(const Rect2D& rect) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _focus.caretLatest = rect;
    _focus.caretLatestValid = true;
    // No Windows o caret era lido na hora (GetGUIThreadInfo) quando uma tecla
    // gerava caractere. O AT-SPI avisa quando o caret anda, o que também cobre
    // setas, Home/End e cliques no texto.
    if (settings().TrackTyping) {
        _focus.caretChanged = true;
        _flags.needsUpdate = true;
    }
}

void Camera::setNeedsUpdate(bool v) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _flags.needsUpdate = v;
}

void Camera::setCursorMoved() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _flags.hasCursorMoved = true;
}

void Camera::setReadingOffset(double offset) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _track.readingOffset = static_cast<float>(offset);
    _flags.needsUpdate = true;
}

double Camera::getReadingOffset() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _track.readingOffset;
}

double Camera::getZoomTarget() { std::lock_guard<std::recursive_mutex> l(_mutex); return _zoom.target; }
double Camera::getZoomLevel() { std::lock_guard<std::recursive_mutex> l(_mutex); return _zoom.level; }
double Camera::getZoomFactor() { std::lock_guard<std::recursive_mutex> l(_mutex); return _zoom.factor; }

// --------------------------------------------------------------------------
// Passos do loop (mesma ordem de Camera.UpdateLoop no original)
// --------------------------------------------------------------------------

void Camera::autoOffStep() {
    const auto& s = settings();
    if (s.ZoomOffAuto && _zoom.on) {
        if (_zoom.hasChanged || _track.hasChanged) setLastZoomTime();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - _lastZoomTime).count();
        if (elapsed > s.ZoomOffTimeSeconds) zoomOnToggleLocked();
    }
}

void Camera::cursorMovedStep() {
    if (_cursorNow != _cursorPositionPre) {
        _flags.hasCursorMoved = true;
        _flags.needsUpdate = true;
    }
    _cursorMovedAmount.x = _cursorNow.x - _cursorPositionPre.x;
    _cursorMovedAmount.y = _cursorNow.y - _cursorPositionPre.y;
    _cursorPositionPre = _cursorNow;
}

void Camera::focusStep() {
    using Mode = FocusState::Mode;
    using Type = FocusState::Type;
    const auto& s = settings();
    const bool trackKeyboard = s.TrackFocus || s.TrackTyping;

    if (_cursorNow != _focus.cursor || _flags.hasCursorMoved || _flags.hasCursorClicked) {
        _flags.hasCursorChanged = true;
    }
    _focus.cursor = _cursorNow;

    // Troca de modo: mouse mexeu -> Mouse; tecla sem mexer o mouse -> Keyboard
    switch (_focus.mode) {
        case Mode::Inactive:
            if (_flags.hasCursorChanged) _focus.mode = Mode::Mouse;
            else if (trackKeyboard && _flags.hasKeyPressed) _focus.mode = Mode::Keyboard;
            break;
        case Mode::Mouse:
            if (!_flags.hasCursorChanged && _flags.hasKeyPressed && trackKeyboard) _focus.mode = Mode::Keyboard;
            break;
        case Mode::Keyboard:
            if (_flags.hasCursorChanged) {
                _focus.mode = Mode::Mouse;
                // Com Alt pressionado o ponteiro "pula" para onde estava o foco
                if (_flags.isControlDown && _cursorWarper) {
                    _cursorWarper(_focus.position.x, _focus.position.y);
                    _focus.cursor = _focus.position;
                    _cursorNow = _focus.position;
                    _cursorPositionPre = _focus.position;
                }
            }
            break;
        case Mode::Narrator:
            break;
    }

    switch (_focus.mode) {
        case Mode::Inactive:
            _focus.type = Type::Nothing;
            break;
        case Mode::Mouse:
            _focus.type = Type::Cursor;
            break;
        case Mode::Keyboard:
        case Mode::Narrator:
            if (_focus.guiChanged) {
                _focus.guiChanged = false;
                _focus.type = Type::Gui;
            } else if (_focus.textChanged) {
                _focus.textChanged = false;
                _focus.type = Type::Text;
            } else if (_focus.caretChanged) {
                _focus.caretChanged = false;
                const Rect2D& caret = _focus.caretLatest;
                if (_focus.caretLatestValid && caret.x >= 0.0) {
                    Rect2D r(caret.x, caret.y, std::max(0.0, caret.width), std::max(0.0, caret.height));
                    if (_focus.caretRectanglePre != r) {
                        _focus.caretRectanglePre = _focus.caretRectangle = r;
                        _focus.type = Type::Caret;
                    }
                }
            }
            break;
    }

    switch (_focus.type) {
        case Type::Cursor:
            _focus.position = _focus.cursor;
            break;
        case Type::Gui:
        case Type::Text: {
            const Rect2D& r = (_focus.type == Type::Gui) ? _focus.guiRectangle : _focus.textRectangle;
            _focus.position.x = static_cast<int>(r.x + std::min(r.width / 2.0, _view.width / 2.0));
            _focus.position.y = static_cast<int>(r.y + std::min(r.height / 2.0, _view.height / 2.0));
            break;
        }
        case Type::Caret:
            _focus.position.x = static_cast<int>(_focus.caretRectangle.x);
            _focus.position.y = static_cast<int>(_focus.caretRectangle.y);
            break;
        case Type::Nothing:
            break;
    }
}

void Camera::zoomLevelStep(double dt) {
    double level = _zoom.level;
    if (_zoom.target == 0.0) _zoom.on = false;

    auto speed = settings().getZoomSpeed();
    if (!speed.has_value()) {
        _zoom.level = _zoom.on ? _zoom.target : 0.0;
        // Observação: no original HasChanged não é atualizado neste caminho
        return;
    }

    double num = _zoom.on ? (_zoom.target - _zoom.level) : (0.0 - _zoom.level - ZoomSnap);
    if (num > ZoomSnap) num += ZoomSnap;
    else if (num < -ZoomSnap) num -= ZoomSnap;

    num = std::min(16.0 * speed.value(), std::abs(num)) * sign(num);
    _zoom.velocity = num * speed.value();
    _zoom.level += _zoom.velocity * static_cast<float>(dt);

    if (_zoom.on && std::abs(num) <= ZoomSnap * static_cast<float>(dt)) {
        _zoom.level = _zoom.target;
    } else if (_zoom.level < 0.0) {
        _zoom.level = 0.0;
        _zoom.target = 0.0;
    }
    _zoom.hasChanged = (_zoom.level != level);
}

void Camera::zoomFactorStep() {
    _zoom.factorPre = _zoom.factor;
    _zoom.factor = static_cast<float>(std::pow(2.0, _zoom.level));
    if (_zoom.factor < ZoomFactorMin) {
        _zoom.factor = (_frame.lerp > 0.0f) ? ZoomFactorMin : 1.0;
    }
}

void Camera::changedScreen() {
    // Region.Focused: monitor que contém o foco (ou o anterior, se nenhum contém)
    const Rect2D* focused = nullptr;
    for (const auto& m : _monitors) {
        if (containsPoint(m, _focus.position)) { focused = &m; break; }
    }
    if (focused) {
        _region.focused = *focused;
    } else if (std::find(_monitors.begin(), _monitors.end(), _region.focused) == _monitors.end()) {
        _region.focused = _monitors[_primary]; // o monitor anterior deixou de existir
    }
    _region.current = settings().TrackScreen ? _region.focused : _monitors[_primary];
    // Modo Fullscreen: com ClampEdge, pula uma atualização para não mostrar um salto
    if (settings().ClampEdge) _zoom.update = false;
    _region.hasChanged = true;
    _flags.needsUpdate = true;
}

void Camera::regionStep(double dt) {
    // O foco saiu do monitor atual: troca de monitor (o retângulo é animado abaixo)
    if (_monitors.size() > 1 && !containsPoint(_region.focused, _focus.position) &&
        settings().CurrentTrackModeActual.getMode() != TrackMode::NoTracking) {
        changedScreen();
    }
    if (!_region.hasChanged) return;

    const float num = 0.0625f;
    const auto trackSpeed = settings().getTrackSpeed();
    Rect2D& r = _region.rectangle;
    const Rect2D& c = _region.current;

    auto animate = [&](double& value, double targetValue, double& velocity, bool useDt) {
        if (trackSpeed.has_value()) {
            float diff = static_cast<float>(targetValue) - static_cast<float>(value);
            velocity = (velocity + diff * num) * RegionFriction;
            value += velocity * (useDt ? static_cast<float>(dt) : dt);
            if (std::round(value) == targetValue) value = targetValue;
        } else {
            value = targetValue;
        }
    };

    if (r.x != c.x) {
        if (_frame.x) {
            r.x = c.x;
            r.width = c.width;
        } else {
            animate(r.x, c.x, _region.positionVelocity.x, true);
            _frame.x = false;
        }
    }
    if (r.y != c.y) {
        if (_frame.y) {
            r.y = c.y;
            r.height = c.height;
        } else {
            animate(r.y, c.y, _region.positionVelocity.y, false);
            _frame.y = false;
        }
    }
    if (r.width != c.width) animate(r.width, c.width, _region.sizeVelocity.x, true);
    if (r.height != c.height) animate(r.height, c.height, _region.sizeVelocity.y, true);

    if (r == c) {
        _region.hasChanged = false;
        _region.positionVelocity = Vector2D();
        _region.sizeVelocity = Vector2D();
    }
}

void Camera::trackStep(double dt) {
    const auto& s = settings();
    double x = _track.position.x;
    double y = _track.position.y;
    bool flag = false;
    const TrackMode mode = s.CurrentTrackModeActual.getMode();
    const Rect2D& region = _region.rectangle;

    if (s.ClampEdge && (mode == TrackMode::Centre || mode == TrackMode::Border)) {
        if (_focus.position.x - _view.width / 2.0 < region.x - 1.0 && _track.position.x < _track.target.x)
            _track.position.x = _track.target.x;
        if (_focus.position.y - _view.height / 2.0 < region.y - 1.0 && _track.position.y < _track.target.y)
            _track.position.y = _track.target.y;
        if (_focus.position.x + _view.width / 2.0 > region.x + region.width + 1.0 && _track.target.x < _track.position.x)
            _track.position.x = _track.target.x;
        if (_focus.position.y + _view.height / 2.0 > region.y + region.height + 1.0 && _track.target.y < _track.position.y)
            _track.position.y = _track.target.y;
    }

    switch (mode) {
        case TrackMode::Centre:
            _track.target = _focus.position;
            break;

        case TrackMode::Consistent: {
            const double size = s.getCurrentTrackSize();
            int cx = static_cast<int>(_region.focused.x + _region.focused.width / 2);
            int cy = static_cast<int>(_region.focused.y + _region.focused.height / 2);
            _track.target.x = static_cast<int>(cx + ((_focus.position.x - cx) - (_focus.position.x - cx) / _zoom.factor) * size);
            _track.target.y = static_cast<int>(cy + ((_focus.position.y - cy) - (_focus.position.y - cy) / _zoom.factor) * size);
            break;
        }

        case TrackMode::Border: {
            const double size = s.getCurrentTrackSize();
            _track.border.width = _view.width - _view.width * size;
            _track.border.height = _view.height - _view.height * size;
            _track.border.x = _view.x + _view.width / 2.0 * size + region.x / _zoom.factor;
            _track.border.y = _view.y + _view.height / 2.0 * size + region.y / _zoom.factor;
            if (_focus.position.x < _track.border.x)
                _track.target.x = static_cast<int>(_focus.position.x + _view.width / 2.0 - _view.width / 2.0 * size);
            else if (_focus.position.x > _track.border.right())
                _track.target.x = static_cast<int>(_focus.position.x - _view.width / 2.0 + _view.width / 2.0 * size);
            if (_focus.position.y < _track.border.y)
                _track.target.y = static_cast<int>(_focus.position.y + _view.height / 2.0 - _view.height / 2.0 * size);
            else if (_focus.position.y > _track.border.bottom())
                _track.target.y = static_cast<int>(_focus.position.y - _view.height / 2.0 + _view.height / 2.0 * size);
            break;
        }

        case TrackMode::KeyCombo: {
            if (!_track.areKeysHeld) break;
            const double size = s.getCurrentTrackSize();
            if (_track.areKeysPressed) {
                _track.border.width = _view.width - _view.width * size;
                _track.border.height = _view.height - _view.height * size;
                _track.border.x = _view.x + _view.width / 2.0 * size + region.x / _zoom.factor;
                _track.border.y = _view.y + _view.height / 2.0 * size + region.y / _zoom.factor;
                if (_focus.position.x < _track.border.x)
                    _view.x = _focus.position.x + _view.width / 2.0 * size - region.x / _zoom.factor;
                else if (_focus.position.x > _track.border.right())
                    _view.x = _focus.position.x + _view.width - _view.width / 2.0 * size - region.x / _zoom.factor;
                if (_focus.position.y < _track.border.y)
                    _view.y = _focus.position.y + _view.height / 2.0 * size - region.y / _zoom.factor;
                else if (_focus.position.y > _track.border.bottom())
                    _view.y = _focus.position.y + _view.height - _view.height / 2.0 * size - region.y / _zoom.factor;
                _track.border.width = std::abs(_focus.position.x - _view.x);
                _track.border.height = std::abs(_focus.position.y - _view.y);
                flag = true;
            }
            if (_zoom.hasChanged || _track.areKeysPressed) {
                _track.border.width = _track.border.width / _zoom.factor * _zoom.factorPre;
                _track.border.height = _track.border.height / _zoom.factor * _zoom.factorPre;
            }
            if (_focus.position.x < _view.x + _track.border.width)
                _track.target.x = static_cast<int>(_focus.position.x + _view.width / 2.0 - _track.border.width + region.x / _zoom.factor);
            if (_focus.position.y < _view.y + _track.border.height)
                _track.target.y = static_cast<int>(_focus.position.y + _view.height / 2.0 - _track.border.height + region.y / _zoom.factor);
            if (_focus.position.x > _view.x + _track.border.width)
                _track.target.x = static_cast<int>(_focus.position.x - _track.border.width + _view.width / 2.0 + region.x / _zoom.factor);
            if (_focus.position.y > _view.y + _track.border.height)
                _track.target.y = static_cast<int>(_focus.position.y - _track.border.height + _view.height / 2.0 + region.y / _zoom.factor);
            _track.areKeysPressed = false;
            break;
        }

        case TrackMode::ScreenLocation: {
            if (mode != _trackModePre && s.TrackLocationZoom) {
                if (!_zoom.on) {
                    _zoom.on = true;
                    _zoom.target = 0.0;
                }
                _zoom.target = s.TrackLocationZoomLevel;
            }
            float tx = static_cast<float>(_region.current.x) + static_cast<float>(_region.focused.width) * s.TrackLocationX;
            float ty = static_cast<float>(_region.current.y) + static_cast<float>(_region.focused.height) * s.TrackLocationY;
            if (_track.areKeysHeld) {
                if (_track.areKeysPressed) {
                    _track.lookOffset = Point2D(0, 0);
                    _track.areKeysPressed = false;
                }
                _track.lookOffset.x += _cursorMovedAmount.x;
                _track.lookOffset.y += _cursorMovedAmount.y;
                tx += static_cast<float>(_track.lookOffset.x);
                ty += static_cast<float>(_track.lookOffset.y);
            }
            _track.target.x = static_cast<int>(tx);
            _track.target.y = static_cast<int>(ty);
            break;
        }

        case TrackMode::NoTracking:
            break;
    }
    _trackModePre = mode;

    _track.velocity = Vector2D(_track.target.x, _track.target.y) - _track.position;
    const float trackSpeed = s.getTrackSpeed().value_or(0.125f);
    const double snap = _track.snap / std::max(_zoom.factor, 0.10000000149011612);
    const double wanted = snap / 2.0 + _track.velocity.length() * trackSpeed;
    double speed = std::min(static_cast<double>(2048.0f * trackSpeed), wanted);
    if (s.TrackSpeedIndex == 4) speed = wanted;

    if (_track.velocity.length() > 0.0) _track.velocity.normalize();
    Vector2D step = _track.velocity * speed;
    if (step.length() >= snap) {
        _track.position += step * static_cast<float>(std::min(dt, 2.0));
    } else {
        _track.position.x = _track.target.x;
        _track.position.y = _track.target.y;
    }

    _track.hasChanged = !(_track.position.x == x && _track.position.y == y && !flag);
}

void Camera::viewStep() {
    _view.width = _region.rectangle.width / _zoom.factor;
    _view.height = _region.rectangle.height / _zoom.factor;
    _view.x = _track.position.x - _view.width / 2.0 - _region.rectangle.x / _zoom.factor;
    _view.y = _track.position.y - _view.height / 2.0 - _region.rectangle.y / _zoom.factor;
}

void Camera::edgeStep() {
    const auto& s = settings();
    if (!s.PushEdge || s.CurrentTrackModeActual.getMode() >= TrackMode::ScreenLocation) return;

    double left = _view.left();
    double top = _view.top();
    double right = _view.right();
    double bottom = _view.bottom();
    if (s.TrackScreen) {
        double offX = static_cast<float>(_region.rectangle.x) / _zoom.factor;
        double offY = static_cast<float>(_region.rectangle.y) / _zoom.factor;
        left += offX; right += offX;
        top += offY; bottom += offY;
    }
    const int x = _focus.position.x;
    const int y = _focus.position.y;
    // Mesma sequência do original (os 'if' não são exclusivos)
    if (x < left) {
        left = x;
        _track.target.x = static_cast<int>(left + _view.width / 2.0);
        _track.position.x = _track.target.x;
    }
    if (x > right) {
        left = x - _view.width;
        _track.target.x = static_cast<int>(left + _view.width / 2.0);
        _track.position.x = _track.target.x;
    }
    if (y < top) {
        top = y;
        _track.target.y = static_cast<int>(top + _view.height / 2.0);
        _track.position.y = _track.target.y;
    }
    if (y > bottom) {
        top = y - _view.height;
        _track.target.y = static_cast<int>(top + _view.height / 2.0);
        _track.position.y = _track.target.y;
    }
}

void Camera::clampStep(double dt) {
    const auto& s = settings();
    if (_zoom.factor < ZoomFactorMin) {
        _view.x = 0.0;
        _view.y = 0.0;
        return;
    }

    _frame.x = false;
    _frame.y = false;
    const int frame = s.AddFrame ? FrameState::FrameSize : 0;
    const float lerpStep = 1.0f / 64.0f * static_cast<float>(dt);

    if (s.ClampEdge) {
        if (_frame.lerp > 1.0f) _frame.lerp = 1.0f;
        if (_frame.lerp > 0.0f) {
            _frame.lerp -= lerpStep;
            _track.hasChanged = true;
        } else if (_frame.lerp < 0.0f) {
            _frame.lerp = 0.0f;
        }
    } else if (_zoom.target > 0.0) {
        if (_frame.lerp > 1.0f) {
            _frame.lerp = 2.0f;
        } else {
            _track.hasChanged = true;
            _frame.lerp += lerpStep;
        }
    } else {
        if (_frame.lerp > 1.0f) {
            _frame.lerp = 1.0f;
        } else {
            _track.hasChanged = true;
            _frame.lerp -= lerpStep;
        }
        if (_frame.lerp < 0.0f) _frame.lerp = 0.0f;
    }

    _frame.lerpW = static_cast<float>(_region.rectangle.width) * 0.5f * _frame.lerp;
    _frame.lerpH = static_cast<float>(_region.rectangle.height) * 0.5f * _frame.lerp;
    const Rect2D& f = _region.focused;
    const double minX = static_cast<float>(f.left()) - _frame.lerpW;
    const double minY = static_cast<float>(f.top()) - _frame.lerpH;
    const double maxX = static_cast<float>(f.right()) + _frame.lerpW;
    const double maxY = static_cast<float>(f.bottom()) + _frame.lerpH;

    if (s.TrackScreen) {
        int l = static_cast<int>(minX + (-f.left()) / _zoom.factor - frame);
        int t = static_cast<int>(minY + (-f.top()) / _zoom.factor - frame);
        int r = static_cast<int>(maxX + (-f.left()) / _zoom.factor + frame);
        int b = static_cast<int>(maxY + (-f.top()) / _zoom.factor + frame);
        if (_view.x < l) { _view.x = l; _frame.x = true; }
        else if (_view.right() > r) { _view.x = r - _view.width; _frame.x = true; }
        if (_view.y < t) { _view.y = t; _frame.y = true; }
        else if (_view.bottom() > b) { _view.y = b - _view.height; _frame.y = true; }
        return;
    }

    int l = static_cast<int>(minX - frame);
    int t = static_cast<int>(minY - frame);
    int r = static_cast<int>(maxX + frame);
    int b = static_cast<int>(maxY + frame);
    if (_view.x < l) _view.x = l;
    else if (_view.right() > r) _view.x = r - _view.width;
    if (_view.y < t) _view.y = t;
    else if (_view.bottom() > b) _view.y = b - _view.height;
    if (s.ClampEdge) {
        if (_view.width > f.width) _view.x = ((l + r) / 2) - _view.width / 2.0;
        if (_view.height > f.height) _view.y = ((t + b) / 2) - _view.height / 2.0;
    }
}

void Camera::readingOffsetStep() {
    if (_track.readingOffset == 0.0f) return;
    const auto& s = settings();
    const Rect2D& bounds = _region.current;
    double lo = bounds.x - _view.x - _view.width * sign(bounds.x);
    double hi = bounds.right() - _view.right() - _view.width * sign(bounds.x);
    if (!s.ClampEdge) {
        lo -= _view.width;
        hi += _view.width;
    }
    if (s.AddFrame) {
        lo -= 2.0;
        hi += 2.0;
    }
    _track.readingOffset = static_cast<float>(clampValue(_track.readingOffset, lo, hi));
    _flags.needsUpdate = true;
    _view.x += _track.readingOffset;
}

void Camera::updateStep() {
    if (_zoom.update) {
        // Modo Fullscreen: a vista é sempre em coordenadas absolutas de tela
        _magnifier.update(static_cast<float>(_zoom.factor), static_cast<float>(_view.x), static_cast<float>(_view.y));
    }
    _zoom.update = true;
    _flags.hasCursorMoved = false;
}

void Camera::updateLoop(double dt) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_cursorProvider) _cursorNow = _cursorProvider();

    // MoveStep (ponteiro pelo teclado) ainda não portado
    cursorMovedStep();
    autoOffStep();
    if (_flags.needsUpdate) {
        focusStep();
        zoomLevelStep(dt);
        zoomFactorStep();
        regionStep(dt);
        trackStep(dt);
        viewStep();
        edgeStep();
        clampStep(dt);
        readingOffsetStep();
        updateStep();
        _flags.reset();
        if (_zoom.hasChanged || _track.hasChanged || _region.hasChanged) {
            _flags.needsUpdate = true;
        }
    }
}

} // namespace Glassbrick
