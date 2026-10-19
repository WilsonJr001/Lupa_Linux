#include <atomic>
#include <chrono>
#include <csignal>
#include <climits>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>

#include <X11/Xlib.h>

#include "Camera.h"
#include "Colour.h"
#include "FocusTracker.h"
#include "GlassbrickSettings.h"
#include "InputManager.h"
#include "MagnifierLinux.h"
#include "Reading.h"
#include "SingleInstance.h"
#include "A11yStatus.h"
#include "DesktopInfo.h"
#include "UpdateLoop.h"

extern char** environ;

static std::atomic<bool> g_keepRunning{true};
static std::atomic<bool> g_reloadRequested{false};
static std::atomic<bool> g_showSettings{false};

static void signalHandler(int) {
    g_keepRunning = false;
}

// SIGHUP: a interface de configurações gravou o arquivo
static void reloadHandler(int) {
    g_reloadRequested = true;
}

// SIGUSR1: alguém tentou abrir uma segunda lupa; mostra as configurações
static void showSettingsHandler(int) {
    g_showSettings = true;
}

// Pasta do executável (a interface fica em <pasta>/ui/glassbrick_ui.py)
static std::string executablePath() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return buf;
}

// Abre a interface (ícone na bandeja + janela de configurações) em Python/GTK
static pid_t launchSettingsUi(const std::string& configPath) {
    const std::string exe = executablePath();
    const std::string dir = exe.substr(0, exe.find_last_of('/'));
    const std::string script = dir + "/ui/glassbrick_ui.py";
    if (access(script.c_str(), R_OK) != 0) {
        std::cerr << "[Interface] Aviso: " << script << " não encontrado; sem ícone na bandeja.\n";
        return -1;
    }
    const std::string pid = std::to_string(getpid());
    std::vector<std::string> args = {"python3", script, "--pid", pid, "--config", configPath, "--exe", exe};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t child = -1;
    if (posix_spawnp(&child, "python3", nullptr, nullptr, argv.data(), environ) != 0) {
        std::cerr << "[Interface] Aviso: não foi possível iniciar a interface (python3).\n";
        return -1;
    }
    std::cout << "[Interface] Ícone na bandeja iniciado (pid " << child << ")\n";
    return child;
}

static void printBanner() {
    std::cout << "=========================================================\n"
              << "         GLASSBRICK - CLONE C++ PARA LINUX (X11)         \n"
              << "=========================================================\n"
              << " Atalhos globais (padrões do Glassbrick original):\n"
              << "   [Ctrl + roda]   : Aumentar / diminuir zoom\n"
              << "   (` = tecla à esquerda do 1, abaixo do Esc; no ABNT2 é a tecla ')\n"
              << "   [Alt + `]       : Ligar / desligar zoom\n"
              << "   [Alt + 1 / 2]   : Aumentar / diminuir zoom\n"
              << "   [Alt + 3]       : Alternar velocidade do zoom\n"
              << "   [Alt + 4]       : Alternar passo do zoom\n"
              << "   [Ctrl + `]      : Alternar modo de rastreamento\n"
              << "   [Ctrl + Alt + `]: Alternar modo de cor\n"
              << "   [Alt + R]       : Modo de leitura (W/S, Shift+W/S, F, A/D, Q/E, Esc)\n"
              << " Terminal: + - z t c r q (+ Enter)\n"
              << "=========================================================\n\n";
}

