// Leitura e escrita do arquivo de configuração (equivalente ao que o original
// faz com ApplicationSettingsBase em SettingsFileSettingsManager.cs).
//
// Formato: texto "Chave = valor", com seções [Nome] só para organizar (as
// chaves valem em qualquer seção, exceto em [Atalhos], onde a chave é o nome
// de uma ação). "#" inicia um comentário. Também lê o formato antigo
// (glassbrick_config.json), que era "Chave=valor" sem seções.

#include "GlassbrickSettings.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace Glassbrick {

namespace {

struct EnumName {
    int value;
    const char* name;
};

const EnumName kTrackModes[] = {
    {static_cast<int>(TrackMode::Centre), "Centre"},
    {static_cast<int>(TrackMode::Consistent), "Consistent"},
    {static_cast<int>(TrackMode::Border), "Border"},
    {static_cast<int>(TrackMode::KeyCombo), "KeyCombo"},
    {static_cast<int>(TrackMode::ScreenLocation), "ScreenLocation"},
    {static_cast<int>(TrackMode::NoTracking), "None"},
};
const EnumName kColourModes[] = {
    {static_cast<int>(ColourMode::Default), "Default"},
    {static_cast<int>(ColourMode::Invert), "Invert"},
    {static_cast<int>(ColourMode::Adjust), "Adjust"},
    {static_cast<int>(ColourMode::Tint), "Tint"},
    {static_cast<int>(ColourMode::Contrast), "Contrast"},
};
const EnumName kInvertModes[] = {
    {static_cast<int>(InvertMode::Brightness), "Brightness"},
    {static_cast<int>(InvertMode::Colour), "Colour"},
};
const EnumName kAdjustModes[] = {
    {static_cast<int>(AdjustMode::Deuteranomaly), "Deuteranomaly"},
    {static_cast<int>(AdjustMode::Protanomaly), "Protanomaly"},
    {static_cast<int>(AdjustMode::Tritanomaly), "Tritanomaly"},
    {static_cast<int>(AdjustMode::Monochromacy), "Monochromacy"},
};
const EnumName kContrastModes[] = {
    {static_cast<int>(ContrastMode::FullColour), "FullColour"},
    {static_cast<int>(ContrastMode::BlackAndWhite), "BlackAndWhite"},
    {static_cast<int>(ContrastMode::BlackAndYellow), "BlackAndYellow"},
    {static_cast<int>(ContrastMode::BlueAndYellow), "BlueAndYellow"},
    {static_cast<int>(ContrastMode::BlueAndWhite), "BlueAndWhite"},
};
const EnumName kTintColours[] = {
    {static_cast<int>(TintColour::Orange), "Orange"},   {static_cast<int>(TintColour::Yellow), "Yellow"},
    {static_cast<int>(TintColour::Lime), "Lime"},       {static_cast<int>(TintColour::Green), "Green"},
    {static_cast<int>(TintColour::Emerald), "Emerald"}, {static_cast<int>(TintColour::Cyan), "Cyan"},
    {static_cast<int>(TintColour::Azure), "Azure"},     {static_cast<int>(TintColour::Blue), "Blue"},
    {static_cast<int>(TintColour::Purple), "Purple"},   {static_cast<int>(TintColour::Magenta), "Magenta"},
    {static_cast<int>(TintColour::Rose), "Rose"},       {static_cast<int>(TintColour::Red), "Red"},
    {static_cast<int>(TintColour::Black), "Black"},
};
// Ações que podem ter atalho (as demais são internas)
const EnumName kBindableActions[] = {
    {static_cast<int>(InputAction::ZoomIn), "ZoomIn"},
    {static_cast<int>(InputAction::ZoomOut), "ZoomOut"},
    {static_cast<int>(InputAction::ZoomOnToggle), "ZoomOnToggle"},
    {static_cast<int>(InputAction::ZoomSpeedCycle), "ZoomSpeedCycle"},
    {static_cast<int>(InputAction::ZoomAmountCycle), "ZoomAmountCycle"},
    {static_cast<int>(InputAction::TrackModeCycle), "TrackModeCycle"},
    {static_cast<int>(InputAction::ColourModeCycle), "ColourModeCycle"},
    {static_cast<int>(InputAction::ReadingModeToggle), "ReadingModeToggle"},
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

template <size_t N>
const char* enumToName(const EnumName (&table)[N], int value) {
    for (const auto& e : table) if (e.value == value) return e.name;
    return "?";
}

template <size_t N>
std::optional<int> nameToEnum(const EnumName (&table)[N], const std::string& name) {
    for (const auto& e : table) if (lower(e.name) == lower(name)) return e.value;
    return std::nullopt;
}

template <size_t N>
std::string enumNames(const EnumName (&table)[N]) {
    std::string out;
    for (const auto& e : table) out += (out.empty() ? "" : ", ") + std::string(e.name);
    return out;
}

std::vector<std::string> splitList(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// --- leitores de valores (devolvem false e uma mensagem se inválido) -------

bool readBool(const std::string& v, bool& out, std::string& err) {
    const std::string l = lower(v);
    if (l == "true" || l == "1" || l == "sim" || l == "yes") { out = true; return true; }
    if (l == "false" || l == "0" || l == "nao" || l == "não" || l == "no") { out = false; return true; }
    err = "esperado true ou false";
    return false;
}

bool readInt(const std::string& v, int lo, int hi, int& out, std::string& err) {
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') { err = "esperado um número inteiro"; return false; }
    if (n < lo || n > hi) { err = "fora do intervalo " + std::to_string(lo) + "–" + std::to_string(hi); return false; }
    out = static_cast<int>(n);
    return true;
}

bool readFloat(const std::string& v, float lo, float hi, float& out, std::string& err) {
    std::string norm = v;
    std::replace(norm.begin(), norm.end(), ',', '.'); // aceita vírgula decimal
    std::istringstream in(norm);
    in.imbue(std::locale::classic());
    float f = 0.0f;
    in >> f;
    if (norm.empty() || in.fail() || !in.eof()) { err = "esperado um número (ex.: 0.5)"; return false; }
    if (f < lo || f > hi) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << "fora do intervalo " << lo << "–" << hi;
        err = o.str();
        return false;
    }
    out = f;
    return true;
}

std::string formatFloat(float f) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << f;
    return o.str();
}

std::string formatBool(bool b) { return b ? "true" : "false"; }

KeySym keySymFromName(const std::string& token) {
    const std::string l = lower(token);
    // A tecla à esquerda do 1 (VK_OEM_3), por posição física
    if (l == "grave" || l == "`" || l == "til" || l == "crase") return XK_grave;
    if (token.size() == 1) {
        unsigned char c = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(token[0])));
        if (c >= 0x20 && c < 0x7f) return c; // Latin-1: KeySym == código do caractere
    }
    // Um único caractere fora do ASCII, em UTF-8 (ex.: "ç" do ABNT2)
    const auto* u = reinterpret_cast<const unsigned char*>(token.data());
    unsigned long cp = 0;
    if (token.size() == 2 && (u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        cp = ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
    } else if (token.size() == 3 && (u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    }
    if (cp >= 0xA0) {
        KeySym sym = cp < 0x100 ? cp : 0x1000000 | cp; // Latin-1 direto; o resto, KeySym Unicode
        KeySym lowerSym = sym, upperSym = sym;
        XConvertCase(sym, &lowerSym, &upperSym);
        return lowerSym;
    }
    KeySym sym = XStringToKeysym(token.c_str());
    if (sym == NoSymbol) sym = XStringToKeysym(l.c_str());
    // Letras maiúsculas e minúsculas são a mesma tecla
    if (sym >= XK_A && sym <= XK_Z) sym = sym - XK_A + XK_a;
    return sym;
}

bool isModifierKeySym(KeySym sym) {
    return (sym >= XK_Shift_L && sym <= XK_Hyper_R) || sym == XK_ISO_Level3_Shift;
}

} // namespace

