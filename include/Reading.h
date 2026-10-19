#pragma once

#include "GlassbrickTypes.h"
#include "GlassbrickSettings.h"
#include "IMagnifier.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace Glassbrick {

class Camera;

// Modo leitura (equivalente a Reading.cs).
//
// Escurece a tela deixando uma faixa de duas linhas acima do cursor, desenha
// linhas-guia abaixo dela e permite navegar pelo texto com o teclado:
//   W / S          sobe / desce 2 px (com Shift: uma linha inteira)
//   F              próxima linha
//   A / D          inicia / acelera rolagem horizontal (pan) p/ esquerda/direita
//   Q / E          diminui / aumenta a altura da linha
//   Esc ou clique  desliga
//
// Threads: onKeyDown/onKeyUp/onMouseClick/activateToggle vêm da thread de
// entrada; updateLoop() roda na thread do UpdateLoop, que também é a que lê e
// move o ponteiro (os movimentos feitos aqui não contam como "mouse movido",
// como acontece com SetCursorPos no Windows).
class Reading {
public:
    using CursorProvider = std::function<Point2D()>;
    using CursorWarper = std::function<void(int, int)>;

    Reading(Camera& camera, const SettingsManager& settings, IMagnifier& magnifier, int screenW, int screenH);

    void setCursorFunctions(CursorProvider provider, CursorWarper warper);
    // Monitores da tela virtual (a janela de leitura fica no monitor do cursor)
    void setMonitors(const std::vector<Rect2D>& monitors, int primary);

    void activateToggle();
    bool isActive() const { return _activeAtomic; }

    // Teclas capturadas enquanto ativo (ver InputManager). keySym é um KeySym X11.
    void onKeyDown(unsigned long keySym, bool shiftHeld);
    void onKeyUp(unsigned long keySym);
    void onOtherKey();   // qualquer outra tecla (não modificadora) interrompe o pan
    void onMouseClick();

    void updateLoop(double dt);

private:
    struct Flags {
        bool speedPlus = false, speedMinus = false;
        bool linePlus = false, lineMinus = false;
        bool lineUp = false, lineDown = false;
        bool moveUp = false, moveDown = false;
        bool mouseMoved = false;
        bool returnFlag = false;   // Flag.Return: volta o ReadingOffset para 0
        bool pan = false;
        bool shiftKey = false;
        bool newLine = false;
        bool zoomChanged = false;
    };

    void turnOff();
    void setActive(bool v) { _active = v; _activeAtomic = v; }
    void turnOnTick();
    void setWindow();
    void turnOffTick();
    void updateFocusPosition();
    void onMouseMove();
    void showLines();
    void moveCursorY(int dy);
    void logicStep(double dt);
    void publishOverlay();
    void setLineHeight(int h) { _lineHeight = std::clamp(h, 8, 96); }

    Camera& _camera;
    const SettingsManager& _settingsManager;
    IMagnifier& _magnifier;
    CursorProvider _cursorProvider;
    CursorWarper _cursorWarper;
    std::recursive_mutex _mutex;

    int _screenX = 0, _screenY = 0, _screenW = 0, _screenH = 0;  // área da janela Reading
    std::vector<Rect2D> _monitors;
    int _primary = 0;

    bool _active = false;
    std::atomic<bool> _activeAtomic{false};
    bool _visible = false;          // Control.Visible
    bool _turnOnTimer = false;
    bool _turnOffTimer = false;
    float _opacity = 0.0f;

    // Cor de fundo (transição para branco quando as cores estão invertidas)
    bool _inverted = false;
    bool _invertColour = false;
    float _invertAmount = 0.0f;
    float _backColour[3] = {0.0f, 0.0f, 0.0f};
    float _backColourPre[3] = {0.0f, 0.0f, 0.0f};
    int _colourModePre = 0;

    // FocusSettings
    int _focusX = 0, _focusY = 0, _focusWidth = 0, _focusHeight = 0;
    float _speed = 0.0f;
    float _throttle = 0.0f;
    int _speedTarget = 0;

    // Line
    int _lineHeight = 22;
    int _lineOffset = 0;
    static constexpr int LineOpacity = 128;

    // Linhas-guia: desenhadas ao navegar, apagadas quando o mouse se move
    bool _linesVisible = false;
    float _linesBaseY = 0.0f;

    Point2D _cursor;
    bool _cursorKnown = false;
    Flags _flag;
};

} // namespace Glassbrick