int main(int argc, char** argv) {
    // Várias threads usam Xlib (entrada, loop de renderização)
    XInitThreads();
    Glassbrick::MagnifierLinux::installErrorHandler();

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, reloadHandler);
    std::signal(SIGUSR1, showSettingsHandler);

    std::string configPath = Glassbrick::SettingsManager::defaultConfigPath();
    bool customConfig = false;
    bool withUi = true;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            customConfig = true;
        } else if (arg == "--sem-interface") {
            withUi = false;
        } else if (arg == "--print-default-config") {
            // Usado pela interface para "Restaurar padrões" sem duplicar os valores
            std::cout << Glassbrick::GlassbrickSettings().toConfigText();
            return 0;
        } else if (arg == "--diagnostico") {
            std::cout << Glassbrick::DesktopInfo::query().report();
            return 0;
        } else if (arg == "--verificar-config" && i + 1 < argc) {
            // Usado pela interface antes de gravar: a lupa é quem decide o que é válido
            std::vector<std::string> warnings;
            if (!Glassbrick::GlassbrickSettings().loadFromFile(argv[i + 1], warnings)) {
                std::cerr << "não foi possível ler " << argv[i + 1] << "\n";
                return 2;
            }
            for (const auto& w : warnings) std::cout << w << "\n";
            return warnings.empty() ? 0 : 1;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: ./glassbrick [opções]\n"
                      << "  --config caminho         arquivo de configuração (padrão: " << configPath << ")\n"
                      << "  --sem-interface          não abre o ícone na bandeja / janela de configurações\n"
                      << "  --print-default-config   mostra a configuração padrão e sai\n"
                      << "  --verificar-config arq   confere um arquivo de configuração e sai (1 = há erros)\n"
                      << "  --diagnostico            mostra o que a lupa enxerga do ambiente gráfico e sai\n"
                      << "Enviar SIGHUP ao processo recarrega a configuração.\n"
                      << "Atalhos: docs/ATALHOS.md | Opções: docs/CONFIGURACAO.md\n";
            return 0;
        }
    }

    // Program.cs: só uma lupa por vez. A segunda avisa e traz a janela da primeira.
    Glassbrick::SingleInstance instance;
    if (!instance.acquire()) {
        std::cout << "Outra instância do Glassbrick já está rodando";
        if (instance.otherPid() > 0) {
            std::cout << " (pid " << instance.otherPid() << "); abrindo as configurações dela";
            kill(instance.otherPid(), SIGUSR1);
        }
        std::cout << ".\n";
        return 0;
    }

    printBanner();

    Glassbrick::SettingsManager settings(configPath);
    std::vector<std::string> warnings;
    if (settings.load(warnings)) {
        std::cout << "[Config] Configurações carregadas de " << configPath << "\n";
    } else {
        // Primeira execução: aproveita o arquivo do formato antigo, se existir
        const std::string legacy = "glassbrick_config.json";
        if (!customConfig && settings.loadFrom(legacy, warnings)) {
            std::cout << "[Config] Migrando " << legacy << " (formato antigo) para " << configPath << "\n";
        } else {
            std::cout << "[Config] Criando " << configPath << " com os valores padrão\n";
        }
        if (!settings.save()) {
            std::cerr << "[Config] Aviso: não foi possível gravar " << configPath << "\n";
        }
    }
    for (const auto& w : warnings) std::cerr << "[Config] Aviso: " << w << "\n";

    // Opcional: acessibilidade da sessão ligada para navegadores (AppAccessibility)
    Glassbrick::A11yStatus a11y;
    a11y.apply(settings.getSettings().AppAccessibility);

    // Parte X11 da lupa (janela, extensões). O OpenGL é criado depois, na
    // thread do UpdateLoop, que é a única que desenha.
    Glassbrick::MagnifierLinux magnifier;
    magnifier.initialise();
    if (!magnifier.isReady()) return 1;

    Glassbrick::Camera camera(settings, magnifier, magnifier.getWidth(), magnifier.getHeight());
    // Chamado dentro de Camera::updateLoop, na thread do loop
    camera.setCursorProvider([&magnifier]() { return magnifier.queryPointer(); });
    camera.setCursorWarper([&magnifier](int x, int y) { magnifier.warpPointer(x, y); });

    Glassbrick::Colour colour(settings, magnifier);
    Glassbrick::Reading reading(camera, settings, magnifier, magnifier.getWidth(), magnifier.getHeight());
    reading.setCursorFunctions([&magnifier]() { return magnifier.queryPointer(); },
                               [&magnifier](int x, int y) { magnifier.warpPointer(x, y); });

    // Monitores (XRandR): Screen.AllScreens / PrimaryScreen do original
    {
        const auto monitors = magnifier.queryMonitors();
        camera.setMonitors(monitors.rects, monitors.primary);
        reading.setMonitors(monitors.rects, monitors.primary);
        std::cout << "[Glassbrick] Monitores: " << monitors.rects.size() << "\n";
    }
    // O que este ambiente desenha fora de janelas X não aparece na lupa
    for (const auto& w : Glassbrick::DesktopInfo::query().warnings()) {
        std::cout << "[Glassbrick] Aviso: " << w << " (detalhes: ./glassbrick --diagnostico)\n";
    }
    magnifier.setMonitorsChangedCallback([&camera, &reading](const Glassbrick::MagnifierLinux::Monitors& m) {
        std::cout << "[Glassbrick] Monitores mudaram: " << m.rects.size() << "\n";
        camera.setMonitors(m.rects, m.primary);
        reading.setMonitors(m.rects, m.primary);
    });

    Glassbrick::InputManager input(settings, camera, colour, reading);
    input.start();

    // Foco de teclado / caret / seleção de outros programas (TrackFocus, TrackTyping)
    Glassbrick::FocusTracker focusTracker(camera, settings);
    focusTracker.start();

    // Evento Modified do original: ao recarregar a configuração, as cores
    // mudam, a câmera recalcula e os atalhos são registrados de novo
    settings.addModifiedListener([&]() {
        colour.changeMatrixTargets();
        camera.setNeedsUpdate(true);
        input.requestRegrab();
        a11y.apply(settings.getSettings().AppAccessibility);
    });

    Glassbrick::UpdateLoop loop(settings, magnifier, camera, colour, reading);
    loop.start();

    pid_t uiPid = withUi ? launchSettingsUi(configPath) : -1;

    std::cout << "[Glassbrick] Em execução. Ctrl+C ou 'q' para sair.\n";

    // Comandos pelo terminal (útil para testes sem atalhos)
    std::thread cliThread([&]() {
        if (!isatty(STDIN_FILENO)) return;
        std::string line;
        while (g_keepRunning && std::cin >> line) {
            if (line == "+" || line == "=") input.triggerAction(Glassbrick::InputAction::ZoomIn);
            else if (line == "-") input.triggerAction(Glassbrick::InputAction::ZoomOut);
            else if (line == "z") input.triggerAction(Glassbrick::InputAction::ZoomOnToggle);
            else if (line == "t") input.triggerAction(Glassbrick::InputAction::TrackModeCycle);
            else if (line == "c") input.triggerAction(Glassbrick::InputAction::ColourModeCycle);
            else if (line == "r") input.triggerAction(Glassbrick::InputAction::ReadingModeToggle);
            else if (line == "q") { g_keepRunning = false; break; }
        }
    });
    cliThread.detach();

    while (g_keepRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_reloadRequested.exchange(false)) settings.requestReload();
        if (g_showSettings.exchange(false)) {
            if (uiPid > 0) kill(uiPid, SIGUSR1);
            else std::cout << "[Glassbrick] Outra instância foi aberta; esta continua rodando (sem interface).\n";
        }
        // Se a interface fechar sozinha (ex.: erro), recolhe o processo
        if (uiPid > 0 && waitpid(uiPid, nullptr, WNOHANG) == uiPid) {
            std::cerr << "[Interface] A interface foi fechada.\n";
            uiPid = -1;
        }
    }

    std::cout << "[Glassbrick] Encerrando...\n";
    if (uiPid > 0 && kill(uiPid, SIGTERM) == 0) waitpid(uiPid, nullptr, 0);
    focusTracker.stop();
    input.stop();
    loop.stop();          // anima a volta ao normal (1 s) e libera o OpenGL
    magnifier.shutdown(); // no-op se o loop já liberou
    std::cout << "[Glassbrick] Encerrado.\n";
    return 0;
}
