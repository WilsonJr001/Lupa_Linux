#include "InputManager.h"
#include "XExtCompat.h"
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace LupaLinux {

// Variações de NumLock/CapsLock que precisam ser "grabbadas" junto
static const unsigned int kLockMasks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
static const unsigned int kRelevantMods = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;
static const unsigned int kClickButtons = Button1Mask | Button2Mask | Button3Mask;
static const int kWheelId = 1000;
// Quanto esperar pelo evento do grab antes de concluir que outro programa capturou a entrada
static const auto kGrabWait = std::chrono::milliseconds(60);

// Teclas tratadas por Reading.HookManager_KeyDown no original
static const KeySym kReadingKeys[] = { XK_w, XK_a, XK_s, XK_d, XK_q, XK_e, XK_f, XK_Escape };

// LUPA_LINUX_DEBUG=1 avisa quando um atalho chegou só pelos eventos raw
static bool debugEnabled() {
    static const bool enabled = std::getenv("LUPA_LINUX_DEBUG") != nullptr;
    return enabled;
}

static bool isReadingKey(KeySym sym) {
    for (KeySym k : kReadingKeys) if (k == sym) return true;
    return false;
}

InputManager::InputManager(SettingsManager& settings, Camera& camera, Colour& colour, Reading& reading)
    : _settingsManager(settings), _camera(camera), _colour(colour), _reading(reading) {}

InputManager::~InputManager() {
    stop();
}

unsigned int InputManager::toX11Mask(Modifiers mod) const {
    unsigned int mask = 0;
    if ((mod & Modifiers::Control) != Modifiers::NoModifier) mask |= ControlMask;
    if ((mod & Modifiers::Alt) != Modifiers::NoModifier) mask |= Mod1Mask;
    if ((mod & Modifiers::Shift) != Modifiers::NoModifier) mask |= ShiftMask;
    if ((mod & Modifiers::Windows) != Modifiers::NoModifier) mask |= Mod4Mask;
    return mask;
}

Modifiers InputManager::fromX11Mask(unsigned int mask) const {
    Modifiers m = Modifiers::NoModifier;
    if (mask & ControlMask) m |= Modifiers::Control;
    if (mask & Mod1Mask) m |= Modifiers::Alt;
    if (mask & ShiftMask) m |= Modifiers::Shift;
    if (mask & Mod4Mask) m |= Modifiers::Windows;
    return m;
}

bool InputManager::initialise() {
    _display = XOpenDisplay(nullptr);
    if (!_display) {
        std::cerr << "[InputManager] Falha ao abrir conexão X11 para entrada.\n";
        return false;
    }
    _root = DefaultRootWindow(_display);
    _hasXI2 = initXInput2();
    if (!_hasXI2) {
        std::cerr << "[InputManager] Aviso: XInput2 indisponível; acompanhamento do teclado desativado.\n";
    }
    return true;
}

bool InputManager::initXInput2() {
    int event = 0, error = 0;
    if (!XQueryExtension(_display, "XInputExtension", &_xiOpcode, &event, &error)) return false;
    int major = 2, minor = 2;
    if (XIQueryVersion(_display, &major, &minor) != Success) return false;

    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
    XISetMask(mask, XI_RawKeyPress);
    XISetMask(mask, XI_RawKeyRelease);
    XISetMask(mask, XI_RawButtonPress);
    XIEventMask evmask{ XIAllMasterDevices, static_cast<int>(sizeof(mask)), mask };
    XISelectEvents(_display, _root, &evmask, 1);
    XFlush(_display);
    return true;
}

static bool isModifierKey(KeySym sym) {
    return (sym >= XK_Shift_L && sym <= XK_Hyper_R) || sym == XK_ISO_Level3_Shift || sym == XK_Mode_switch;
}

// Aproxima o KeyPress do WinForms (tecla que gera caractere)
static bool producesCharacter(KeySym sym) {
    return (sym >= 0x20 && sym < 0xff00) || (sym >= XK_KP_Space && sym <= XK_KP_9) ||
           sym == XK_BackSpace || sym == XK_Return || sym == XK_Tab || sym > 0x1000000; // Unicode
}

static bool isReadingKey(KeySym sym);

void InputManager::processRawEvent(int evtype, int detail) {
    if (evtype == XI_RawKeyPress) {
        KeySym sym = XkbKeycodeToKeysym(_display, static_cast<KeyCode>(detail), 0, 0);
        _camera.onKeyDown(static_cast<unsigned int>(detail), sym == XK_Alt_L || sym == XK_Alt_R);
        if (!isModifierKey(sym) && _settingsManager.getSettings().KeyboardEnabled) {
            if (const KeyBinding* b = findBinding(static_cast<KeyCode>(detail), currentModifiers())) {
                rawShortcut(detail, b->action);
            }
        }
        if (!isModifierKey(sym)) {
            if (producesCharacter(sym)) _camera.onCharTyped();
            // As teclas do modo leitura chegam pelo grab; qualquer outra para o pan
            if (!isReadingKey(sym)) _reading.onOtherKey();
        }
    } else if (evtype == XI_RawKeyRelease) {
        _camera.onKeyUp();
    } else if (evtype == XI_RawButtonPress && (detail == Button4 || detail == Button5)) {
        const auto& s = _settingsManager.getSettings();
        if (currentModifiers() == toX11Mask(s.MouseModifiers)) {
            bool zoomIn = (detail == Button4) ^ s.MouseFlip;
            rawShortcut(kWheelId + detail, zoomIn ? InputAction::ZoomIn : InputAction::ZoomOut);
        }
    } else if (evtype == XI_RawButtonPress && detail >= 1 && detail <= 3) {
        // HookManager.MouseDown (Camera) e HookManager.MouseClick (Reading)
        _camera.onCursorClicked();
        _reading.onMouseClick();
    }
}

// Tecla física à esquerda do "1", abaixo do Esc (nome XKB "TLDE"). É o que o
// Windows chama de VK_OEM_3 (Keys 192): ` no layout americano, ' no ABNT2.
static KeyCode tildeKeyKeycode(Display* display) {
    KeyCode found = 0;
    XkbDescPtr desc = XkbGetMap(display, 0, XkbUseCoreKbd);
    if (desc) {
        if (XkbGetNames(display, XkbKeyNamesMask, desc) == Success && desc->names && desc->names->keys) {
            for (int kc = desc->min_key_code; kc <= desc->max_key_code && !found; ++kc) {
                if (std::strncmp(desc->names->keys[kc].name, "TLDE", XkbKeyNameLength) == 0) {
                    found = static_cast<KeyCode>(kc);
                }
            }
        }
        XkbFreeKeyboard(desc, 0, True);
    }
    return found ? found : 49; // 49 = TLDE no mapeamento evdev padrão
}

KeyCode InputManager::keycodeFor(unsigned int keySym) const {
    if (keySym == XK_grave) return _tildeKeycode;
    return XKeysymToKeycode(_display, keySym);
}

void InputManager::grabAll() {
    const auto& s = _settingsManager.getSettings();
    _tildeKeycode = tildeKeyKeycode(_display);

    if (s.KeyboardEnabled) {
        for (const auto& binding : s.Bindings) {
            KeyCode kc = keycodeFor(binding.keySym);
            if (kc == 0) continue;
            unsigned int base = toX11Mask(binding.modifiers);
            for (unsigned int extra : kLockMasks) {
                XGrabKey(_display, kc, base | extra, _root, False, GrabModeAsync, GrabModeAsync);
            }
        }
    }

    // Roda do mouse + MouseModifiers (padrão: Ctrl) => zoom
    unsigned int wheelMods = toX11Mask(s.MouseModifiers);
    for (unsigned int button : { Button4, Button5 }) {
        for (unsigned int extra : kLockMasks) {
            XGrabButton(_display, button, wheelMods | extra, _root, False, ButtonPressMask,
                        GrabModeAsync, GrabModeAsync, None, None);
        }
    }
    XSync(_display, False);
}

void InputManager::setReadingKeysGrabbed(bool grabbed) {
    if (grabbed == _readingKeysGrabbed) return;
    _readingKeysGrabbed = grabbed;
    for (KeySym sym : kReadingKeys) {
        KeyCode kc = XKeysymToKeycode(_display, sym);
        if (kc == 0) continue;
        // Sem modificador e com Shift (Shift+W/S pula uma linha inteira)
        for (unsigned int mods : { 0u, static_cast<unsigned int>(ShiftMask) }) {
            for (unsigned int extra : kLockMasks) {
                if (grabbed) XGrabKey(_display, kc, mods | extra, _root, False, GrabModeAsync, GrabModeAsync);
                else XUngrabKey(_display, kc, mods | extra, _root);
            }
        }
    }
    XFlush(_display);
}

void InputManager::ungrabAll() {
    if (!_display) return;
    _readingKeysGrabbed = false;
    XUngrabKey(_display, AnyKey, AnyModifier, _root);
    XUngrabButton(_display, AnyButton, AnyModifier, _root);
    XFlush(_display);
}

void InputManager::start() {
    if (_running) return;
    if (!_display && !initialise()) return;

    grabAll();
    _running = true;

    _inputThread = std::thread([this]() {
        while (_running) {
          {
            // Configurações podem ser trocadas pela thread do loop (recarga)
            std::lock_guard<std::recursive_mutex> settingsLock(_settingsManager.mutex());
            if (_regrabRequested.exchange(false)) {
                ungrabAll();
                grabAll();
                std::cout << "[Input] Atalhos atualizados\n";
            }
            pollPointer();
            setReadingKeysGrabbed(_reading.isActive());
            while (XPending(_display) > 0) {
                XEvent ev;
                XNextEvent(_display, &ev);
                if (ev.type == GenericEvent && ev.xcookie.extension == _xiOpcode) {
                    if (XGetEventData(_display, &ev.xcookie)) {
                        const auto* raw = static_cast<const XIRawEvent*>(ev.xcookie.data);
                        processRawEvent(raw->evtype, raw->detail);
                        XFreeEventData(_display, &ev.xcookie);
                    }
                } else if (ev.type == KeyPress) processKeyEvent(ev.xkey);
                else if (ev.type == KeyRelease) processKeyRelease(ev.xkey);
                else if (ev.type == ButtonPress) processButtonEvent(ev.xbutton);
            }
            flushPendingShortcuts();
          }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });
}

void InputManager::stop() {
    if (!_running) return;
    _running = false;
    if (_inputThread.joinable()) _inputThread.join();
    ungrabAll();
    if (_display) {
        XCloseDisplay(_display);
        _display = nullptr;
    }
}

void InputManager::pollPointer() {
    Window rootRet, childRet;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(_display, _root, &rootRet, &childRet, &rootX, &rootY, &winX, &winY, &mask)) return;

    // Sem XInput2, cliques são detectados pela máscara de botões
    unsigned int buttons = mask & kClickButtons;
    if (!_hasXI2 && (buttons & ~_buttonMaskPre)) {
        _camera.onCursorClicked();
        _reading.onMouseClick();
    }
    _buttonMaskPre = buttons;

    // Input.cs: quando os modificadores pressionados são exatamente os
    // MouseModifiers dispara TrackModifiersDown; ao soltar, TrackModifiersUp.
    bool down = fromX11Mask(mask & kRelevantMods) == _settingsManager.getSettings().MouseModifiers;
    if (down != _trackModifiersDown) {
        _trackModifiersDown = down;
        triggerAction(down ? InputAction::TrackModifiersDown : InputAction::TrackModifiersUp);
    }
}

void InputManager::processKeyEvent(const XKeyEvent& ev) {
    KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&ev), 0);
    unsigned int state = ev.state & kRelevantMods;

    if (_readingKeysGrabbed && (state & ~ShiftMask) == 0) {
        for (KeySym rk : kReadingKeys) {
            if (rk == sym) {
                _reading.onKeyDown(sym, (state & ShiftMask) != 0);
                return;
            }
        }
    }

    // Compara pela tecla física (keycode), como os Keys do Windows, e não
    // pelo símbolo, que muda com o layout e com teclas mortas
    if (const KeyBinding* b = findBinding(static_cast<KeyCode>(ev.keycode), state)) {
        coreShortcut(ev.keycode, b->action);
    }
}