// ---------------------------------------------------------------------------
// Atalhos
// ---------------------------------------------------------------------------

const char* inputActionName(InputAction action) {
    return enumToName(kBindableActions, static_cast<int>(action));
}

std::string bindingToString(Modifiers modifiers, unsigned int keySym) {
    if (keySym == 0) return "nenhum";
    std::string out;
    auto has = [&](Modifiers m) { return (modifiers & m) != Modifiers::NoModifier; };
    // Mesma ordem do KeyBinding.ToString() original: Ctrl, Win, Alt, Shift
    if (has(Modifiers::Control)) out += "Ctrl+";
    if (has(Modifiers::Windows)) out += "Super+";
    if (has(Modifiers::Alt)) out += "Alt+";
    if (has(Modifiers::Shift)) out += "Shift+";
    const char* name = (keySym == XK_grave) ? "grave" : XKeysymToString(keySym);
    out += name ? name : "?";
    return out;
}

std::optional<std::pair<Modifiers, unsigned int>> parseBinding(const std::string& text, std::string& error) {
    const std::string t = trim(text);
    const std::string l = lower(t);
    if (l.empty() || l == "nenhum" || l == "none") return std::make_pair(Modifiers::NoModifier, 0u);

    std::vector<std::string> parts = splitList(t, '+');
    if (parts.empty()) {
        error = "atalho vazio";
        return std::nullopt;
    }
    Modifiers mods = Modifiers::NoModifier;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string m = lower(parts[i]);
        if (m == "ctrl" || m == "control") mods |= Modifiers::Control;
        else if (m == "alt") mods |= Modifiers::Alt;
        else if (m == "shift") mods |= Modifiers::Shift;
        else if (m == "super" || m == "win" || m == "windows" || m == "meta") mods |= Modifiers::Windows;
        else {
            error = "modificador desconhecido \"" + parts[i] + "\" (use Ctrl, Alt, Shift ou Super)";
            return std::nullopt;
        }
    }
    KeySym sym = keySymFromName(parts.back());
    if (sym == NoSymbol) {
        error = "tecla desconhecida \"" + parts.back() + "\"";
        return std::nullopt;
    }
    if (isModifierKeySym(sym)) {
        error = "o atalho precisa de uma tecla além dos modificadores";
        return std::nullopt;
    }
    return std::make_pair(mods, static_cast<unsigned int>(sym));
}

