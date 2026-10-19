// Testes da leitura/escrita do arquivo de configuração. Rode com `make test`.

#include "GlassbrickSettings.h"
#include <X11/keysym.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace Glassbrick;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FALHOU", what.c_str());
    if (!ok) ++g_failures;
}

std::string tempPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("glassbrick_test_" + std::to_string(getpid()) + "_" + name)).string();
}

std::string readAll(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path) << content;
}

bool hasWarningContaining(const std::vector<std::string>& w, const std::string& text) {
    for (const auto& s : w) if (s.find(text) != std::string::npos) return true;
    return false;
}

bool bindingIs(const GlassbrickSettings& s, InputAction a, Modifiers m, unsigned int sym) {
    const KeyBinding* b = s.bindingFor(a);
    return b && b->modifiers == m && b->keySym == sym;
}

void testDefaults() {
    std::puts("Padrões");
    GlassbrickSettings s;
    check(s.Bindings.size() == 8, "8 atalhos, um por ação");
    check(bindingIs(s, InputAction::ZoomOnToggle, Modifiers::Alt, XK_grave), "ZoomOnToggle = Alt+grave");
    check(bindingIs(s, InputAction::ColourModeCycle, Modifiers::Control | Modifiers::Alt, XK_grave),
          "ColourModeCycle = Ctrl+Alt+grave (trocado: não prende o apóstrofo no ABNT2)");
    check(s.CurrentTrackModeActual.getMode() == TrackMode::Centre, "rastreamento inicial Centre");
}

void testRoundTrip() {
    std::puts("Salvar e ler de volta preserva tudo");
    GlassbrickSettings a;
    a.RefreshRate = 75;
    a.ZoomSpeedActual.setMode(4);
    a.ZoomAmountActual.setModeEnabled(3, true);
    a.CurrentTrackModeActual.setModeEnabled(TrackMode::KeyCombo, true);
    a.CurrentTrackModeActual.setMode(TrackMode::KeyCombo);
    a.CurrentColourModeActual.setModeEnabled(ColourMode::Contrast, true);
    a.ColourContrast = ContrastMode::BlueAndYellow;
    a.ColourTint = TintColour::Rose;
    a.ColourInvertSaturation = 0.25f;
    a.TrackFocus = false;
    a.MouseModifiers = Modifiers::Control | Modifiers::Shift;
    a.setBinding(InputAction::ReadingModeToggle, Modifiers::Windows, XK_F5);
    a.setBinding(InputAction::ZoomAmountCycle, Modifiers::NoModifier, 0);

    const std::string p1 = tempPath("a.conf"), p2 = tempPath("b.conf");
    check(a.saveToFile(p1), "salvou o arquivo");

    GlassbrickSettings b;
    std::vector<std::string> w;
    check(b.loadFromFile(p1, w), "leu o arquivo");
    check(w.empty(), "sem avisos ao ler o próprio arquivo" + (w.empty() ? "" : " (" + w.front() + ")"));
    b.saveToFile(p2);
    check(readAll(p1) == readAll(p2), "salvar de novo gera exatamente o mesmo texto");
    check(b.RefreshRate == 75 && b.ZoomSpeedActual.getMode() == 4, "valores numéricos");
    check(b.CurrentTrackModeActual.getMode() == TrackMode::KeyCombo &&
              b.CurrentTrackModeActual.isModeEnabled(TrackMode::KeyCombo) &&
              !b.CurrentTrackModeActual.isModeEnabled(TrackMode::Consistent),
          "modo e lista de modos do ciclo");
    check(b.ColourContrast == ContrastMode::BlueAndYellow && b.ColourTint == TintColour::Rose, "enums de cor");
    check(b.MouseModifiers == (Modifiers::Control | Modifiers::Shift), "MouseModifiers = Ctrl+Shift");
    check(bindingIs(b, InputAction::ReadingModeToggle, Modifiers::Windows, XK_F5), "atalho Super+F5");
    check(b.bindingFor(InputAction::ZoomAmountCycle) == nullptr, "atalho desligado (nenhum)");
    std::filesystem::remove(p1);
    std::filesystem::remove(p2);
}

