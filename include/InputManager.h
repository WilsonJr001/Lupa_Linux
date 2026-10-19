#pragma once

#include "GlassbrickTypes.h"
#include "GlassbrickSettings.h"
#include "Camera.h"
#include "Colour.h"
#include "Reading.h"
#include <X11/Xlib.h>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

namespace Glassbrick {

// Entrada global (equivalente a Input.cs + partes de HookManager).
//
// No Windows o original usa hooks de baixo nível (WH_KEYBOARD_LL/WH_MOUSE_LL).
// Aqui usamos "grabs" passivos do X11, que também consomem o evento (como o
// e.Handled = true do original):
//  - XGrabKey para cada KeyBinding;
//  - XGrabButton nos botões 4/5 (roda do mouse) com os MouseModifiers.
// O estado dos modificadores (TrackModifiersDown/Up) é lido do XQueryPointer.
//
// Enquanto o modo leitura está ativo, as teclas dele (W A S D Q E F Esc) também
// são capturadas.
//
// Para "ver" todas as teclas e cliques sem consumi-los (como os hooks
// WH_KEYBOARD_LL/WH_MOUSE_LL), usa eventos raw do XInput2. Sem XInput2, os
// cliques caem para a máscara do XQueryPointer e o modo Teclado fica inativo.
//
// Quando outro programa captura o teclado ou o mouse inteiro (um menu aberto,
// por exemplo), os grabs passivos não disparam. Os eventos raw continuam
// chegando, então um atalho visto só por eles é executado assim mesmo, depois
// de esperar um instante pelo evento do grab (para não executar duas vezes).
// No Windows os hooks de baixo nível nunca são bloqueados por menus.
class InputManager {
public:
    InputManager(SettingsManager& settings, Camera& camera, Colour& colour, Reading& reading);
    ~InputManager();

    bool initialise();
    void start();
    void stop();

    void triggerAction(InputAction action, float amount = 1.0f);

    // Configurações recarregadas: refaz os grabs na thread de entrada
    void requestRegrab() { _regrabRequested = true; }

private:
    unsigned int toX11Mask(Modifiers mod) const;
    Modifiers fromX11Mask(unsigned int mask) const;
    void grabAll();
    void ungrabAll();
    void setReadingKeysGrabbed(bool grabbed);
    void processKeyEvent(const XKeyEvent& ev);
    const KeyBinding* findBinding(KeyCode keycode, unsigned int state) const;
    void rawShortcut(int id, InputAction action);
    void coreShortcut(int id, InputAction action, float amount = 1.0f);
    void flushPendingShortcuts();
    unsigned int currentModifiers();
    void processKeyRelease(const XKeyEvent& ev);
    void processButtonEvent(const XButtonEvent& ev);
    bool initXInput2();
    void processRawEvent(int evtype, int detail);
    void pollPointer();
    KeyCode keycodeFor(unsigned int keySym) const;
    bool toggleAllowed();

    SettingsManager& _settingsManager;
    Camera& _camera;
    Colour& _colour;
    Reading& _reading;

    Display* _display = nullptr;
    Window _root = 0;

    std::atomic<bool> _running{false};
    std::atomic<bool> _regrabRequested{false};
    std::thread _inputThread;
    bool _trackModifiersDown = false;
    bool _readingKeysGrabbed = false;
    unsigned int _buttonMaskPre = 0;
    bool _hasXI2 = false;
    int _xiOpcode = 0;
    KeyCode _tildeKeycode = 49;
    // SettingsForm.TrackModeCycle/ColourModeCycle: ignora repetições em menos de 300 ms
    std::chrono::steady_clock::time_point _lastToggleTime{};

    // Atalhos vistos pelos eventos raw, esperando o evento do grab.
    // id = keycode da tecla, ou 1000 + botão da roda.
    struct PendingShortcut {
        int id;
        InputAction action;
        std::chrono::steady_clock::time_point time;
    };
    std::vector<PendingShortcut> _pendingShortcuts;
    std::map<int, std::chrono::steady_clock::time_point> _lastCoreShortcut;
};

} // namespace Glassbrick
