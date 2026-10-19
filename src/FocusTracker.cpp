#include "FocusTracker.h"
#include "AtspiCompat.h"
#include "Camera.h"
#include "LupaSettings.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace LupaLinux {

// Os callbacks do AT-SPI são funções C sem contexto
static Camera* s_camera = nullptr;
// Segurado ao chamar a Camera, que lê as configurações (ver SettingsManager::mutex)
static std::recursive_mutex* s_settingsMutex = nullptr;

static bool debugEnabled() {
    static const bool enabled = std::getenv("LUPA_LINUX_DEBUG") != nullptr;
    return enabled;
}

static void clearError(GError*& error) {
    if (error) {
        g_error_free(error);
        error = nullptr;
    }
}

static bool takeRect(AtspiRect* r, Rect2D& out) {
    if (!r) return false;
    out = Rect2D(r->x, r->y, r->width, r->height);
    g_free(r);
    // AT-SPI devolve -1/0 quando não sabe a posição
    return !(out.x < 0 && out.y < 0) && (out.width > 0 || out.height > 0);
}

static void onFocusEvent(const AtspiEvent* event) {
    if (!event || !event->source || event->detail1 != 1) return; // só ao ganhar foco
    AtspiComponent* component = atspi_accessible_get_component_iface(event->source);
    if (!component) return;
    GError* error = nullptr;
    Rect2D rect;
    bool ok = takeRect(atspi_component_get_extents(component, ATSPI_COORD_TYPE_SCREEN, &error), rect);
    clearError(error);
    g_object_unref(component);
    if (!ok) return;
    if (debugEnabled()) {
        std::cerr << "[Foco] elemento " << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height << "\n";
    }
    std::lock_guard<std::recursive_mutex> lock(*s_settingsMutex);
    s_camera->onGuiFocusChanged(rect);
}

static void onCaretEvent(const AtspiEvent* event) {
    if (!event || !event->source) return;
    AtspiText* text = atspi_accessible_get_text_iface(event->source);
    if (!text) return;
    GError* error = nullptr;
    const int offset = event->detail1;
    Rect2D rect;
    bool ok = takeRect(atspi_text_get_character_extents(text, offset, ATSPI_COORD_TYPE_SCREEN, &error), rect);
    clearError(error);
    if (!ok && offset > 0) {
        // Caret no fim do texto: não há caractere nessa posição; usa o
        // lado direito do caractere anterior
        if (takeRect(atspi_text_get_character_extents(text, offset - 1, ATSPI_COORD_TYPE_SCREEN, &error), rect)) {
            rect.x += rect.width;
            rect.width = 0;
            ok = true;
        }
        clearError(error);
    }
    g_object_unref(text);
    if (!ok) return;
    if (debugEnabled()) {
        std::cerr << "[Foco] caret " << rect.x << "," << rect.y << " (offset " << offset << ")\n";
    }
    std::lock_guard<std::recursive_mutex> lock(*s_settingsMutex);
    s_camera->onCaretMoved(rect);
}

static void onSelectionEvent(const AtspiEvent* event) {
    if (!event || !event->source) return;
    AtspiText* text = atspi_accessible_get_text_iface(event->source);
    if (!text) return;
    GError* error = nullptr;
    Rect2D rect;
    bool ok = false;
    if (atspi_text_get_n_selections(text, &error) > 0) {
        clearError(error);
        AtspiRange* range = atspi_text_get_selection(text, 0, &error);
        clearError(error);
        if (range) {
            if (range->start_offset != range->end_offset) {
                ok = takeRect(atspi_text_get_range_extents(text, range->start_offset, range->end_offset,
                                                           ATSPI_COORD_TYPE_SCREEN, &error), rect);
                clearError(error);
            }
            g_free(range);
        }
    }
    clearError(error);
    g_object_unref(text);
    if (!ok) return;
    if (debugEnabled()) {
        std::cerr << "[Foco] seleção " << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height << "\n";
    }
    std::lock_guard<std::recursive_mutex> lock(*s_settingsMutex);
    s_camera->onTextSelectionChanged(rect);
}

FocusTracker::FocusTracker(Camera& camera, SettingsManager& settings) : _camera(camera), _settings(settings) {}

FocusTracker::~FocusTracker() {
    stop();
}

void FocusTracker::start() {
    if (_running) return;
    _running = true;
    _loopStarted = false;
    _finished = false;
    s_camera = &_camera;
    s_settingsMutex = &_settings.mutex();
    _thread = std::thread(&FocusTracker::run, this);
}

void FocusTracker::stop() {
    if (!_running) return;
    _running = false;
    // g_main_loop_quit é seguro entre threads; repete porque o loop pode
    // ainda não ter começado a rodar quando o primeiro pedido chega
    while (!_finished) {
        if (_loopStarted) atspi_event_quit();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (_thread.joinable()) _thread.join();
}

void FocusTracker::run() {
    if (atspi_init() < 0) {
        std::cerr << "[FocusTracker] Aviso: AT-SPI indisponível; foco de teclado desativado.\n";
        _finished = true;
        return;
    }

    struct Subscription {
        const char* event;
        AtspiEventListenerSimpleCB callback;
        AtspiEventListener* listener;
    } subs[] = {
        { "object:state-changed:focused", onFocusEvent, nullptr },
        { "object:text-caret-moved", onCaretEvent, nullptr },
        { "object:text-selection-changed", onSelectionEvent, nullptr },
    };

    for (auto& sub : subs) {
        GError* error = nullptr;
        sub.listener = atspi_event_listener_new_simple(sub.callback, nullptr);
        if (!atspi_event_listener_register(sub.listener, sub.event, &error)) {
            std::cerr << "[FocusTracker] Aviso: não foi possível ouvir " << sub.event << "\n";
        }
        clearError(error);
    }
    std::cout << "[FocusTracker] Acompanhando foco e caret via AT-SPI.\n";

    if (_running) {
        _loopStarted = true;
        atspi_event_main();
    }

    for (auto& sub : subs) {
        if (!sub.listener) continue;
        GError* error = nullptr;
        atspi_event_listener_deregister(sub.listener, sub.event, &error);
        clearError(error);
        g_object_unref(sub.listener);
    }
    atspi_exit();
    _finished = true;
}

} // namespace LupaLinux
