#!/usr/bin/env python3
"""Ícone na bandeja e janela de configurações da Lupa Linux (equivalente a SettingsForm.cs).

Normalmente é aberto pela própria lupa (./glassbrick), que passa --pid, --config e --exe.
- Aplicar e OK gravam o arquivo de configuração (depois de a lupa conferi-lo com
  --verificar-config) e enviam SIGHUP à lupa, que recarrega sem reiniciar. OK também
  fecha a janela.
- "Sair" no menu da bandeja encerra a lupa (SIGTERM).
- Fechar a janela só a esconde, como no original (que minimiza para a bandeja).

Também pode ser usado sozinho para editar o arquivo:
    python3 ui/glassbrick_ui.py --show [--config caminho]
"""

import argparse
import os
import re
import signal
import subprocess
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config_file as cf  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ICON = os.path.join(HERE, "glassbrick.png")
DOCS = os.path.normpath(os.path.join(HERE, "..", "docs"))
VERSION = "2.2 (clone para Linux)"

# Tecla física à esquerda do 1 (TLDE) no mapeamento evdev; a lupa a chama de "grave"
TILDE_KEYCODE = 49

SPEED_LABELS = ["Mais lenta", "Lenta", "Média", "Rápida", "Instantânea"]
AMOUNT_LABELS = ["Mínimo", "Pequeno", "Médio", "Grande", "Enorme"]
TRACK_SPEED_LABELS = ["Muito lenta", "Lenta", "Média", "Rápida", "Instantânea"]

TRACK_MODES = [  # (valor no arquivo, rótulo, descrição)
    ("Centre", "Centro", "O ponto sob o mouse fica sempre no centro da tela."),
    ("Consistent", "Proporcional", "A posição do mouse na tela corresponde à posição na área ampliada."),
    ("Border", "Borda", "A imagem só rola quando o mouse chega perto das bordas."),
    ("KeyCombo", "Tecla", "A imagem só se move enquanto o modificador do mouse (Ctrl) está pressionado."),
    ("ScreenLocation", "Região", "A vista fica num ponto fixo; segure Ctrl e mova o mouse para olhar em volta."),
    ("None", "Nenhum", "A vista não segue o mouse."),
]
COLOUR_MODES = [
    ("Default", "Normal", "Cores normais."),
    ("Invert", "Inverter", "Inverte o brilho ou as cores."),
    ("Adjust", "Daltonismo", "Corrige (ou simula) deficiências de visão de cores."),
    ("Tint", "Tingir", "Tinge a tela de uma cor."),
    ("Contrast", "Contraste", "Alto contraste com duas cores."),
]
INVERT_TYPES = [("Brightness", "Brilho (mantém os tons)"), ("Colour", "Cores")]
ADJUST_TYPES = [("Deuteranomaly", "Deuteranomalia (verde)"), ("Protanomaly", "Protanomalia (vermelho)"),
                ("Tritanomaly", "Tritanomalia (azul)"), ("Monochromacy", "Monocromacia (tons de cinza)")]
TINTS = [("Orange", "Laranja"), ("Yellow", "Amarelo"), ("Lime", "Lima"), ("Green", "Verde"),
         ("Emerald", "Esmeralda"), ("Cyan", "Ciano"), ("Azure", "Azul-celeste"), ("Blue", "Azul"),
         ("Purple", "Roxo"), ("Magenta", "Magenta"), ("Rose", "Rosa"), ("Red", "Vermelho"), ("Black", "Preto")]
CONTRASTS = [("FullColour", "Cores intensas"), ("BlackAndWhite", "Preto e branco"),
             ("BlackAndYellow", "Preto e amarelo"), ("BlueAndYellow", "Azul e amarelo"),
             ("BlueAndWhite", "Azul e branco")]

ACTION_LABELS = {
    "ZoomIn": "Aumentar zoom",
    "ZoomOut": "Diminuir zoom",
    "ZoomOnToggle": "Ligar/desligar zoom",
    "ZoomSpeedCycle": "Ciclo de velocidade",
    "ZoomAmountCycle": "Ciclo de passo",
    "TrackModeCycle": "Ciclo de modo",
    "ColourModeCycle": "Ciclo de cor",
    "ReadingModeToggle": "Ligar/desligar",
}


def key_exists(name):
    return Gdk.keyval_from_name(name) not in (0, 0xFFFFFF)


def fmt(x):
    return f"{x:.3f}".rstrip("0").rstrip(".")


