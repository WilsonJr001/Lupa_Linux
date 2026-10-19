#pragma once

#include <string>
#include <vector>

namespace Glassbrick {

// O que a lupa consegue enxergar do ambiente gráfico. A lupa só desenha janelas
// X (e o papel de parede publicado em _XROOTPMAP_ID). Em ambientes como
// Cinnamon e GNOME Shell, o painel, o menu e o papel de parede são desenhados
// dentro do próprio compositor, sem janela X, e por isso não aparecem na lupa.
struct DesktopInfo {
    bool connected = false;
    std::string session;          // $XDG_CURRENT_DESKTOP
    std::string windowManager;    // _NET_WM_NAME do gerenciador de janelas
    bool compositing = false;     // há um compositor (dono de _NET_WM_CM_S0)
    int clients = 0;              // janelas em _NET_CLIENT_LIST
    int hiddenClients = 0;        // minimizadas (WM_STATE Iconic / _NET_WM_STATE_HIDDEN)
    int docks = 0;                // painéis / barras de tarefas (_NET_WM_WINDOW_TYPE_DOCK)
    int desktops = 0;             // janelas de área de trabalho (_NET_WM_WINDOW_TYPE_DESKTOP)
    bool rootPixmap = false;      // papel de parede publicado em _XROOTPMAP_ID / ESETROOT_PMAP_ID

    static DesktopInfo query();

    // Avisos sobre o que não vai aparecer na lupa (vazio se estiver tudo bem)
    std::vector<std::string> warnings() const;
    std::string report() const;
};

} // namespace Glassbrick
