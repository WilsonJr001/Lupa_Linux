#pragma once

#include "LupaTypes.h"
#include "LupaSettings.h"
#include "IMagnifier.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace LupaLinux {

// Motor de câmera (equivalente a Camera.cs).
//
// Porte fiel dos passos do original: Zoom, Region, Track (todos os modos),
// View, Edge, Clamp (com Frame.Lerp), ReadingOffset e AutoOff.
//
// FocusStep (modos Mouse/Teclado) também está portado; os eventos de foco
// vêm do FocusTracker (AT-SPI, no lugar do UI Automation) e as teclas do
// InputManager (XInput2, no lugar dos hooks de baixo nível).
//
// Não portado:
//  - MoveStep / MoveCursorKeyDown: código morto no original (MoveCursorKeyDown
//    nunca é chamado, então Move.Active nunca fica verdadeiro);
//  - modo janela (RenderMode.Windowed: _magnifier.ChangeScreen).
//
// Os métodos públicos são chamados pela thread de entrada e updateLoop() pela
// thread do UpdateLoop, por isso tudo é protegido por um mutex.
class Camera {
public:
    using CursorProvider = std::function<Point2D()>;
    using CursorWarper = std::function<void(int, int)>;

    Camera(SettingsManager& settings, IMagnifier& magnifier, int screenW, int screenH);

    void setCursorProvider(CursorProvider provider);
    void setCursorWarper(CursorWarper warper);

    // Monitores em pixels da tela virtual (Screen.AllScreens) e o principal
    // (Screen.PrimaryScreen). Chamado na inicialização e quando a disposição
    // dos monitores muda (SystemEvents.DisplaySettingsChanged no original).
    void setMonitors(const std::vector<Rect2D>& monitors, int primary);

    // Ações vindas de InputAction
    void zoomIn(float delta);
    void zoomOut(float delta);
    void zoomOnToggle();
    void trackModifiersHeld();
    void trackModifiersReleased();
    void resetZoom();

    // Eventos de hooks globais (HookManager_* no original)
    void onCursorClicked();
    void onKeyDown(unsigned int keyCode, bool isAlt); // HookManager_KeyDown
    void onCharTyped();                               // HookManager_KeyPress
    void onKeyUp();                                   // HookManager_KeyUp

    // Eventos de acessibilidade (Automation no original), em pixels de tela
    void onGuiFocusChanged(const Rect2D& rect);       // OnFocusChange
    void onTextSelectionChanged(const Rect2D& rect);  // OnTextSelectChangeEvent
    void onCaretMoved(const Rect2D& rect);            // substitui GetGUIThreadInfo().rcCaret

    void setNeedsUpdate(bool v);
    void setCursorMoved();
    void setReadingOffset(double offset);
    double getReadingOffset();

    void updateLoop(double dt);

    double getZoomTarget();
    double getZoomLevel();
    double getZoomFactor();

private:
    struct ZoomState {
        bool on = false;
        double level = 0.0;
        double saved = 1.0;
        double target = 0.0;
        double factor = 1.0;
        double factorPre = 0.0;
        double velocity = 0.0;
        bool hasChanged = true;
        bool update = true;
    };

    struct TrackState {
        Vector2D position;
        Point2D target;
        Point2D lookOffset;
        float readingOffset = 0.0f;
        Vector2D velocity;
        Rect2D border;
        float snap = 0.5f;
        bool hasChanged = false;
        bool areKeysHeld = false;
        bool areKeysPressed = false;
    };

    struct FocusState {
        enum class Mode { Inactive, Mouse, Keyboard, Narrator }; // Inactive = Modes.None
        enum class Type { Nothing, Cursor, Gui, Caret, Text };  // Nothing = Types.None
        Mode mode = Mode::Mouse;
        Type type = Type::Cursor;
        bool guiChanged = false;
        bool caretChanged = false;
        bool textChanged = false;
        bool textKeyPressed = false;
        Point2D position;
        Point2D cursor;
        Rect2D guiRectangle, guiRectanglePre;
        Rect2D caretRectangle, caretRectanglePre;
        Rect2D textRectangle, textRectanglePre;
        Rect2D caretLatest;          // último caret informado pelo AT-SPI
        bool caretLatestValid = false;
    };

    struct FrameState {
        bool x = false;
        bool y = false;
        float lerp = 0.0f;
        float lerpW = 0.0f;
        float lerpH = 0.0f;
        static constexpr int FrameSize = 2;
    };

    struct RegionState {
        bool hasChanged = false;
        Rect2D current;    // Region.Current.Bounds (tela da lupa)
        Rect2D focused;    // Region.Focused.Bounds (tela que contém o foco)
        Rect2D rectangle;  // retângulo animado em direção a current
        Vector2D positionVelocity;
        Vector2D sizeVelocity;
    };

    struct Flags {
        bool needsUpdate = true;
        bool hasCursorMoved = false;
        bool hasCursorClicked = false;
        bool hasCursorChanged = true;
        bool hasKeyPressed = false;
        bool isControlDown = false;
        bool isKeyDown = false;
        void reset() {
            needsUpdate = hasCursorMoved = hasCursorClicked = hasCursorChanged =
                hasKeyPressed = isControlDown = isKeyDown = false;
        }
    };

    // Versões sem lock, chamadas com o mutex já adquirido
    void zoomOnToggleLocked();
    void setLastZoomTime() { _lastZoomTime = std::chrono::steady_clock::now(); }

    void autoOffStep();
    void cursorMovedStep();
    void focusStep();
    void zoomLevelStep(double dt);
    void zoomFactorStep();
    void regionStep(double dt);
    void changedScreen();
    void trackStep(double dt);
    void viewStep();
    void edgeStep();
    void clampStep(double dt);
    void readingOffsetStep();
    void updateStep();

    const LupaSettings& settings() const { return _settingsManager.getSettings(); }

    SettingsManager& _settingsManager;
    IMagnifier& _magnifier;
    CursorProvider _cursorProvider;
    CursorWarper _cursorWarper;
    unsigned int _keyCodePrevious = 0;
    std::recursive_mutex _mutex;

    ZoomState _zoom;
    TrackState _track;
    FocusState _focus;
    FrameState _frame;
    RegionState _region;
    std::vector<Rect2D> _monitors;
    int _primary = 0;
    Flags _flags;

    Rect2D _view;
    TrackMode _trackModePre = TrackMode::Border;
    Point2D _cursorNow;
    Point2D _cursorMovedAmount;
    Point2D _cursorPositionPre;
    std::chrono::steady_clock::time_point _lastZoomTime = std::chrono::steady_clock::now();
};

} // namespace LupaLinux