const KeyBinding* InputManager::findBinding(KeyCode keycode, unsigned int state) const {
    for (const auto& binding : _settingsManager.getSettings().Bindings) {
        if (binding.keySym != 0 && keycodeFor(binding.keySym) == keycode && toX11Mask(binding.modifiers) == state) {
            return &binding;
        }
    }
    return nullptr;
}

unsigned int InputManager::currentModifiers() {
    Window rootRet, childRet;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int mask = 0;
    XQueryPointer(_display, _root, &rootRet, &childRet, &rootX, &rootY, &winX, &winY, &mask);
    return mask & kRelevantMods;
}

// Evento raw de um atalho: normalmente o evento do grab chega junto e o
// executa; este só fica guardado para o caso de o grab não disparar.
void InputManager::rawShortcut(int id, InputAction action) {
    const auto now = std::chrono::steady_clock::now();
    auto it = _lastCoreShortcut.find(id);
    if (it != _lastCoreShortcut.end() && now - it->second < kGrabWait * 2) return; // o grab já tratou
    _pendingShortcuts.push_back({id, action, now});
}

void InputManager::coreShortcut(int id, InputAction action, float amount) {
    _lastCoreShortcut[id] = std::chrono::steady_clock::now();
    for (auto it = _pendingShortcuts.begin(); it != _pendingShortcuts.end(); ++it) {
        if (it->id == id) {
            _pendingShortcuts.erase(it); // o mesmo toque, visto pelos dois caminhos
            break;
        }
    }
    triggerAction(action, amount);
}