void testBindingParsing() {
    std::puts("Texto de atalhos");
    std::string err;
    auto b1 = parseBinding("ctrl + ALT + Grave", err);
    check(b1 && b1->first == (Modifiers::Control | Modifiers::Alt) && b1->second == XK_grave,
          "\"ctrl + ALT + Grave\" (maiúsculas e espaços)");
    auto b2 = parseBinding("Alt+R", err);
    check(b2 && b2->second == XK_r, "\"Alt+R\" = mesma tecla que Alt+r");
    auto b3 = parseBinding("Super+F12", err);
    check(b3 && b3->first == Modifiers::Windows && b3->second == XK_F12, "\"Super+F12\"");
    auto b4 = parseBinding("nenhum", err);
    check(b4 && b4->second == 0, "\"nenhum\" desliga");
    check(!parseBinding("Hyper+a", err), "modificador inválido é rejeitado");
    check(!parseBinding("Alt+teclaquenaoexiste", err), "tecla inválida é rejeitada");
    check(!parseBinding("Ctrl+Alt", err), "só modificadores é rejeitado");
    auto b5 = parseBinding("Ctrl+Alt+ç", err);
    check(b5 && b5->second == XK_ccedilla, "\"Ctrl+Alt+ç\" (UTF-8) = tecla ç do ABNT2");
    auto b6 = parseBinding("Alt+Ç", err);
    check(b6 && b6->second == XK_ccedilla, "\"Alt+Ç\" = mesma tecla que Alt+ç");
    auto b7 = parseBinding("Alt+'", err);
    check(b7 && b7->second == XK_apostrophe, "\"Alt+'\"");
    check(bindingToString(Modifiers::Control | Modifiers::Alt, XK_grave) == "Ctrl+Alt+grave", "escrita Ctrl+Alt+grave");
}

void testInvalidValues() {
    std::puts("Valores inválidos geram aviso e mantêm o anterior");
    const std::string p = tempPath("bad.conf");
    writeFile(p,
              "[Geral]\n"
              "RefreshRate = 9999\n"
              "[Rastreamento]\n"
              "TrackMode = Diagonal\n"
              "ClampEdge = talvez\n"
              "TrackBorderSizeBorder = 0,75   # vírgula decimal é aceita\n"
              "OpcaoQueNaoExiste = 1\n"
              "[Atalhos]\n"
              "ZoomIn = Alt+teclaquenaoexiste\n"
              "ZoomOut = Alt+1\n"
              "AcaoInventada = Alt+x\n"
              "linha sem igual\n");
    GlassbrickSettings s;
    std::vector<std::string> w;
    s.loadFromFile(p, w);
    check(s.RefreshRate == 60 && hasWarningContaining(w, "RefreshRate"), "RefreshRate fora do intervalo");
    check(s.CurrentTrackModeActual.getMode() == TrackMode::Centre && hasWarningContaining(w, "TrackMode"), "modo inexistente");
    check(s.ClampEdge && hasWarningContaining(w, "ClampEdge"), "booleano inválido");
    check(s.TrackBorderSizeBorder == 0.75f, "vírgula decimal aceita (0,75)");
    check(hasWarningContaining(w, "OpcaoQueNaoExiste"), "opção desconhecida avisada");
    check(bindingIs(s, InputAction::ZoomIn, Modifiers::Alt, XK_1) && hasWarningContaining(w, "ZoomIn"),
          "atalho inválido mantém o padrão");
    check(hasWarningContaining(w, "mesmo atalho"), "dois atalhos iguais (ZoomIn e ZoomOut = Alt+1) avisados");
    check(hasWarningContaining(w, "AcaoInventada"), "ação desconhecida avisada");
    check(hasWarningContaining(w, ":12:"), "aviso informa o número da linha");
    std::filesystem::remove(p);
}

