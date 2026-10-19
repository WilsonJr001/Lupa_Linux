// Testes da Camera sem X11: uma lupa falsa registra o que seria desenhado e o
// cursor é simulado. Rode com `make test`.

#include "Camera.h"
#include "LupaSettings.h"
#include "IMagnifier.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace LupaLinux;

namespace {

struct FakeMagnifier : IMagnifier {
    float zoom = 1.0f, x = 0.0f, y = 0.0f;
    void initialise() override {}
    void update(float z, float vx, float vy) override { zoom = z; x = vx; y = vy; }
    void updateColour(const std::array<float, 25>&) override {}
    void setReadingOverlay(const ReadingOverlay&) override {}
    void render() override {}
    void shutdown() override {}
};

constexpr int W = 1366, H = 768;

struct Fixture;
const Fixture* g_last = nullptr;

struct Fixture {
    SettingsManager settings{"/nonexistent/lupa_linux_test.conf"};
    FakeMagnifier mag;
    Camera camera{settings, mag, W, H};
    Point2D cursor{W / 2, H / 2};
    Point2D warpedTo{-1, -1};

    Fixture() {
        g_last = this;
        camera.setCursorProvider([this]() { return cursor; });
        camera.setCursorWarper([this](int px, int py) { warpedTo = {px, py}; cursor = {px, py}; });
    }
    void frames(int n) { for (int i = 0; i < n; ++i) camera.updateLoop(1.0); }
    // Centro da área ampliada, em pixels de tela
    double centreX() const { return mag.x + W / mag.zoom / 2.0; }
    double centreY() const { return mag.y + H / mag.zoom / 2.0; }
};

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FALHOU", what.c_str());
    if (!ok && g_last) std::printf("         centro=(%.1f, %.1f) zoom=%.2f\n", g_last->centreX(), g_last->centreY(), g_last->mag.zoom);
    if (!ok) ++g_failures;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

void testZoomSteps() {
    std::puts("Zoom: Alt+1 x4 => nível 1.0 => 2x");
    Fixture f;
    for (int i = 0; i < 4; ++i) f.camera.zoomIn(1.0f);
    f.frames(240);
    check(near(f.camera.getZoomLevel(), 1.0, 1e-6), "nível de zoom = 1.0");
    check(near(f.mag.zoom, 2.0, 1e-4), "fator enviado à lupa = 2.0");

    f.camera.zoomOnToggle();
    f.frames(240);
    check(near(f.mag.zoom, 1.0, 1e-4), "Alt+` desliga (fator 1)");
    f.camera.zoomOnToggle();
    f.frames(240);
    check(near(f.mag.zoom, 2.0, 1e-4), "Alt+` religa no nível salvo (2x)");
}

void testCentreFollowsCursor() {
    std::puts("Rastreamento Centre segue o cursor e respeita as bordas");
    Fixture f;
    f.camera.zoomIn(4.0f);
    f.cursor = {700, 400};
    f.frames(300);
    check(near(f.centreX(), 700, 2) && near(f.centreY(), 400, 2), "vista centrada no cursor");

    f.cursor = {5, 5};
    f.frames(300);
    check(near(f.mag.x, 0, 0.5) && near(f.mag.y, 0, 0.5), "ClampEdge: vista não passa do canto");
}

void testKeyboardFocusFollowsCaret() {
    std::puts("Modo Teclado: tecla sem mexer o mouse => segue o caret");
    Fixture f;
    f.camera.zoomIn(4.0f);
    f.cursor = {400, 300};
    f.frames(300);

    f.camera.onKeyDown(38, false);                    // tecla 'a'
    f.camera.onCaretMoved(Rect2D(1000, 500, 2, 16));  // AT-SPI: caret andou
    f.frames(300);
    check(near(f.centreX(), 1000, 3) && near(f.centreY(), 500, 3), "vista centrada no caret");

    f.camera.onTextSelectionChanged(Rect2D(400, 200, 100, 20));
    f.frames(300);
    check(near(f.centreX(), 450, 3) && near(f.centreY(), 210, 3), "seleção de texto: centro da seleção");

    f.cursor = {410, 300};                            // mouse mexeu
    f.frames(300);
    check(near(f.centreX(), 410, 3) && near(f.centreY(), 300, 3), "mouse mexeu => volta ao cursor");
    check(f.warpedTo.x == -1, "sem Alt, o ponteiro não é movido");
}

void testAltWarpsPointerToFocus() {
    std::puts("Alt pressionado ao voltar para o mouse => ponteiro pula para o foco");
    Fixture f;
    f.camera.zoomIn(4.0f);
    f.cursor = {400, 300};
    f.frames(300);
    f.camera.onKeyDown(64, true);                     // Alt
    f.camera.onGuiFocusChanged(Rect2D(800, 500, 200, 40));
    f.frames(300);
    check(near(f.centreX(), 900, 3) && near(f.centreY(), 520, 3), "segue o elemento focado (centro)");

    f.camera.onKeyDown(64, true);                     // Alt ainda pressionado no quadro da troca
    f.cursor = {405, 300};
    f.frames(2);
    check(f.warpedTo == Point2D(900, 520), "ponteiro movido para o foco");
}

void testTrackFocusDisabled() {
    std::puts("TrackFocus/TrackTyping desligados => ignora eventos de foco");
    Fixture f;
    f.settings.getSettingsMutable().TrackFocus = false;
    f.settings.getSettingsMutable().TrackTyping = false;
    f.camera.zoomIn(4.0f);
    f.cursor = {400, 300};
    f.frames(300);
    f.camera.onKeyDown(38, false);
    f.camera.onCaretMoved(Rect2D(1000, 500, 2, 16));
    f.camera.onGuiFocusChanged(Rect2D(800, 500, 200, 40));
    f.frames(300);
    check(near(f.centreX(), 400, 3) && near(f.centreY(), 300, 3), "continua no cursor");
}

void testTwoMonitors() {
    std::puts("Dois monitores (principal 1366x768 + 1920x1080 à direita)");
    Fixture f;
    const Rect2D left(0, 0, 1366, 768), right(1366, 0, 1920, 1080);
    f.camera.setMonitors({left, right}, 0);
    f.camera.zoomIn(4.0f);
    // Ponto da tela virtual que aparece no centro de um monitor (transformação de tela cheia)
    auto shownAt = [&](double px, double py) {
        return std::make_pair(f.mag.x + px / f.mag.zoom, f.mag.y + py / f.mag.zoom);
    };

    f.cursor = {600, 400};
    f.frames(300);
    auto c1 = shownAt(left.x + left.width / 2, left.y + left.height / 2);
    check(near(c1.first, 600, 3) && near(c1.second, 400, 3), "no monitor principal: centro dele mostra o cursor");

    f.cursor = {2200, 500};
    f.frames(400);
    auto c2 = shownAt(right.x + right.width / 2, right.y + right.height / 2);
    check(near(c2.first, 2200, 3) && near(c2.second, 500, 3), "cursor no segundo monitor: a vista vai para ele");

    f.cursor = {1400, 30};
    f.frames(400);
    auto edge = shownAt(right.x, right.y);
    check(edge.first >= right.x - 0.5 && edge.second >= right.y - 0.5,
          "perto da borda: o segundo monitor não mostra conteúdo do primeiro");

    f.camera.setMonitors({left}, 0);  // segundo monitor desconectado
    f.cursor = {700, 300};
    f.frames(400);
    auto c3 = shownAt(left.x + left.width / 2, left.y + left.height / 2);
    check(near(c3.first, 700, 3) && near(c3.second, 300, 3), "monitor desconectado: volta ao principal");
}

void testTrackScreenOff() {
    std::puts("TrackScreen desligado: a vista fica sempre no monitor principal");
    Fixture f;
    f.settings.getSettingsMutable().TrackScreen = false;
    const Rect2D left(0, 0, 1366, 768), right(1366, 0, 1920, 1080);
    f.camera.setMonitors({left, right}, 0);
    f.camera.zoomIn(4.0f);
    f.cursor = {2200, 500};
    f.frames(400);
    const double shownX = f.mag.x + (left.x + left.width / 2) / f.mag.zoom;
    const double shownY = f.mag.y + (left.y + left.height / 2) / f.mag.zoom;
    check(near(shownX, 2200, 3) && near(shownY, 500, 3), "o monitor principal mostra o cursor que está no outro");
}

} // namespace

int main() {
    testZoomSteps();
    testCentreFollowsCursor();
    testKeyboardFocusFollowsCaret();
    testAltWarpsPointerToFocus();
    testTrackFocusDisabled();
    testTwoMonitors();
    testTrackScreenOff();
    if (g_failures) {
        std::printf("\n%d verificação(ões) falharam\n", g_failures);
        return 1;
    }
    std::puts("\nTodos os testes passaram");
    return 0;
}
