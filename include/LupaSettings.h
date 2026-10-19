#pragma once

#include "LupaTypes.h"
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>

namespace LupaLinux {

// Atalho de teclado (KeyBinding.cs). Cada ação tem no máximo um atalho.
// keySym é um KeySym X11; XK_grave tem significado especial: a tecla física à
// esquerda do "1" (VK_OEM_3 no Windows), seja qual for o layout.
struct KeyBinding {
    Modifiers modifiers;
    unsigned int keySym;
    InputAction action;

    KeyBinding(Modifiers m, unsigned int k, InputAction a)
        : modifiers(m), keySym(k), action(a) {}
};

class LupaSettings {
public:
    static const std::vector<float> ZoomSpeeds;
    static const std::vector<float> ZoomAmounts;
    static const std::vector<float> ColourTransitions;
    static const std::vector<float> TrackSpeeds;

    IntModeCycler<float> ZoomSpeedActual;
    IntModeCycler<float> ZoomAmountActual;
    EnumModeCycler<TrackMode> CurrentTrackModeActual;
    EnumModeCycler<ColourMode> CurrentColourModeActual;

    int TrackSpeedIndex = 2;
    bool TrackSpeedSame = true;
    float TrackBorderSizeBorder = 0.5f;
    float TrackBorderSizeKeyCombo = 0.5f;
    float TrackFrameSize = 1.1f;
    float TrackLocationX = 0.5f;
    float TrackLocationY = 0.5f;
    bool TrackLocationZoom = false;
    float TrackLocationZoomLevel = 1.0f;
    bool TrackTyping = true;
    bool TrackFocus = true;
    bool ClampEdge = true;
    bool AddFrame = false;
    bool PushEdge = true;
    bool TrackScreen = true;
    // Não existe no original: liga org.a11y.Status.IsEnabled para que navegadores
    // (Firefox) exponham o conteúdo das páginas ao AT-SPI (ver A11yStatus.h)
    bool AppAccessibility = false;

    int ColourTransitionIndex = 2;
    bool ColourTransitionSame = true;
    InvertMode ColourInvertType = InvertMode::Brightness;
    float ColourInvertSaturation = 0.5f;
    AdjustMode ColourAdjustType = AdjustMode::Deuteranomaly;
    float ColourAdjustAmount = 1.0f;
    bool ColourAdjustSimulate = false;
    TintColour ColourTint = TintColour::Azure;
    float ColourTintAmount = 0.5f;
    bool ColourTintParty = false;
    ContrastMode ColourContrast = ContrastMode::BlackAndYellow;
    float ColourContrastAmount = 1.0f;
    bool ColourContrastInvert = false;

    bool ProVersion = false;
    RenderMode CurrentRenderMode = RenderMode::Fullscreen;
    int RefreshRate = 60;

    Modifiers MouseModifiers = Modifiers::Control;
    bool MouseFlip = false;
    bool MouseAccelerate = true;
    bool KeyboardEnabled = true;

    bool ZoomOffAuto = false;
    float ZoomOffTimeSeconds = 10.0f;

    bool CursorKeysWasd = false;
    bool CursorKeysArrows = false;

    std::vector<KeyBinding> Bindings;

    LupaSettings();

    void resetToDefaults();

    std::optional<float> getZoomSpeed() const {
        float val = ZoomSpeedActual.getValue();
        if (val <= 0.0f) return std::nullopt; // instant
        return val;
    }

    float getZoomAmount() const {
        return ZoomAmountActual.getValue();
    }

    std::optional<float> getTrackSpeed() const {
        if (TrackSpeedIndex < 0 || TrackSpeedIndex >= static_cast<int>(TrackSpeeds.size()))
            return TrackSpeeds[2];
        return TrackSpeeds[TrackSpeedIndex];
    }

    std::optional<float> getColourTransition() const {
        if (ColourTransitionIndex < 0 || ColourTransitionIndex >= static_cast<int>(ColourTransitions.size()))
            return ColourTransitions[2];
        float val = ColourTransitions[ColourTransitionIndex];
        if (val <= 0.0f) return std::nullopt;
        return val;
    }

    float getCurrentTrackSize() const {
        switch (CurrentTrackModeActual.getMode()) {
            case TrackMode::Consistent: return TrackFrameSize;
            case TrackMode::Border: return TrackBorderSizeBorder;
            case TrackMode::KeyCombo: return TrackBorderSizeKeyCombo;
            default: return 0.5f;
        }
    }

    // Atalho atual de uma ação (nullptr se desligado)
    const KeyBinding* bindingFor(InputAction action) const;
    // Define (ou, com keySym 0, remove) o atalho de uma ação
    void setBinding(InputAction action, Modifiers modifiers, unsigned int keySym);

    // Persistência (src/SettingsFile.cpp). Formato "chave = valor" com seções
    // e comentários; valores inválidos ou desconhecidos geram avisos e mantêm
    // o valor atual. Também lê o formato antigo (glassbrick_config.json).
    bool loadFromFile(const std::string& filename, std::vector<std::string>& warnings);
    bool saveToFile(const std::string& filename) const;
    std::string toConfigText() const;  // conteúdo do arquivo (usado por --print-default-config)
};

// Texto de um atalho, ex.: "Ctrl+Alt+grave", "Alt+r" (ou "nenhum")
std::string bindingToString(Modifiers modifiers, unsigned int keySym);
// Interpreta o texto de um atalho; "nenhum" devolve keySym 0. Falha => nullopt
std::optional<std::pair<Modifiers, unsigned int>> parseBinding(const std::string& text, std::string& error);
// Nome da ação usado no arquivo (ex.: "ColourModeCycle")
const char* inputActionName(InputAction action);

class SettingsManager {
private:
    LupaSettings _settings;
    std::string _configPath;
    bool _modified = false;

public:
    explicit SettingsManager(const std::string& configPath);

    // ~/.config/lupa_linux/lupa_linux.conf (respeita $XDG_CONFIG_HOME)
    static std::string defaultConfigPath();
    // Onde a configuração ficava quando o programa se chamava glassbrick
    static std::string previousConfigPath();

    const LupaSettings& getSettings() const { return _settings; }
    LupaSettings& getSettingsMutable() { return _settings; }
    const std::string& configPath() const { return _configPath; }

    bool load(std::vector<std::string>& warnings);
    bool loadFrom(const std::string& path, std::vector<std::string>& warnings);
    bool save();  // cria a pasta se preciso

    // --- Recarregar sem reiniciar (a interface grava o arquivo e envia SIGHUP) ---
    //
    // Threads que não são a do UpdateLoop (entrada, foco, terminal) devem
    // segurar mutex() enquanto usam as configurações. A troca acontece em
    // applyPendingReload(), chamada pela thread do UpdateLoop no início de cada
    // quadro (como SwitchToModifiedSettings no original), com o mutex seguro.
    // Ordem dos locks: este mutex antes dos de Camera/Colour/Reading.
    std::recursive_mutex& mutex() { return _mutex; }
    void requestReload() { _reloadRequested = true; }
    bool applyPendingReload(std::vector<std::string>& warnings);
    // Chamado (com o mutex seguro) depois de cada recarga: evento Modified do original
    void addModifiedListener(std::function<void()> listener) { _listeners.push_back(std::move(listener)); }

private:
    std::recursive_mutex _mutex;
    std::atomic<bool> _reloadRequested{false};
    std::vector<std::function<void()>> _listeners;

public:
    void markModified() { _modified = true; }
    bool isModified() const { return _modified; }
    void clearModified() { _modified = false; }
};

} // namespace LupaLinux
