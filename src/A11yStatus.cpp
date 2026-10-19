#include "A11yStatus.h"
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace LupaLinux {

// Sem os headers de D-Bus nesta máquina, usa o utilitário dbus-send
std::optional<bool> A11yStatus::isEnabled() {
    FILE* p = popen("dbus-send --session --print-reply --dest=org.a11y.Bus /org/a11y/bus "
                    "org.freedesktop.DBus.Properties.Get string:org.a11y.Status string:IsEnabled 2>/dev/null",
                    "r");
    if (!p) return std::nullopt;
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    if (pclose(p) != 0) return std::nullopt;
    if (out.find("boolean true") != std::string::npos) return true;
    if (out.find("boolean false") != std::string::npos) return false;
    return std::nullopt;
}

bool A11yStatus::setEnabled(bool value) {
    std::vector<std::string> args = {
        "dbus-send", "--session", "--print-reply", "--dest=org.a11y.Bus", "/org/a11y/bus",
        "org.freedesktop.DBus.Properties.Set", "string:org.a11y.Status", "string:IsEnabled",
        value ? "variant:boolean:true" : "variant:boolean:false"};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "dbus-send", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void A11yStatus::apply(bool wanted) {
    if (wanted && !_changedByUs) {
        auto current = isEnabled();
        if (current == false && setEnabled(true)) {
            _changedByUs = true;
            std::cout << "[Acessibilidade] Ligada para os navegadores. Reabra o Firefox para que a lupa "
                         "siga o foco dentro das páginas.\n";
        } else if (!current) {
            std::cerr << "[Acessibilidade] Aviso: não foi possível ligar (dbus-send / org.a11y.Bus).\n";
        }
    } else if (!wanted && _changedByUs) {
        if (setEnabled(false)) std::cout << "[Acessibilidade] Sinal de acessibilidade restaurado (desligado).\n";
        _changedByUs = false;
    }
}

} // namespace LupaLinux
