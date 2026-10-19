"""Leitura e escrita do arquivo de configuração da lupa (glassbrick.conf).

O formato é o mesmo lido por src/SettingsFile.cpp: "Chave = valor", seções
[Nome] e comentários com "#". Ao gravar, só o valor das linhas é trocado, então
comentários e a ordem do arquivo são preservados.

Não depende de GTK, para poder ser testado sem tela (tests/test_ui_config.py).
"""

import re

# Seção onde cada chave é criada se ainda não existir no arquivo
SECTIONS = {
    "Geral": ["RefreshRate"],
    "Zoom": ["ZoomSpeed", "ZoomSpeedModes", "ZoomAmount", "ZoomAmountModes",
             "ZoomOffAuto", "ZoomOffTimeSeconds"],
    "Mouse e teclado": ["MouseModifiers", "MouseFlip", "MouseAccelerate", "KeyboardEnabled"],
    "Rastreamento": ["TrackMode", "TrackModes", "TrackSpeedIndex", "TrackSpeedSame",
                     "TrackBorderSizeBorder", "TrackBorderSizeKeyCombo", "TrackFrameSize",
                     "TrackLocationX", "TrackLocationY", "TrackLocationZoom",
                     "TrackLocationZoomLevel", "TrackTyping", "TrackFocus", "ClampEdge",
                     "AddFrame", "PushEdge", "TrackScreen", "AppAccessibility"],
    "Cores": ["ColourMode", "ColourModes", "ColourTransitionIndex", "ColourTransitionSame",
              "ColourInvertType", "ColourInvertSaturation", "ColourAdjustType",
              "ColourAdjustAmount", "ColourAdjustSimulate", "ColourTint", "ColourTintAmount",
              "ColourTintParty", "ColourContrast", "ColourContrastAmount", "ColourContrastInvert"],
    "Atalhos": ["ZoomIn", "ZoomOut", "ZoomOnToggle", "ZoomSpeedCycle", "ZoomAmountCycle",
                "TrackModeCycle", "ColourModeCycle", "ReadingModeToggle"],
}

ACTIONS = SECTIONS["Atalhos"]

_LINE = re.compile(r"^(\s*)([A-Za-z][A-Za-z0-9]*)(\s*=\s*)([^#]*?)(\s*(#.*)?)$")


def parse(text):
    """Devolve {chave: valor} de um texto de configuração."""
    values = {}
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("[") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def update(text, new_values):
    """Troca os valores em `text`, preservando comentários; acrescenta chaves que faltarem."""
    pending = dict(new_values)
    out = []
    for raw in text.splitlines():
        m = _LINE.match(raw)
        if m and m.group(2) in pending:
            indent, key, eq, _old, tail = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5) or ""
            out.append(f"{indent}{key}{eq}{pending.pop(key)}{tail}")
        else:
            out.append(raw)

    # Chaves novas: no fim da seção correspondente (ou numa seção nova)
    for section, keys in SECTIONS.items():
        missing = [k for k in keys if k in pending]
        if not missing:
            continue
        lines = [f"{k} = {pending.pop(k)}" for k in missing]
        header = f"[{section}]"
        if header in (l.strip() for l in out):
            idx = next(i for i, l in enumerate(out) if l.strip() == header) + 1
            while idx < len(out) and not out[idx].strip().startswith("["):
                idx += 1
            while idx > 0 and not out[idx - 1].strip():
                idx -= 1
            out[idx:idx] = lines
        else:
            out += ["", header] + lines
    for key, value in pending.items():  # chaves fora do mapa
        out.append(f"{key} = {value}")
    return "\n".join(out) + "\n"


# --- Atalhos ---------------------------------------------------------------

MODIFIER_NAMES = {
    "ctrl": "Ctrl", "control": "Ctrl",
    "alt": "Alt",
    "shift": "Shift",
    "super": "Super", "win": "Super", "windows": "Super", "meta": "Super",
}
MODIFIER_ORDER = ["Ctrl", "Super", "Alt", "Shift"]  # mesma ordem do original
GRAVE_ALIASES = {"grave", "`", "til", "crase"}
NO_BINDING = {"", "nenhum", "none"}
MODIFIER_KEYS = {"Shift_L", "Shift_R", "Control_L", "Control_R", "Alt_L", "Alt_R", "Meta_L",
                  "Meta_R", "Super_L", "Super_R", "Hyper_L", "Hyper_R", "ISO_Level3_Shift",
                  "Caps_Lock", "Shift_Lock", "ISO_Level5_Shift", "Mode_switch", "Num_Lock",
                  "ISO_Next_Group", "ISO_Prev_Group", "ISO_First_Group", "ISO_Last_Group"}


def normalize_binding(text, key_exists):
    """Valida e normaliza um atalho ("ctrl + alt + Grave" -> "Ctrl+Alt+grave").

    key_exists(nome) diz se o nome de tecla existe (no programa: Gdk.keyval_from_name).
    Devolve (texto_normalizado, None) ou (None, mensagem_de_erro).
    """
    t = text.strip()
    if t.lower() in NO_BINDING:
        return "nenhum", None
    parts = [p.strip() for p in t.split("+") if p.strip()]
    if not parts:
        return None, "atalho vazio"
    mods = set()
    for p in parts[:-1]:
        name = MODIFIER_NAMES.get(p.lower())
        if not name:
            return None, f'modificador desconhecido "{p}" (use Ctrl, Alt, Shift ou Super)'
        mods.add(name)
    key = parts[-1]
    if key.lower() in GRAVE_ALIASES:
        key = "grave"
    elif len(key) == 1:
        key = key.lower()
    elif not key_exists(key):
        return None, f'tecla desconhecida "{key}"'
    if key in MODIFIER_KEYS:
        return None, "o atalho precisa de uma tecla além dos modificadores"
    return "+".join([m for m in MODIFIER_ORDER if m in mods] + [key]), None


def duplicate_bindings(bindings):
    """Lista de (ação1, ação2, atalho) para atalhos repetidos (ignora "nenhum")."""
    seen = {}
    dups = []
    for action, binding in bindings.items():
        if binding == "nenhum":
            continue
        key = binding.lower()
        if key in seen:
            dups.append((seen[key], action, binding))
        else:
            seen[key] = action
    return dups