void InputManager::flushPendingShortcuts() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = _pendingShortcuts.begin(); it != _pendingShortcuts.end();) {
        if (now - it->time >= kGrabWait) {
            if (debugEnabled()) std::cout << "[Input] Atalho recebido com a entrada capturada por outro programa\n";
            InputAction action = it->action;
            it = _pendingShortcuts.erase(it);
            triggerAction(action);
        } else {
            ++it;
        }
    }
}

void InputManager::processKeyRelease(const XKeyEvent& ev) {
    KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&ev), 0);
    _reading.onKeyUp(sym);
}

void InputManager::processButtonEvent(const XButtonEvent& ev) {
    if (ev.button != Button4 && ev.button != Button5) return;
    const auto& s = _settingsManager.getSettings();
    // No X11 cada "clique" da roda é um evento (Delta 120 no Windows), então
    // a quantidade é sempre 1, com ou sem MouseAccelerate.
    bool zoomIn = (ev.button == Button4) ^ s.MouseFlip;
    coreShortcut(kWheelId + static_cast<int>(ev.button), zoomIn ? InputAction::ZoomIn : InputAction::ZoomOut, 1.0f);
}

bool InputManager::toggleAllowed() {
    const auto now = std::chrono::steady_clock::now();
    if (now - _lastToggleTime < std::chrono::milliseconds(300)) return false;
    _lastToggleTime = now;
    return true;
}

