#include "GlassbrickSettings.h"
#include <X11/keysym.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace Glassbrick {

const std::vector<float> GlassbrickSettings::ZoomSpeeds = {
    1.0f / 32.0f, 0.0625f, 0.125f, 0.25f, 0.0f
};

const std::vector<float> GlassbrickSettings::ZoomAmounts = {
    0.0625f, 0.125f, 0.25f, 0.5f, 1.0f
};

const std::vector<float> GlassbrickSettings::ColourTransitions = {
    1.0f / 48.0f, 0.0625f, 0.125f, 0.25f, 0.0f
};

const std::vector<float> GlassbrickSettings::TrackSpeeds = {
    1.0f / 32.0f, 0.0625f, 0.125f, 0.25f, 1.0f / 3.0f
};

GlassbrickSettings::GlassbrickSettings()
    : ZoomSpeedActual(2, static_cast<int>(ZoomSpeeds.size()), [](int m) { return ZoomSpeeds[m]; }),
      ZoomAmountActual(2, static_cast<int>(ZoomAmounts.size()), [](int m) { return ZoomAmounts[m]; }),
      CurrentTrackModeActual(TrackMode::Border, {
          TrackMode::Centre, TrackMode::Consistent, TrackMode::Border,
          TrackMode::KeyCombo, TrackMode::ScreenLocation, TrackMode::NoTracking
      }),
      CurrentColourModeActual(ColourMode::Default, {
          ColourMode::Default, ColourMode::Invert, ColourMode::Adjust,
          ColourMode::Tint, ColourMode::Contrast
      }) {
    resetToDefaults();
}

void GlassbrickSettings::resetToDefaults() {
    Bindings.clear();
    // XK_grave aqui significa a tecla física à esquerda do "1" (Keys 192 /
    // VK_OEM_3 no original): ` no teclado americano, ' no ABNT2. O
    // InputManager a localiza pela posição (XKB "TLDE"), não pelo símbolo.
    // Atalhos padrão compatíveis com Glassbrick original:
    // Alt + ~  -> Zoom On/Off Toggle
    Bindings.emplace_back(Modifiers::Alt, XK_grave, InputAction::ZoomOnToggle);
    // Alt + 1  -> Zoom In
    Bindings.emplace_back(Modifiers::Alt, XK_1, InputAction::ZoomIn);
    // Alt + 2  -> Zoom Out
    Bindings.emplace_back(Modifiers::Alt, XK_2, InputAction::ZoomOut);
    // Alt + 3  -> Ciclo de Velocidade de Zoom
    Bindings.emplace_back(Modifiers::Alt, XK_3, InputAction::ZoomSpeedCycle);
    // Alt + 4  -> Ciclo de Quantidade de Zoom
    Bindings.emplace_back(Modifiers::Alt, XK_4, InputAction::ZoomAmountCycle);
    // Ctrl + ~ -> Ciclo de Modo de Rastreamento (Centre, Consistent, Border...)
    Bindings.emplace_back(Modifiers::Control, XK_grave, InputAction::TrackModeCycle);
    // Ctrl+Alt+` -> Ciclo de Modo de Cores (Default, Invert, Adjust, Tint, Contrast)
    // DIFERENÇA INTENCIONAL: no original é ` sem modificador. No ABNT2 essa
    // tecla é o apóstrofo, que ficaria impossível de digitar com a lupa aberta.
    Bindings.emplace_back(Modifiers::Control | Modifiers::Alt, XK_grave, InputAction::ColourModeCycle);
    // Alt + R  -> Modo Leitura (Reading Mode Toggle)
    Bindings.emplace_back(Modifiers::Alt, XK_r, InputAction::ReadingModeToggle);

    ProVersion = false;
    CurrentRenderMode = RenderMode::Fullscreen;
    RefreshRate = 60;

    ZoomSpeedActual.setMode(2);
    ZoomSpeedActual.setModeEnabled(0, true);
    ZoomSpeedActual.setModeEnabled(1, false);
    ZoomSpeedActual.setModeEnabled(2, true);
    ZoomSpeedActual.setModeEnabled(3, false);
    ZoomSpeedActual.setModeEnabled(4, true);

    ZoomAmountActual.setMode(2);
    ZoomAmountActual.setModeEnabled(0, true);
    ZoomAmountActual.setModeEnabled(1, false);
    ZoomAmountActual.setModeEnabled(2, true);
    ZoomAmountActual.setModeEnabled(3, false);
    ZoomAmountActual.setModeEnabled(4, true);

    CurrentTrackModeActual.setMode(TrackMode::Centre);
    CurrentTrackModeActual.setModeEnabled(TrackMode::Centre, true);
    CurrentTrackModeActual.setModeEnabled(TrackMode::Consistent, false);
    CurrentTrackModeActual.setModeEnabled(TrackMode::Border, true);
    CurrentTrackModeActual.setModeEnabled(TrackMode::KeyCombo, false);
    CurrentTrackModeActual.setModeEnabled(TrackMode::ScreenLocation, false);
    CurrentTrackModeActual.setModeEnabled(TrackMode::NoTracking, false);

    CurrentColourModeActual.setMode(ColourMode::Default);
    CurrentColourModeActual.setModeEnabled(ColourMode::Default, true);
    CurrentColourModeActual.setModeEnabled(ColourMode::Invert, true);
    CurrentColourModeActual.setModeEnabled(ColourMode::Adjust, false);
    CurrentColourModeActual.setModeEnabled(ColourMode::Tint, false);
    CurrentColourModeActual.setModeEnabled(ColourMode::Contrast, false);

    MouseModifiers = Modifiers::Control;
    MouseFlip = false;
    MouseAccelerate = true;
    KeyboardEnabled = true;

    ZoomOffAuto = false;
    ZoomOffTimeSeconds = 10.0f;

    TrackSpeedIndex = 2;
    TrackSpeedSame = true;
    TrackBorderSizeBorder = 0.5f;
    TrackBorderSizeKeyCombo = 0.5f;
    TrackFrameSize = 1.1f;
    TrackLocationX = 0.5f;
    TrackLocationY = 0.5f;
    TrackLocationZoom = false;
    TrackLocationZoomLevel = 1.0f;
    TrackTyping = true;
    TrackFocus = true;
    ClampEdge = true;
    AddFrame = false;
    PushEdge = true;
    TrackScreen = true;
    AppAccessibility = false;

    ColourTransitionIndex = 2;
    ColourTransitionSame = true;
    ColourInvertType = InvertMode::Brightness;
    ColourInvertSaturation = 0.5f;
    ColourAdjustType = AdjustMode::Deuteranomaly;
    ColourAdjustAmount = 1.0f;
    ColourAdjustSimulate = false;
    ColourTint = TintColour::Azure;
    ColourTintAmount = 0.5f;
    ColourTintParty = false;
    ColourContrast = ContrastMode::BlackAndYellow;
    ColourContrastAmount = 1.0f;
    ColourContrastInvert = false;

    CursorKeysWasd = false;
    CursorKeysArrows = false;
}

const KeyBinding* GlassbrickSettings::bindingFor(InputAction action) const {
    for (const auto& b : Bindings) {
        if (b.action == action) return &b;
    }
    return nullptr;
}

void GlassbrickSettings::setBinding(InputAction action, Modifiers modifiers, unsigned int keySym) {
    Bindings.erase(std::remove_if(Bindings.begin(), Bindings.end(),
                                  [action](const KeyBinding& b) { return b.action == action; }),
                   Bindings.end());
    if (keySym != 0) Bindings.emplace_back(modifiers, keySym, action);
}

} // namespace Glassbrick
