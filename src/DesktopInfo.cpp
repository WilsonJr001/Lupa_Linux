#include "DesktopInfo.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <sstream>

namespace Glassbrick {

namespace {

// Lê uma propriedade inteira; devolve os itens (32 bits vêm como long no Xlib)
std::vector<unsigned long> readList(Display* d, Window w, const char* name, Atom type) {
    std::vector<unsigned long> out;
    Atom actual = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, XInternAtom(d, name, False), 0, 4096, False, type, &actual, &format, &count,
                           &after, &data) == Success && data) {
        if (format == 32) {
            const unsigned long* items = reinterpret_cast<const unsigned long*>(data);
            out.assign(items, items + count);
        }
        XFree(data);
    }
    return out;
}

std::string readUtf8(Display* d, Window w, const char* name) {
    std::string out;
    Atom actual = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, XInternAtom(d, name, False), 0, 256, False, AnyPropertyType, &actual, &format,
                           &count, &after, &data) == Success && data) {
        if (format == 8) out.assign(reinterpret_cast<const char*>(data), count);
        XFree(data);
    }
    return out;
}

bool contains(const std::vector<unsigned long>& list, Atom atom) {
    for (unsigned long a : list) {
        if (a == atom) return true;
    }
    return false;
}

int ignoreErrors(Display*, XErrorEvent*) { return 0; } // janela que sumiu no meio da leitura

} // namespace

DesktopInfo DesktopInfo::query() {
    DesktopInfo info;
    if (const char* s = std::getenv("XDG_CURRENT_DESKTOP")) info.session = s;
    Display* d = XOpenDisplay(nullptr);
    if (!d) return info;
    info.connected = true;
    auto previous = XSetErrorHandler(ignoreErrors);
    Window root = DefaultRootWindow(d);

    auto check = readList(d, root, "_NET_SUPPORTING_WM_CHECK", XA_WINDOW);
    if (!check.empty()) info.windowManager = readUtf8(d, check[0], "_NET_WM_NAME");

    std::string cm = "_NET_WM_CM_S" + std::to_string(DefaultScreen(d));
    info.compositing = XGetSelectionOwner(d, XInternAtom(d, cm.c_str(), False)) != None;

    info.rootPixmap = !readList(d, root, "_XROOTPMAP_ID", XA_PIXMAP).empty() ||
                      !readList(d, root, "ESETROOT_PMAP_ID", XA_PIXMAP).empty();

    const Atom dock = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    const Atom desktop = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    const Atom hidden = XInternAtom(d, "_NET_WM_STATE_HIDDEN", False);
    for (unsigned long w : readList(d, root, "_NET_CLIENT_LIST", XA_WINDOW)) {
        ++info.clients;
        auto types = readList(d, w, "_NET_WM_WINDOW_TYPE", XA_ATOM);
        if (contains(types, dock)) ++info.docks;
        if (contains(types, desktop)) ++info.desktops;
        auto wmState = readList(d, w, "WM_STATE", AnyPropertyType);
        if ((!wmState.empty() && wmState[0] == IconicState) ||
            contains(readList(d, w, "_NET_WM_STATE", XA_ATOM), hidden)) {
            ++info.hiddenClients;
        }
    }

    XSync(d, False);
    XSetErrorHandler(previous);
    XCloseDisplay(d);
    return info;
}

std::vector<std::string> DesktopInfo::warnings() const {
    std::vector<std::string> out;
    if (!connected || clients == 0) return out; // sem gerenciador de janelas: nada a dizer
    if (docks == 0) {
        out.push_back("nenhum painel/barra de tarefas é uma janela X. Se houver um (Cinnamon, GNOME Shell), "
                      "ele é desenhado pelo compositor e não aparece na lupa.");
    }
    if (desktops == 0 && !rootPixmap) {
        out.push_back("o papel de parede não é uma janela nem está em _XROOTPMAP_ID; na lupa o fundo fica preto.");
    }
    return out;
}

std::string DesktopInfo::report() const {
    std::ostringstream o;
    o << "Diagnóstico do ambiente gráfico (para relatar problemas)\n";
    if (!connected) {
        o << "  Não foi possível abrir o display X11 (DISPLAY=" << (std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "")
          << ").\n";
        return o.str();
    }
    const char* sessionType = std::getenv("XDG_SESSION_TYPE");
    o << "  Sessão: " << (session.empty() ? "?" : session) << " (" << (sessionType ? sessionType : "?") << ")\n"
      << "  Gerenciador de janelas: " << (windowManager.empty() ? "nenhum/desconhecido" : windowManager) << "\n"
      << "  Compositor ativo: " << (compositing ? "sim" : "não") << "\n"
      << "  Janelas de aplicativos: " << clients << " (minimizadas: " << hiddenClients << ")\n"
      << "  Painéis (dock): " << docks << " | Janelas de área de trabalho: " << desktops << "\n"
      << "  Papel de parede em _XROOTPMAP_ID: " << (rootPixmap ? "sim" : "não") << "\n";
    auto w = warnings();
    if (w.empty()) o << "  Tudo o que aparece na tela é visível para a lupa.\n";
    for (const auto& line : w) o << "  Aviso: " << line << "\n";
    return o.str();
}

} // namespace Glassbrick