def to_float(text, default):
    try:
        return float(text.replace(",", "."))
    except (ValueError, AttributeError):
        return default


def to_bool(text):
    return str(text).strip().lower() in ("true", "1", "sim", "yes")


# ---------------------------------------------------------------------------
# Campo de atalho: texto editável + "Gravar" (captura a combinação) + "Limpar"
# ---------------------------------------------------------------------------

class ShortcutEntry(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        self.window = window
        self.entry = Gtk.Entry(width_chars=16)
        self.entry.set_tooltip_text("Ex.: Ctrl+Alt+grave, Alt+r, Super+F5. \"grave\" é a tecla à esquerda do 1.")
        record = Gtk.Button(label="Gravar")
        record.set_tooltip_text("Clique e pressione a combinação de teclas (Esc cancela)")
        record.connect("clicked", self._start_capture)
        clear = Gtk.Button(label="Limpar")
        clear.set_tooltip_text("Desliga este atalho")
        clear.connect("clicked", lambda *_: self.entry.set_text("nenhum"))
        self.pack_start(self.entry, True, True, 0)
        self.pack_start(record, False, False, 0)
        self.pack_start(clear, False, False, 0)
        self._previous = ""
        self._handler = None
        self._seat = None

    def get(self):
        return self.entry.get_text()

    def set(self, value):
        self.entry.set_text(value or "nenhum")

    def _start_capture(self, _button):
        self._previous = self.entry.get_text()
        self.entry.set_text("Pressione a combinação…")
        self._handler = self.window.connect("key-press-event", self._on_key)
        # Captura ativa do teclado: tem prioridade sobre os atalhos que a lupa
        # registrou, então dá para gravar inclusive combinações já em uso
        gdk_window = self.window.get_window()
        self._seat = Gdk.Display.get_default().get_default_seat()
        self._seat.grab(gdk_window, Gdk.SeatCapabilities.KEYBOARD, False, None, None, None, None)

    def _stop_capture(self):
        if self._handler:
            self.window.disconnect(self._handler)
            self._handler = None
        if self._seat:
            self._seat.ungrab()
            self._seat = None

    def _on_key(self, _widget, event):
        # Ctrl, Alt etc. sozinhos: espera a tecla principal. Confere também pelo
        # nome, porque event.is_modifier não vem marcado em todos os sistemas
        # (sem isso, gravar Ctrl+Alt+J registrava só "Control_L")
        if event.is_modifier or Gdk.keyval_name(event.keyval) in cf.MODIFIER_KEYS:
            return True
        state = event.state & Gtk.accelerator_get_default_mod_mask()
        if event.keyval == Gdk.KEY_Escape and not state:
            self.entry.set_text(self._previous)
            self._stop_capture()
            return True
        mods = []
        if state & Gdk.ModifierType.CONTROL_MASK:
            mods.append("Ctrl")
        if state & (Gdk.ModifierType.SUPER_MASK | Gdk.ModifierType.MOD4_MASK):
            mods.append("Super")
        if state & Gdk.ModifierType.MOD1_MASK:
            mods.append("Alt")
        if state & Gdk.ModifierType.SHIFT_MASK:
            mods.append("Shift")
        if event.hardware_keycode == TILDE_KEYCODE:
            name = "grave"
        else:
            # Tecla "base" (sem Shift), como a lupa compara: pela tecla física
            keymap = Gdk.Keymap.get_for_display(Gdk.Display.get_default())
            ok, keyval, *_ = keymap.translate_keyboard_state(event.hardware_keycode, 0, event.group)
            name = Gdk.keyval_name(Gdk.keyval_to_lower(keyval if ok else event.keyval))
        self.entry.set_text("+".join(mods + [name]))
        self._stop_capture()
        return True


# ---------------------------------------------------------------------------
# Janela de configurações
# ---------------------------------------------------------------------------

class SettingsWindow(Gtk.Window):
    def __init__(self, app):
        super().__init__(title="Glassbrick — Configurações")
        self.app = app
        self.set_icon_from_file(ICON)
        self.set_default_size(620, 560)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.connect("delete-event", self._on_close)

        self.fields = {}     # chave do arquivo -> (get, set)
        self.shortcuts = {}  # ação -> ShortcutEntry

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, border_width=10)
        self.add(root)
        notebook = Gtk.Notebook()
        root.pack_start(notebook, True, True, 0)
        notebook.append_page(self._scroll(self._zoom_tab()), Gtk.Label(label="Zoom"))
        notebook.append_page(self._scroll(self._track_tab()), Gtk.Label(label="Rastreamento"))
        notebook.append_page(self._scroll(self._colour_tab()), Gtk.Label(label="Cores"))
        notebook.append_page(self._scroll(self._misc_tab()), Gtk.Label(label="Outros"))

        buttons = Gtk.Box(spacing=6)
        restore = Gtk.Button(label="Restaurar padrões")
        restore.connect("clicked", self._on_restore)
        cancel = Gtk.Button(label="Cancelar")
        cancel.connect("clicked", self._on_cancel)
        apply = Gtk.Button(label="Aplicar")
        apply.set_tooltip_text("Salva e aplica na lupa sem fechar esta janela")
        apply.connect("clicked", self._on_apply)
        ok = Gtk.Button(label="OK")
        ok.get_style_context().add_class("suggested-action")
        ok.connect("clicked", self._on_ok)
        buttons.pack_start(restore, False, False, 0)
        buttons.pack_end(ok, False, False, 0)
        buttons.pack_end(apply, False, False, 0)
        buttons.pack_end(cancel, False, False, 0)
        root.pack_start(buttons, False, False, 0)

    # --- ajudantes de layout e de campos ---------------------------------

    @staticmethod
    def _scroll(widget):
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.add(widget)
        return sw

    @staticmethod
    def _frame(title, child):
        frame = Gtk.Frame(label=title)
        child.set_border_width(8)
        frame.add(child)
        return frame

    @staticmethod
    def _page():
        return Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, border_width=10)

    def _check(self, key, label, tooltip=None):
        w = Gtk.CheckButton(label=label)
        if tooltip:
            w.set_tooltip_text(tooltip)
        self.fields[key] = (lambda: "true" if w.get_active() else "false", lambda v: w.set_active(to_bool(v)))
        return w

    def _step_scale(self, key, labels):
        """Escala de 0 a 4 com nomes (velocidade, passo...)."""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, len(labels) - 1, 1)
        scale.set_draw_value(False)
        for i, name in enumerate(labels):
            scale.add_mark(i, Gtk.PositionType.BOTTOM, name)
        scale.connect("value-changed", lambda s: s.set_value(round(s.get_value())))
        box.pack_start(scale, False, False, 0)
        self.fields[key] = (lambda: str(int(round(scale.get_value()))), lambda v: scale.set_value(int(to_float(v, 2))))
        return box

    def _float_scale(self, key, lo, hi, step, default):
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, lo, hi, step)
        scale.set_digits(2)
        scale.set_hexpand(True)
        self.fields[key] = (lambda: fmt(scale.get_value()), lambda v: scale.set_value(to_float(v, default)))
        return scale

    def _spin(self, key, lo, hi, step, digits, default):
        spin = Gtk.SpinButton.new_with_range(lo, hi, step)
        spin.set_digits(digits)
        getter = (lambda: str(int(spin.get_value()))) if digits == 0 else (lambda: fmt(spin.get_value()))
        self.fields[key] = (getter, lambda v: spin.set_value(to_float(v, default)))
        return spin

    def _combo(self, key, options):
        combo = Gtk.ComboBoxText()
        for value, label in options:
            combo.append(value, label)
        def set_(value):
            if not combo.set_active_id(value):
                combo.set_active(0)

        self.fields[key] = (lambda: combo.get_active_id() or options[0][0], set_)
        return combo

    def _cycle_checks(self, key, values, labels):
        """Linha de caixas "incluir no ciclo" -> lista no arquivo (ex.: 0, 2, 4)."""
        box = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=6)
        checks = []
        for value, label in zip(values, labels):
            c = Gtk.CheckButton(label=label)
            checks.append((value, c))
            box.add(c)

        def get():
            return ", ".join(v for v, c in checks if c.get_active())

        def set_(text):
            chosen = {p.strip().lower() for p in text.split(",")}
            for v, c in checks:
                c.set_active(v.lower() in chosen)

        self.fields[key] = (get, set_)
        self._cycles = getattr(self, "_cycles", {})
        self._cycles[key] = checks
        return box

    def _shortcut(self, action):
        s = ShortcutEntry(self)
        self.shortcuts[action] = s
        self.fields[action] = (s.get, s.set)
        return s

    @staticmethod
    def _grid(rows):
        grid = Gtk.Grid(column_spacing=10, row_spacing=6)
        for r, (label, widget) in enumerate(rows):
            lbl = Gtk.Label(label=label, xalign=0)
            grid.attach(lbl, 0, r, 1, 1)
            widget.set_hexpand(True)
            grid.attach(widget, 1, r, 1, 1)
        return grid

    @staticmethod
    def _hint(text):
        lbl = Gtk.Label(label=text, xalign=0)
        lbl.set_line_wrap(True)
        lbl.get_style_context().add_class("dim-label")
        return lbl

    # --- abas ---------------------------------------------------------------

    def _zoom_tab(self):
        page = self._page()

        speed = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        speed.pack_start(self._step_scale("ZoomSpeed", SPEED_LABELS), False, False, 0)
        speed.pack_start(self._hint("Velocidades incluídas no ciclo:"), False, False, 0)
        speed.pack_start(self._cycle_checks("ZoomSpeedModes", ["0", "1", "2", "3", "4"], SPEED_LABELS), False, False, 0)
        speed.pack_start(self._grid([("Atalho do ciclo:", self._shortcut("ZoomSpeedCycle"))]), False, False, 0)
        page.pack_start(self._frame("Velocidade da animação", speed), False, False, 0)

        amount = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        amount.pack_start(self._step_scale("ZoomAmount", AMOUNT_LABELS), False, False, 0)
        amount.pack_start(self._hint("Quanto cada clique da roda muda o zoom. Passos incluídos no ciclo:"), False, False, 0)
        amount.pack_start(self._cycle_checks("ZoomAmountModes", ["0", "1", "2", "3", "4"], AMOUNT_LABELS), False, False, 0)
        amount.pack_start(self._grid([("Atalho do ciclo:", self._shortcut("ZoomAmountCycle"))]), False, False, 0)
        page.pack_start(self._frame("Passo", amount), False, False, 0)

        mouse = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        mods_row = Gtk.Box(spacing=8)
        mods_row.pack_start(Gtk.Label(label="Roda do mouse +"), False, False, 0)
        mod_checks = {}
        for name in ["Ctrl", "Alt", "Shift", "Super"]:
            c = Gtk.CheckButton(label=name)
            mod_checks[name] = c
            mods_row.pack_start(c, False, False, 0)

        def get_mods():
            return "+".join(n for n in ["Ctrl", "Super", "Alt", "Shift"] if mod_checks[n].get_active()) or "Ctrl"

        def set_mods(v):
            chosen = {cf.MODIFIER_NAMES.get(p.strip().lower()) for p in v.split("+")}
            for n, c in mod_checks.items():
                c.set_active(n in chosen)

        self.fields["MouseModifiers"] = (get_mods, set_mods)
        self._mod_checks = mod_checks
        mouse.pack_start(mods_row, False, False, 0)
        mouse.pack_start(self._check("MouseFlip", "Inverter a direção da roda"), False, False, 0)
        page.pack_start(self._frame("Mouse", mouse), False, False, 0)

        keyboard = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        keyboard.pack_start(self._check("KeyboardEnabled", "Atalhos de teclado habilitados"), False, False, 0)
        keyboard.pack_start(self._grid([
            ("Aumentar zoom:", self._shortcut("ZoomIn")),
            ("Diminuir zoom:", self._shortcut("ZoomOut")),
            ("Ligar/desligar zoom:", self._shortcut("ZoomOnToggle")),
        ]), False, False, 0)
        page.pack_start(self._frame("Teclado", keyboard), False, False, 0)

        other = Gtk.Box(spacing=6)
        other.pack_start(self._check("ZoomOffAuto", "Desligar o zoom após"), False, False, 0)
        other.pack_start(self._spin("ZoomOffTimeSeconds", 1, 3600, 1, 0, 10), False, False, 0)
        other.pack_start(Gtk.Label(label="segundos sem movimento"), False, False, 0)
        page.pack_start(self._frame("Outros", other), False, False, 0)
        return page

    def _track_tab(self):
        page = self._page()

        modes = Gtk.Grid(column_spacing=10, row_spacing=6)
        modes.attach(self._hint("Modo atual"), 0, 0, 1, 1)
        modes.attach(self._hint("No ciclo"), 1, 0, 1, 1)
        radios = {}
        checks = []
        group = None
        extras = {
            "Consistent": self._grid([("Tamanho do quadro:", self._float_scale("TrackFrameSize", 0, 2, 0.05, 1.1))]),
            "Border": self._grid([("Área livre no centro:", self._float_scale("TrackBorderSizeBorder", 0, 1, 0.05, 0.5))]),
            "KeyCombo": self._grid([("Área livre no centro:", self._float_scale("TrackBorderSizeKeyCombo", 0, 1, 0.05, 0.5))]),
        }
        region = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        region.pack_start(self._grid([("Ponto X (0 = esquerda):", self._float_scale("TrackLocationX", 0, 1, 0.05, 0.5)),
                                      ("Ponto Y (0 = topo):", self._float_scale("TrackLocationY", 0, 1, 0.05, 0.5))]),
                          False, False, 0)
        zoom_row = Gtk.Box(spacing=6)
        zoom_row.pack_start(self._check("TrackLocationZoom", "Aplicar zoom ao entrar, nível"), False, False, 0)
        zoom_row.pack_start(self._spin("TrackLocationZoomLevel", 0, 5, 0.25, 2, 1), False, False, 0)
        region.pack_start(zoom_row, False, False, 0)
        extras["ScreenLocation"] = region

        row = 1
        for value, label, desc in TRACK_MODES:
            radio = Gtk.RadioButton.new_with_label_from_widget(group, label)
            group = group or radio
            radio.set_tooltip_text(desc)
            radios[value] = radio
            include = Gtk.CheckButton()
            include.set_tooltip_text("Incluir no ciclo do atalho")
            checks.append((value, include))
            modes.attach(radio, 0, row, 1, 1)
            modes.attach(include, 1, row, 1, 1)
            modes.attach(self._hint(desc), 2, row, 1, 1)
            row += 1
            if value in extras:
                extras[value].set_margin_start(24)
                modes.attach(extras[value], 0, row, 3, 1)
                row += 1
        self._radio_field("TrackMode", radios)
        self._checks_field("TrackModes", checks)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.pack_start(modes, False, False, 0)
        box.pack_start(self._grid([("Atalho do ciclo:", self._shortcut("TrackModeCycle"))]), False, False, 0)
        page.pack_start(self._frame("Modo de rastreamento", box), False, False, 0)

        page.pack_start(self._frame("Velocidade com que a vista segue", self._step_scale("TrackSpeedIndex", TRACK_SPEED_LABELS)),
                        False, False, 0)

        other = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        other.pack_start(self._check("TrackFocus", "Seguir o foco do teclado (Tab, menus, botões)"), False, False, 0)
        other.pack_start(self._check("TrackTyping", "Seguir o cursor de texto e a seleção ao digitar"), False, False, 0)
        other.pack_start(self._check("ClampEdge", "Não passar das bordas da tela"), False, False, 0)
        other.pack_start(self._check("AddFrame", "Margem de 2 px nas bordas"), False, False, 0)
        other.pack_start(self._check("PushEdge", "O mouse empurra a vista (nunca sai da área visível)"), False, False, 0)
        other.pack_start(self._check("TrackScreen", "Seguir o monitor onde está o foco"), False, False, 0)
        other.pack_start(self._check(
            "AppAccessibility", "Seguir o teclado também em navegadores (Firefox)",
            "Liga a acessibilidade do sistema para que navegadores mostrem o conteúdo das páginas à lupa.\n"
            "Vale para navegadores abertos depois; pode deixá-los um pouco mais pesados."), False, False, 0)
        page.pack_start(self._frame("Outros", other), False, False, 0)
        return page

    def _colour_tab(self):
        page = self._page()
        modes = Gtk.Grid(column_spacing=10, row_spacing=6)
        modes.attach(self._hint("Modo atual"), 0, 0, 1, 1)
        modes.attach(self._hint("No ciclo"), 1, 0, 1, 1)
        extras = {
            "Invert": self._grid([("Tipo:", self._combo("ColourInvertType", INVERT_TYPES)),
                                  ("Saturação:", self._float_scale("ColourInvertSaturation", 0, 1, 0.05, 0.5))]),
            "Adjust": self._grid([("Tipo:", self._combo("ColourAdjustType", ADJUST_TYPES)),
                                  ("Intensidade:", self._float_scale("ColourAdjustAmount", 0, 1, 0.05, 1)),
                                  ("", self._check("ColourAdjustSimulate", "Simular (mostra como a pessoa enxerga)"))]),
            "Tint": self._grid([("Cor:", self._combo("ColourTint", TINTS)),
                                ("Intensidade:", self._float_scale("ColourTintAmount", 0, 1, 0.05, 0.5)),
                                ("", self._check("ColourTintParty", "Trocar de cor aleatoriamente"))]),
            "Contrast": self._grid([("Cores:", self._combo("ColourContrast", CONTRASTS)),
                                    ("Intensidade:", self._float_scale("ColourContrastAmount", 0, 1, 0.05, 1)),
                                    ("", self._check("ColourContrastInvert", "Inverter claro e escuro"))]),
        }
        radios, checks, group, row = {}, [], None, 1
        for value, label, desc in COLOUR_MODES:
            radio = Gtk.RadioButton.new_with_label_from_widget(group, label)
            group = group or radio
            radios[value] = radio
            include = Gtk.CheckButton()
            include.set_tooltip_text("Incluir no ciclo do atalho")
            checks.append((value, include))
            modes.attach(radio, 0, row, 1, 1)
            modes.attach(include, 1, row, 1, 1)
            modes.attach(self._hint(desc), 2, row, 1, 1)
            row += 1
            if value in extras:
                extras[value].set_margin_start(24)
                modes.attach(extras[value], 0, row, 3, 1)
                row += 1
        self._radio_field("ColourMode", radios)
        self._checks_field("ColourModes", checks)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.pack_start(modes, False, False, 0)
        box.pack_start(self._grid([("Atalho do ciclo:", self._shortcut("ColourModeCycle"))]), False, False, 0)
        page.pack_start(self._frame("Modo de cor", box), False, False, 0)
        page.pack_start(self._frame("Velocidade da transição", self._step_scale("ColourTransitionIndex", TRACK_SPEED_LABELS)),
                        False, False, 0)
        return page

    def _misc_tab(self):
        page = self._page()
        reading = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        reading.pack_start(self._grid([("Ligar/desligar:", self._shortcut("ReadingModeToggle"))]), False, False, 0)
        keys = Gtk.Grid(column_spacing=16, row_spacing=2)
        for r, (k, d) in enumerate([("W / S", "sobe / desce 2 px"), ("Shift+W / Shift+S, F", "linha anterior / próxima"),
                                    ("D / A", "rola para a direita / esquerda (repetir acelera)"),
                                    ("E / Q", "aumenta / diminui a altura da linha"), ("Esc ou clique", "sai")]):
            keys.attach(Gtk.Label(label=k, xalign=0), 0, r, 1, 1)
            keys.attach(self._hint(d), 1, r, 1, 1)
        reading.pack_start(keys, False, False, 0)
        page.pack_start(self._frame("Modo leitura", reading), False, False, 0)

        render = Gtk.Box(spacing=6)
        render.pack_start(Gtk.Label(label="Taxa de atualização:"), False, False, 0)
        render.pack_start(self._spin("RefreshRate", 10, 240, 5, 0, 60), False, False, 0)
        render.pack_start(Gtk.Label(label="quadros por segundo"), False, False, 0)
        page.pack_start(self._frame("Renderização", render), False, False, 0)

        about = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        title = Gtk.Box(spacing=8)
        title.pack_start(Gtk.Image.new_from_pixbuf(GdkPixbuf.Pixbuf.new_from_file_at_size(ICON, 48, 48)),
                         False, False, 0)
        name = Gtk.Label(xalign=0)
        name.set_markup(f"<b>Glassbrick | Lupa de tela</b>\nVersão {VERSION}")
        title.pack_start(name, False, False, 0)
        about.pack_start(title, False, False, 0)
        about.pack_start(self._hint(f"Arquivo de configuração: {self.app.config_path}"), False, False, 0)
        docs = Gtk.LinkButton.new_with_label("file://" + os.path.join(DOCS, "ATALHOS.md"), "Lista de atalhos (docs/ATALHOS.md)")
        docs.set_halign(Gtk.Align.START)
        about.pack_start(docs, False, False, 0)
        page.pack_start(self._frame("Sobre", about), False, False, 0)
        return page

    def _radio_field(self, key, radios):
        def get():
            return next(v for v, r in radios.items() if r.get_active())

        def set_(v):
            (radios.get(v) or next(iter(radios.values()))).set_active(True)

        self.fields[key] = (get, set_)

    def _checks_field(self, key, checks):
        def get():
            return ", ".join(v for v, c in checks if c.get_active())

        def set_(text):
            chosen = {p.strip().lower() for p in text.split(",")}
            for v, c in checks:
                c.set_active(v.lower() in chosen)

        self.fields[key] = (get, set_)

    # --- carregar / gravar ---------------------------------------------------

    def load_values(self, values):
        for key, (_get, set_) in self.fields.items():
            if key in values:
                set_(values[key])

    def read_values(self):
        return {key: get() for key, (get, _set) in self.fields.items()}

    def validate(self, values):
        """Normaliza os atalhos e garante consistência; devolve lista de erros."""
        errors = []
        for action in cf.ACTIONS:
            normalized, err = cf.normalize_binding(values[action], key_exists)
            if err:
                errors.append(f"{ACTION_LABELS[action]} ({action}): {err}")
            else:
                values[action] = normalized
                self.shortcuts[action].set(normalized)
        for a, b, binding in cf.duplicate_bindings({k: values[k] for k in cf.ACTIONS}):
            errors.append(f"{ACTION_LABELS[a]} e {ACTION_LABELS[b]} usam o mesmo atalho ({binding})")
        # O modo atual precisa estar no ciclo (a lupa exige)
        for current, cycle in [("ZoomSpeed", "ZoomSpeedModes"), ("ZoomAmount", "ZoomAmountModes"),
                               ("TrackMode", "TrackModes"), ("ColourMode", "ColourModes")]:
            items = [p.strip() for p in values[cycle].split(",") if p.strip()]
            if values[current] not in items:
                items.append(values[current])
                self.fields[cycle][1](", ".join(items))
                values[cycle] = self.fields[cycle][0]()  # relê na ordem dos campos
        return errors

    def apply(self):
        """Valida, grava e avisa a lupa. Devolve True se deu certo."""
        values = self.read_values()
        errors = self.validate(values)
        if errors:
            self.app.message(self, Gtk.MessageType.ERROR, "Corrija os atalhos antes de salvar", "\n".join(errors))
            return False
        return self.app.save(values)

    def _on_apply(self, _button):
        self.apply()  # a janela continua aberta

    def _on_ok(self, _button):
        if self.apply():
            self.hide()

    def _on_cancel(self, _button):
        self.load_values(self.app.read_config())
        self.app.close_window()

    def _on_close(self, *_):
        self._on_cancel(None)
        return True  # não destrói a janela; só esconde (como o original)

    def _on_restore(self, _button):
        dialog = Gtk.MessageDialog(transient_for=self, modal=True, message_type=Gtk.MessageType.QUESTION,
                                   buttons=Gtk.ButtonsType.YES_NO,
                                   text="Restaurar todas as configurações para os padrões?")
        dialog.format_secondary_text("Os valores só serão gravados ao clicar em Aplicar ou OK.")
        answer = dialog.run()
        dialog.destroy()
        if answer == Gtk.ResponseType.YES:
            defaults = self.app.default_config()
            if defaults:
                self.load_values(defaults)