void InputManager::triggerAction(InputAction action, float amount) {
    // Também chamado pela thread do terminal (main.cpp)
    std::lock_guard<std::recursive_mutex> settingsLock(_settingsManager.mutex());
    auto& settings = _settingsManager.getSettingsMutable();

    switch (action) {
        case InputAction::ZoomIn:
            _camera.zoomIn(amount);
            break;
        case InputAction::ZoomOut:
            _camera.zoomOut(amount);
            break;
        case InputAction::ZoomOnToggle:
            _camera.zoomOnToggle();
            break;
        case InputAction::ZoomSpeedCycle: {
            int mode = settings.ZoomSpeedActual.cycle();
            std::cout << "[Input] Velocidade do zoom -> modo " << mode << "\n";
            _camera.setNeedsUpdate(true);
            break;
        }
        case InputAction::ZoomAmountCycle: {
            int mode = settings.ZoomAmountActual.cycle();
            std::cout << "[Input] Passo do zoom -> modo " << mode << " (" << settings.getZoomAmount() << ")\n";
            break;
        }
        case InputAction::TrackModeCycle: {
            if (!toggleAllowed()) break;
            TrackMode tm = settings.CurrentTrackModeActual.cycle();
            const char* name = "?";
            switch (tm) {
                case TrackMode::Centre: name = "Centre"; break;
                case TrackMode::Consistent: name = "Consistent"; break;
                case TrackMode::Border: name = "Border"; break;
                case TrackMode::KeyCombo: name = "KeyCombo"; break;
                case TrackMode::ScreenLocation: name = "ScreenLocation"; break;
                case TrackMode::NoTracking: name = "None"; break;
            }
            std::cout << "[Input] Modo de rastreamento -> " << name << "\n";
            _camera.setNeedsUpdate(true);
            break;
        }
        case InputAction::ColourModeCycle: {
            if (!toggleAllowed()) break;
            ColourMode cm = settings.CurrentColourModeActual.cycle();
            const char* name = "?";
            switch (cm) {
                case ColourMode::Default: name = "Default"; break;
                case ColourMode::Invert: name = "Invert"; break;
                case ColourMode::Adjust: name = "Adjust"; break;
                case ColourMode::Tint: name = "Tint"; break;
                case ColourMode::Contrast: name = "Contrast"; break;
            }
            std::cout << "[Input] Modo de cor -> " << name << "\n";
            _colour.changeMatrixTargets();
            break;
        }
        case InputAction::ReadingModeToggle:
            _reading.activateToggle();
            break;
        case InputAction::TrackModifiersDown:
            _camera.trackModifiersHeld();
            break;
        case InputAction::TrackModifiersUp:
            _camera.trackModifiersReleased();
            break;
        case InputAction::AnyKeyReleased:
            break;
    }
}

} // namespace LupaLinux
