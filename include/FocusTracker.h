#pragma once

#include "LupaTypes.h"
#include <atomic>
#include <thread>

namespace LupaLinux {

class Camera;
class SettingsManager;

// Acompanha foco, caret e seleção de texto de outros programas via AT-SPI2
// (equivalente às assinaturas de UI Automation feitas em Camera.cs:
// SubscribeToFocusChange e SubscribeToTextSelectEvent).
//
// Eventos ouvidos:
//   object:state-changed:focused   -> Camera::onGuiFocusChanged (extents do elemento)
//   object:text-caret-moved        -> Camera::onCaretMoved (extents do caractere no caret)
//   object:text-selection-changed  -> Camera::onTextSelectionChanged (extents da seleção)
//
// O AT-SPI tem seu próprio loop (GMainLoop) e não é thread-safe, então tudo
// roda numa thread dedicada. Só aplicativos com acessibilidade ativa enviam
// eventos (GTK e Qt sim; Firefox/Chromium podem exigir ativação).
class FocusTracker {
public:
    FocusTracker(Camera& camera, SettingsManager& settings);
    ~FocusTracker();

    void start();
    void stop();

private:
    void run();

    Camera& _camera;
    SettingsManager& _settings;
    std::thread _thread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _loopStarted{false};
    std::atomic<bool> _finished{false};
};

} // namespace LupaLinux