// ---------------------------------------------------------------------------
// Leitura
// ---------------------------------------------------------------------------

bool GlassbrickSettings::loadFromFile(const std::string& filename, std::vector<std::string>& warnings) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    using Setter = std::function<bool(const std::string&, std::string&)>;
    auto boolSetter = [](bool& field) -> Setter {
        return [&field](const std::string& v, std::string& e) { return readBool(v, field, e); };
    };
    auto intSetter = [](int& field, int lo, int hi) -> Setter {
        return [&field, lo, hi](const std::string& v, std::string& e) { return readInt(v, lo, hi, field, e); };
    };
    auto floatSetter = [](float& field, float lo, float hi) -> Setter {
        return [&field, lo, hi](const std::string& v, std::string& e) { return readFloat(v, lo, hi, field, e); };
    };
    auto enumSetter = [](auto& field, const auto& table) -> Setter {
        return [&field, &table](const std::string& v, std::string& e) {
            auto value = nameToEnum(table, v);
            if (!value) { e = "valores aceitos: " + enumNames(table); return false; }
            field = static_cast<std::remove_reference_t<decltype(field)>>(*value);
            return true;
        };
    };
    // Modo atual de um ciclador por índice (0–4)
    auto intCyclerMode = [](IntModeCycler<float>& cycler) -> Setter {
        return [&cycler](const std::string& v, std::string& e) {
            int m = 0;
            if (!readInt(v, 0, 4, m, e)) return false;
            cycler.setMode(m);
            return true;
        };
    };
    // Lista de modos habilitados no ciclo de um IntModeCycler
    auto intCyclerList = [](IntModeCycler<float>& cycler) -> Setter {
        return [&cycler](const std::string& v, std::string& e) {
            std::vector<int> modes;
            for (const auto& item : splitList(v, ',')) {
                int m = 0;
                if (!readInt(item, 0, 4, m, e)) { e = "\"" + item + "\": " + e; return false; }
                modes.push_back(m);
            }
            if (modes.empty()) { e = "a lista precisa ter ao menos um modo"; return false; }
            for (int m = 0; m < 5; ++m) cycler.setModeEnabled(m, std::find(modes.begin(), modes.end(), m) != modes.end());
            return true;
        };
    };
    auto enumCyclerMode = [](auto& cycler, const auto& table) -> Setter {
        return [&cycler, &table](const std::string& v, std::string& e) {
            auto value = nameToEnum(table, v);
            if (!value) { e = "valores aceitos: " + enumNames(table); return false; }
            using T = decltype(cycler.getMode());
            cycler.setMode(static_cast<T>(*value));
            return true;
        };
    };
    auto enumCyclerList = [](auto& cycler, const auto& table) -> Setter {
        return [&cycler, &table](const std::string& v, std::string& e) {
            using T = decltype(cycler.getMode());
            std::vector<int> modes;
            for (const auto& item : splitList(v, ',')) {
                auto value = nameToEnum(table, item);
                if (!value) { e = "\"" + item + "\" inválido; valores aceitos: " + enumNames(table); return false; }
                modes.push_back(*value);
            }
            if (modes.empty()) { e = "a lista precisa ter ao menos um modo"; return false; }
            for (const auto& entry : table) {
                cycler.setModeEnabled(static_cast<T>(entry.value),
                                      std::find(modes.begin(), modes.end(), entry.value) != modes.end());
            }
            return true;
        };
    };

    const std::map<std::string, Setter> setters = {
        // [Geral]
        {"RefreshRate", intSetter(RefreshRate, 10, 240)},
        // [Zoom]
        {"ZoomSpeed", intCyclerMode(ZoomSpeedActual)},
        {"ZoomSpeedModes", intCyclerList(ZoomSpeedActual)},
        {"ZoomAmount", intCyclerMode(ZoomAmountActual)},
        {"ZoomAmountModes", intCyclerList(ZoomAmountActual)},
        {"ZoomOffAuto", boolSetter(ZoomOffAuto)},
        {"ZoomOffTimeSeconds", floatSetter(ZoomOffTimeSeconds, 1.0f, 3600.0f)},
        // [Mouse e teclado]
        {"MouseModifiers", [this](const std::string& v, std::string& e) {
             auto parsed = parseBinding(v + "+a", e); // reaproveita o leitor de modificadores
             if (!parsed || parsed->first == Modifiers::NoModifier) {
                 if (e.empty()) e = "informe ao menos um modificador (ex.: Ctrl)";
                 return false;
             }
             MouseModifiers = parsed->first;
             return true;
         }},
        {"MouseFlip", boolSetter(MouseFlip)},
        {"MouseAccelerate", boolSetter(MouseAccelerate)},
        {"KeyboardEnabled", boolSetter(KeyboardEnabled)},
        // [Rastreamento]
        {"TrackMode", enumCyclerMode(CurrentTrackModeActual, kTrackModes)},
        {"TrackModes", enumCyclerList(CurrentTrackModeActual, kTrackModes)},
        {"TrackSpeedIndex", intSetter(TrackSpeedIndex, 0, 4)},
        {"TrackSpeedSame", boolSetter(TrackSpeedSame)},
        {"TrackBorderSizeBorder", floatSetter(TrackBorderSizeBorder, 0.0f, 1.0f)},
        {"TrackBorderSizeKeyCombo", floatSetter(TrackBorderSizeKeyCombo, 0.0f, 1.0f)},
        {"TrackFrameSize", floatSetter(TrackFrameSize, 0.0f, 2.0f)},
        {"TrackLocationX", floatSetter(TrackLocationX, 0.0f, 1.0f)},
        {"TrackLocationY", floatSetter(TrackLocationY, 0.0f, 1.0f)},
        {"TrackLocationZoom", boolSetter(TrackLocationZoom)},
        {"TrackLocationZoomLevel", floatSetter(TrackLocationZoomLevel, 0.0f, 5.0f)},
        {"TrackTyping", boolSetter(TrackTyping)},
        {"TrackFocus", boolSetter(TrackFocus)},
        {"ClampEdge", boolSetter(ClampEdge)},
        {"AddFrame", boolSetter(AddFrame)},
        {"PushEdge", boolSetter(PushEdge)},
        {"TrackScreen", boolSetter(TrackScreen)},
        {"AppAccessibility", boolSetter(AppAccessibility)},
        // [Cores]
        {"ColourMode", enumCyclerMode(CurrentColourModeActual, kColourModes)},
        {"ColourModes", enumCyclerList(CurrentColourModeActual, kColourModes)},
        {"ColourTransitionIndex", intSetter(ColourTransitionIndex, 0, 4)},
        {"ColourTransitionSame", boolSetter(ColourTransitionSame)},
        {"ColourInvertType", enumSetter(ColourInvertType, kInvertModes)},
        {"ColourInvertSaturation", floatSetter(ColourInvertSaturation, 0.0f, 1.0f)},
        {"ColourAdjustType", enumSetter(ColourAdjustType, kAdjustModes)},
        {"ColourAdjustAmount", floatSetter(ColourAdjustAmount, 0.0f, 1.0f)},
        {"ColourAdjustSimulate", boolSetter(ColourAdjustSimulate)},
        {"ColourTint", enumSetter(ColourTint, kTintColours)},
        {"ColourTintAmount", floatSetter(ColourTintAmount, 0.0f, 1.0f)},
        {"ColourTintParty", boolSetter(ColourTintParty)},
        {"ColourContrast", enumSetter(ColourContrast, kContrastModes)},
        {"ColourContrastAmount", floatSetter(ColourContrastAmount, 0.0f, 1.0f)},
        {"ColourContrastInvert", boolSetter(ColourContrastInvert)},
    };

    std::string section;
    std::string line;
    int lineNo = 0;
    auto warn = [&](const std::string& msg) {
        warnings.push_back(filename + ":" + std::to_string(lineNo) + ": " + msg);
    };

    while (std::getline(file, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty() || line[0] == '/') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            warn("linha ignorada (esperado \"chave = valor\")");
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (section == "atalhos") {
            auto action = nameToEnum(kBindableActions, key);
            if (!action) {
                warn("ação desconhecida \"" + key + "\"; ações: " + enumNames(kBindableActions));
                continue;
            }
            std::string err;
            auto parsed = parseBinding(value, err);
            if (!parsed) {
                const KeyBinding* current = bindingFor(static_cast<InputAction>(*action));
                warn(key + ": " + err + "; mantido " +
                     (current ? bindingToString(current->modifiers, current->keySym) : "nenhum"));
                continue;
            }
            setBinding(static_cast<InputAction>(*action), parsed->first, parsed->second);
            continue;
        }

        auto it = setters.find(key);
        if (it == setters.end()) {
            warn("opção desconhecida \"" + key + "\" (ignorada)");
            continue;
        }
        std::string err;
        if (!it->second(value, err)) warn(key + " = " + value + ": " + err + "; mantido o valor anterior");
    }

    // O modo atual precisa estar entre os habilitados no ciclo
    if (!CurrentTrackModeActual.isModeEnabled(CurrentTrackModeActual.getMode())) {
        warnings.push_back(filename + ": TrackMode não está em TrackModes; usando o primeiro da lista");
        CurrentTrackModeActual.cycle();
    }
    if (!CurrentColourModeActual.isModeEnabled(CurrentColourModeActual.getMode())) {
        warnings.push_back(filename + ": ColourMode não está em ColourModes; usando o primeiro da lista");
        CurrentColourModeActual.cycle();
    }

    // Dois atalhos iguais: só um funcionaria
    for (size_t i = 0; i < Bindings.size(); ++i) {
        for (size_t j = i + 1; j < Bindings.size(); ++j) {
            if (Bindings[i].keySym == Bindings[j].keySym && Bindings[i].modifiers == Bindings[j].modifiers) {
                warnings.push_back(filename + ": " + inputActionName(Bindings[i].action) + " e " +
                                   inputActionName(Bindings[j].action) + " usam o mesmo atalho (" +
                                   bindingToString(Bindings[i].modifiers, Bindings[i].keySym) + ")");
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Escrita
// ---------------------------------------------------------------------------

bool GlassbrickSettings::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    file << toConfigText();
    return static_cast<bool>(file);
}

std::string GlassbrickSettings::toConfigText() const {
    std::ostringstream o;
    o.imbue(std::locale::classic());

    auto intModes = [](const IntModeCycler<float>& c) {
        std::string out;
        for (int m = 0; m < 5; ++m) if (c.isModeEnabled(m)) out += (out.empty() ? "" : ", ") + std::to_string(m);
        return out;
    };
    auto enumModes = [](const auto& c, const auto& table) {
        using T = decltype(c.getMode());
        std::string out;
        for (const auto& e : table) {
            if (c.isModeEnabled(static_cast<T>(e.value))) out += (out.empty() ? "" : ", ") + std::string(e.name);
        }
        return out;
    };

    o << "# Configuração da Lupa Linux (clone do Glassbrick)\n"
         "#\n"
         "# Formato: Chave = valor. Linhas começando com # são comentários.\n"
         "# Valores inválidos são ignorados com um aviso no terminal (fica o valor padrão).\n"
         "# Apague este arquivo para voltar aos padrões. Detalhes em docs/CONFIGURACAO.md.\n"
         "# Trocas feitas por atalho (Alt+3, Ctrl+`...) valem só até fechar o programa,\n"
         "# como no original; para torná-las permanentes, altere este arquivo.\n\n";

    o << "[Geral]\n"
      << "RefreshRate = " << RefreshRate << "              # quadros por segundo da lupa (10–240)\n\n";

    o << "[Zoom]\n"
         "# Velocidade da animação: 0 = lenta, 1 = 1/16, 2 = normal, 3 = 1/4, 4 = instantânea\n"
      << "ZoomSpeed = " << ZoomSpeedActual.getMode() << "\n"
      << "ZoomSpeedModes = " << intModes(ZoomSpeedActual) << "         # modos alternados com o atalho ZoomSpeedCycle\n"
      << "# Passo de cada clique (em níveis; fator = 2^nível): 0 = 0.0625, 1 = 0.125, 2 = 0.25, 3 = 0.5, 4 = 1\n"
      << "ZoomAmount = " << ZoomAmountActual.getMode() << "\n"
      << "ZoomAmountModes = " << intModes(ZoomAmountActual) << "        # modos alternados com o atalho ZoomAmountCycle\n"
      << "ZoomOffAuto = " << formatBool(ZoomOffAuto) << "          # desliga o zoom sozinho após um tempo parado\n"
      << "ZoomOffTimeSeconds = " << formatFloat(ZoomOffTimeSeconds) << "      # segundos parado até desligar (1–3600)\n\n";

    o << "[Mouse e teclado]\n"
      << "MouseModifiers = " << [&] {
             std::string s = bindingToString(MouseModifiers, XK_a);
             return s.substr(0, s.size() - 2); // tira o "+a"
         }() << "        # modificador usado com a roda para dar zoom (Ctrl, Alt, Shift, Super)\n"
      << "MouseFlip = " << formatBool(MouseFlip) << "            # inverte o sentido da roda\n"
      << "MouseAccelerate = " << formatBool(MouseAccelerate) << "       # sem efeito no Linux (cada clique da roda é um passo)\n"
      << "KeyboardEnabled = " << formatBool(KeyboardEnabled) << "       # false desliga todos os atalhos de teclado\n\n";

    o << "[Rastreamento]\n"
      << "# Modos: " << enumNames(kTrackModes) << "\n"
      << "TrackMode = " << enumToName(kTrackModes, static_cast<int>(CurrentTrackModeActual.getMode())) << "\n"
      << "TrackModes = " << enumModes(CurrentTrackModeActual, kTrackModes) << "    # modos alternados com o atalho TrackModeCycle\n"
      << "TrackSpeedIndex = " << TrackSpeedIndex << "          # velocidade com que a vista segue: 0 (lenta) a 4 (imediata)\n"
      << "TrackSpeedSame = " << formatBool(TrackSpeedSame) << "\n"
      << "TrackBorderSizeBorder = " << formatFloat(TrackBorderSizeBorder) << "  # modo Border: fração livre no centro da tela (0–1)\n"
      << "TrackBorderSizeKeyCombo = " << formatFloat(TrackBorderSizeKeyCombo) << " # modo KeyCombo: idem (0–1)\n"
      << "TrackFrameSize = " << formatFloat(TrackFrameSize) << "         # modo Consistent: proporção (0–2)\n"
      << "TrackLocationX = " << formatFloat(TrackLocationX) << "         # modo ScreenLocation: ponto fixo (0–1 da largura)\n"
      << "TrackLocationY = " << formatFloat(TrackLocationY) << "         # modo ScreenLocation: ponto fixo (0–1 da altura)\n"
      << "TrackLocationZoom = " << formatBool(TrackLocationZoom) << "    # modo ScreenLocation: aplica o zoom abaixo ao entrar\n"
      << "TrackLocationZoomLevel = " << formatFloat(TrackLocationZoomLevel) << "    # nível de zoom (0–5)\n"
      << "TrackTyping = " << formatBool(TrackTyping) << "           # segue o cursor de texto e a seleção ao digitar\n"
      << "TrackFocus = " << formatBool(TrackFocus) << "            # segue o elemento focado (Tab, menus...)\n"
      << "ClampEdge = " << formatBool(ClampEdge) << "             # não mostra área além da borda da tela\n"
      << "AddFrame = " << formatBool(AddFrame) << "             # margem de 2 px nas bordas\n"
      << "PushEdge = " << formatBool(PushEdge) << "              # o mouse empurra a vista e nunca sai dela\n"
      << "TrackScreen = " << formatBool(TrackScreen) << "           # segue o monitor onde está o foco\n"
      << "# Liga a acessibilidade do sistema (org.a11y.Status.IsEnabled) para que navegadores como o\n"
      << "# Firefox deixem a lupa seguir o foco e o cursor de texto dentro das páginas. Vale para\n"
      << "# programas abertos depois; pode deixar navegadores um pouco mais pesados.\n"
      << "AppAccessibility = " << formatBool(AppAccessibility) << "\n\n";

    o << "[Cores]\n"
      << "# Modos: " << enumNames(kColourModes) << "\n"
      << "ColourMode = " << enumToName(kColourModes, static_cast<int>(CurrentColourModeActual.getMode())) << "\n"
      << "ColourModes = " << enumModes(CurrentColourModeActual, kColourModes) << "  # modos alternados com o atalho ColourModeCycle\n"
      << "ColourTransitionIndex = " << ColourTransitionIndex << "    # velocidade da transição: 0 (lenta) a 4 (instantânea)\n"
      << "ColourTransitionSame = " << formatBool(ColourTransitionSame) << "\n"
      << "ColourInvertType = " << enumToName(kInvertModes, static_cast<int>(ColourInvertType))
      << "   # Invert: " << enumNames(kInvertModes) << "\n"
      << "ColourInvertSaturation = " << formatFloat(ColourInvertSaturation) << "   # Invert: saturação (0–1)\n"
      << "ColourAdjustType = " << enumToName(kAdjustModes, static_cast<int>(ColourAdjustType))
      << "   # Adjust (daltonismo): " << enumNames(kAdjustModes) << "\n"
      << "ColourAdjustAmount = " << formatFloat(ColourAdjustAmount) << "         # Adjust: intensidade (0–1)\n"
      << "ColourAdjustSimulate = " << formatBool(ColourAdjustSimulate) << "    # Adjust: true simula a deficiência em vez de corrigir\n"
      << "ColourTint = " << enumToName(kTintColours, static_cast<int>(ColourTint))
      << "             # Tint: " << enumNames(kTintColours) << "\n"
      << "ColourTintAmount = " << formatFloat(ColourTintAmount) << "         # Tint: intensidade (0–1)\n"
      << "ColourTintParty = " << formatBool(ColourTintParty) << "         # Tint: troca de cor aleatoriamente\n"
      << "ColourContrast = " << enumToName(kContrastModes, static_cast<int>(ColourContrast))
      << "  # Contrast: " << enumNames(kContrastModes) << "\n"
      << "ColourContrastAmount = " << formatFloat(ColourContrastAmount) << "       # Contrast: intensidade (0–1)\n"
      << "ColourContrastInvert = " << formatBool(ColourContrastInvert) << "    # Contrast: inverte claro/escuro\n\n";

    o << "[Atalhos]\n"
         "# Formato: Modificadores+Tecla, ex.: Ctrl+Alt+grave, Alt+r, Super+F5. \"nenhum\" desliga.\n"
         "# Modificadores: Ctrl, Alt, Shift, Super. Teclas: nomes do X11 (a, 1, F5, Return, space...).\n"
         "# \"grave\" é a tecla à esquerda do 1, abaixo do Esc (no ABNT2, a tecla ').\n";
    for (const auto& entry : kBindableActions) {
        const KeyBinding* b = bindingFor(static_cast<InputAction>(entry.value));
        o << entry.name << " = " << (b ? bindingToString(b->modifiers, b->keySym) : "nenhum") << "\n";
    }

    return o.str();
}

// ---------------------------------------------------------------------------
// SettingsManager
// ---------------------------------------------------------------------------

SettingsManager::SettingsManager(const std::string& configPath) : _configPath(configPath) {}

std::string SettingsManager::defaultConfigPath() {
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".config";
    } else {
        base = ".";
    }
    return (base / "glassbrick" / "glassbrick.conf").string();
}

bool SettingsManager::load(std::vector<std::string>& warnings) {
    return loadFrom(_configPath, warnings);
}

bool SettingsManager::loadFrom(const std::string& path, std::vector<std::string>& warnings) {
    return _settings.loadFromFile(path, warnings);
}

bool SettingsManager::applyPendingReload(std::vector<std::string>& warnings) {
    if (!_reloadRequested.exchange(false)) return false;
    // Lê fora do lock (arquivo pequeno, mas não há por que travar as outras threads)
    GlassbrickSettings fresh;
    if (!fresh.loadFromFile(_configPath, warnings)) {
        warnings.push_back("não foi possível ler " + _configPath + "; configurações mantidas");
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _settings = std::move(fresh);
    for (auto& listener : _listeners) listener();
    return true;
}

bool SettingsManager::save() {
    std::error_code ec;
    const auto parent = std::filesystem::path(_configPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    return _settings.saveToFile(_configPath);
}

} // namespace Glassbrick
