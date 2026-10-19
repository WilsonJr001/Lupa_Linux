#include "UpdateLoop.h"
#include <chrono>
#include <iostream>

namespace Glassbrick {

UpdateLoop::UpdateLoop(SettingsManager& settings, IMagnifier& magnifier,
                       Camera& camera, Colour& colour, Reading& reading)
    : _settingsManager(settings),
      _magnifier(magnifier),
      _camera(camera),
      _colour(colour),
      _reading(reading) {}

UpdateLoop::~UpdateLoop() {
    stop();
}

void UpdateLoop::start() {
    if (_running) return;
    _running = true;
    _loopThread = std::thread(&UpdateLoop::runLoop, this);
}

void UpdateLoop::stop() {
    if (!_running) return;
    _running = false;
    if (_loopThread.joinable()) {
        _loopThread.join();
    }
}

void UpdateLoop::runLoop() {
    using clock = std::chrono::steady_clock;

    _magnifier.initialise();

    auto lastTime = clock::now();
    double dt = 1.0;
    bool shuttingDown = false;
    clock::time_point shutdownDeadline = clock::time_point::max();

    while (clock::now() < shutdownDeadline) {
        // Como no original: ao desligar, volta o zoom e as cores ao normal
        // com animação durante 1 segundo antes de sair.
        if (!_running && !shuttingDown) {
            shuttingDown = true;
            shutdownDeadline = clock::now() + std::chrono::seconds(1);
            std::lock_guard<std::recursive_mutex> settingsLock(_settingsManager.mutex());
            auto& s = _settingsManager.getSettingsMutable();
            s.ZoomSpeedActual.setMode(2);
            s.ColourTransitionIndex = 2;
            s.CurrentColourModeActual.setMode(ColourMode::Default);
            _colour.changeMatrixTargets();
            _camera.resetZoom();
        }

        const auto frameStart = clock::now();

        // SwitchToModifiedSettings: a interface gravou o arquivo e pediu recarga
        std::vector<std::string> warnings;
        if (_settingsManager.applyPendingReload(warnings)) {
            std::cout << "[Config] Configurações recarregadas\n";
        }
        for (const auto& w : warnings) std::cerr << "[Config] Aviso: " << w << "\n";

        _camera.updateLoop(dt);
        _colour.colourUpdateStep(dt);
        _reading.updateLoop(dt);
        // Diferente do Windows, o conteúdo ampliado precisa ser redesenhado a
        // cada quadro (ver IMagnifier.h)
        _magnifier.render();

        int refreshRate = _settingsManager.getSettings().RefreshRate;
        if (refreshRate <= 0) refreshRate = 60;
        const double frameTargetMs = 1000.0 / refreshRate;
        const double elapsedMs = std::chrono::duration<double, std::milli>(clock::now() - frameStart).count();
        const double remainingMs = frameTargetMs - elapsedMs;
        if (remainingMs > 1.0) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remainingMs));
        }

        const auto now = clock::now();
        // dt em "quadros de 60 Hz" (16.67 ms = 1.0), como no original
        dt = std::chrono::duration<double, std::milli>(now - lastTime).count() / 16.666666666666668;
        if (dt < 0.1) dt = 0.1;
        if (dt > 3.0) dt = 3.0; // evita saltos se o processo congelar
        lastTime = now;
    }

    _magnifier.shutdown();
    std::cout << "[UpdateLoop] Loop encerrado.\n";
}

} // namespace Glassbrick