void testLegacyFormat() {
    std::puts("Formato antigo (glassbrick_config.json)");
    const std::string p = tempPath("legacy.json");
    writeFile(p,
              "# Configuração Glassbrick Clone (Lupa Linux)\n"
              "RefreshRate=60\nTrackSpeedIndex=3\nColourTransitionIndex=1\nClampEdge=false\n"
              "PushEdge=true\nMouseAccelerate=true\nColourTintAmount=0.5\n"
              "ColourInvertSaturation=0.4\nTrackBorderSizeBorder=0.5\n");
    GlassbrickSettings s;
    std::vector<std::string> w;
    check(s.loadFromFile(p, w), "leu o arquivo antigo");
    check(w.empty(), "sem avisos");
    check(s.TrackSpeedIndex == 3 && s.ColourTransitionIndex == 1 && !s.ClampEdge && s.ColourInvertSaturation == 0.4f,
          "valores migrados");
    std::filesystem::remove(p);
}

void testManagerCreatesDirectory() {
    std::puts("SettingsManager");
    const std::string dir = tempPath("dir");
    const std::string p = dir + "/sub/glassbrick.conf";
    SettingsManager m(p);
    std::vector<std::string> w;
    check(!m.load(w), "arquivo inexistente => load() falso");
    check(m.save() && std::filesystem::exists(p), "save() cria as pastas");
    std::filesystem::remove_all(dir);

    setenv("XDG_CONFIG_HOME", "/tmp/xdgteste", 1);
    check(SettingsManager::defaultConfigPath() == "/tmp/xdgteste/glassbrick/glassbrick.conf", "respeita $XDG_CONFIG_HOME");
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/fulano", 1);
    check(SettingsManager::defaultConfigPath() == "/home/fulano/.config/glassbrick/glassbrick.conf", "padrão ~/.config");
}

void testReload() {
    std::puts("Recarregar sem reiniciar (SIGHUP)");
    const std::string p = tempPath("reload.conf");
    SettingsManager m(p);
    std::vector<std::string> w;
    m.save();
    m.load(w);
    int modified = 0;
    m.addModifiedListener([&]() { ++modified; });

    check(!m.applyPendingReload(w) && modified == 0, "sem pedido de recarga, nada muda");

    std::string text = readAll(p);
    auto replaceLine = [&](const std::string& key, const std::string& value) {
        auto pos = text.find("\n" + key + " = ");
        auto end = text.find('\n', pos + 1);
        text.replace(pos + 1, end - pos - 1, key + " = " + value);
    };
    replaceLine("RefreshRate", "30");
    replaceLine("ColourModeCycle", "Ctrl+Shift+F12");
    writeFile(p, text);

    m.requestReload();
    check(m.applyPendingReload(w) && modified == 1, "recarregou e avisou os ouvintes (evento Modified)");
    check(m.getSettings().RefreshRate == 30, "novo valor aplicado");
    check(bindingIs(m.getSettings(), InputAction::ColourModeCycle, Modifiers::Control | Modifiers::Shift, XK_F12),
          "novo atalho aplicado");
    check(!m.applyPendingReload(w) && modified == 1, "o pedido é consumido (não recarrega duas vezes)");

    std::filesystem::remove(p);
    m.requestReload();
    w.clear();
    check(!m.applyPendingReload(w) && m.getSettings().RefreshRate == 30 && !w.empty(),
          "arquivo sumiu: mantém as configurações e avisa");
}

} // namespace

int main() {
    testDefaults();
    testRoundTrip();
    testBindingParsing();
    testInvalidValues();
    testLegacyFormat();
    testManagerCreatesDirectory();
    testReload();
    if (g_failures) {
        std::printf("\n%d verificação(ões) falharam\n", g_failures);
        return 1;
    }
    std::puts("\nTodos os testes passaram");
    return 0;
}