# ---------------------------------------------------------------------------
# Aplicativo: bandeja + comunicação com a lupa
# ---------------------------------------------------------------------------

class App:
    def __init__(self, args):
        self.pid = args.pid
        self.exe = args.exe
        self.config_path = args.config
        self.window = SettingsWindow(self)
        self.window.load_values(self.read_config())
        self.indicator = None
        if self.pid:
            self._create_tray()
            self._notify_started()
            GLib.timeout_add_seconds(1, self._check_magnifier)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, self.quit)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, self.quit)
        # A lupa pede para mostrar a janela quando alguém tenta abrir uma segunda lupa
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGUSR1, self._on_show_request)
        if args.show or not self.pid:
            self.show_window()

    # --- bandeja ---
    def _create_tray(self):
        menu = Gtk.Menu()
        show = Gtk.MenuItem(label="Mostrar configurações")
        show.connect("activate", lambda *_: self.show_window())
        exit_item = Gtk.MenuItem(label="Sair")
        exit_item.connect("activate", lambda *_: self.exit_magnifier())
        menu.append(show)
        menu.append(Gtk.SeparatorMenuItem())
        menu.append(exit_item)
        menu.show_all()
        try:
            gi.require_version("AyatanaAppIndicator3", "0.1")
            from gi.repository import AyatanaAppIndicator3 as AppIndicator
            self.indicator = AppIndicator.Indicator.new("glassbrick", ICON,
                                                        AppIndicator.IndicatorCategory.APPLICATION_STATUS)
            self.indicator.set_status(AppIndicator.IndicatorStatus.ACTIVE)
            self.indicator.set_title("Glassbrick")
            self.indicator.set_menu(menu)
            self.indicator.set_secondary_activate_target(show)  # clique do meio abre as configurações
        except (ValueError, ImportError):
            # Sem AppIndicator: ícone clássico (XEmbed). Clique abre as configurações
            icon = Gtk.StatusIcon.new_from_file(ICON)
            icon.set_tooltip_text("Glassbrick")
            icon.connect("activate", lambda *_: self.show_window())
            icon.connect("popup-menu", lambda i, b, t: menu.popup(None, None, Gtk.StatusIcon.position_menu, i, b, t))
            self.indicator = icon

    def _notify_started(self):
        # Equivalente ao balão "Glassbrick is now in the system tray"
        try:
            gi.require_version("Notify", "0.7")
            from gi.repository import Notify
            Notify.init("Glassbrick")
            Notify.Notification.new("Glassbrick", "A lupa está ativa. Configurações no ícone da bandeja.", ICON).show()
        except Exception:  # noqa: BLE001 - aviso é opcional
            pass

    def _check_magnifier(self):
        try:
            os.kill(self.pid, 0)
            return True
        except ProcessLookupError:
            self.quit()
            return False

    def _on_show_request(self):
        self.show_window()
        return True  # mantém o tratamento do sinal

    # --- janela ---
    def show_window(self):
        self.window.show_all()
        self.window.present()

    def close_window(self):
        if self.pid:
            self.window.hide()
        else:
            self.quit()

    # --- arquivo ---
    def read_config(self):
        try:
            with open(self.config_path, encoding="utf-8") as f:
                return cf.parse(f.read())
        except FileNotFoundError:
            return self.default_config() or {}

    def default_config_text(self):
        exe = self.exe or os.path.join(HERE, "..", "glassbrick")
        try:
            return subprocess.run([exe, "--print-default-config"], capture_output=True, text=True,
                                  check=True, timeout=5).stdout
        except (OSError, subprocess.SubprocessError) as e:
            self.message(self.window, Gtk.MessageType.ERROR, "Não foi possível obter os padrões", str(e))
            return None

    def default_config(self):
        text = self.default_config_text()
        return cf.parse(text) if text else None

    def save(self, values):
        try:
            try:
                with open(self.config_path, encoding="utf-8") as f:
                    text = f.read()
            except FileNotFoundError:
                text = self.default_config_text() or ""
            os.makedirs(os.path.dirname(self.config_path) or ".", exist_ok=True)
            tmp = self.config_path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(cf.update(text, values))
            problems = self.check_with_magnifier(tmp)
            if problems:
                os.remove(tmp)
                self.message(self.window, Gtk.MessageType.ERROR, "A lupa não aceitou estas configurações",
                             problems)
                return False
            os.replace(tmp, self.config_path)  # grava de uma vez; a lupa nunca lê arquivo pela metade
        except OSError as e:
            self.message(self.window, Gtk.MessageType.ERROR, "Não foi possível salvar", str(e))
            return False
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGHUP)  # a lupa recarrega sem reiniciar
            except ProcessLookupError:
                pass
        return True

    def check_with_magnifier(self, path):
        """Pede à lupa que confira o arquivo; devolve o texto dos problemas ou "".

        A lupa é quem lê o arquivo de verdade, então é ela que decide o que vale
        (evita a interface aceitar um atalho que a lupa ignoraria em silêncio).
        """
        exe = self.exe or os.path.join(HERE, "..", "glassbrick")
        try:
            result = subprocess.run([exe, "--verificar-config", path], capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            return ""  # lupa indisponível: não impede de salvar
        if result.returncode != 1:
            return ""
        text = re.sub(re.escape(path) + r":(\d+):", r"Linha \1:", result.stdout)
        return text.replace(path + ": ", "").strip()

    # --- outros ---
    @staticmethod
    def message(parent, kind, title, text):
        dialog = Gtk.MessageDialog(transient_for=parent, modal=True, message_type=kind,
                                   buttons=Gtk.ButtonsType.OK, text=title)
        dialog.format_secondary_text(text)
        dialog.run()
        dialog.destroy()

    def exit_magnifier(self):
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        self.quit()

    @staticmethod
    def quit(*_):
        Gtk.main_quit()
        return False


def main():
    default_config = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"),
                                  "glassbrick", "glassbrick.conf")
    parser = argparse.ArgumentParser(description="Configurações da Lupa Linux (Glassbrick)")
    parser.add_argument("--pid", type=int, help="PID da lupa (para recarregar e para o menu Sair)")
    parser.add_argument("--config", default=default_config, help="arquivo de configuração")
    parser.add_argument("--exe", help="caminho do executável glassbrick (para obter os padrões)")
    parser.add_argument("--show", action="store_true", help="abre a janela ao iniciar")
    args = parser.parse_args()
    App(args)
    Gtk.main()


if __name__ == "__main__":
    main()
