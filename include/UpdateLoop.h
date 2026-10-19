#pragma once

#include "GlassbrickSettings.h"
#include "IMagnifier.h"
#include "Camera.h"
#include "Colour.h"
#include "Reading.h"
#include <atomic>
#include <thread>

namespace Glassbrick {

class UpdateLoop {
private:
    SettingsManager& _settingsManager;
    IMagnifier& _magnifier;
    Camera& _camera;
    Colour& _colour;
    Reading& _reading;

    std::atomic<bool> _running{false};
    std::thread _loopThread;

    void runLoop();

public:
    UpdateLoop(SettingsManager& settings, IMagnifier& magnifier,
               Camera& camera, Colour& colour, Reading& reading);
    ~UpdateLoop();

    void start();
    void stop();
    bool isRunning() const { return _running; }
};

} // namespace Glassbrick
