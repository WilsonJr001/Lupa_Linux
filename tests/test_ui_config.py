"""Testes de ui/config_file.py (sem GTK). Rode com `make test`."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ui"))
import config_file as cf  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(f"  [{'ok' if ok else 'FALHOU'}] {what}")
    if not ok:
        failures += 1


KNOWN_KEYS = {"F5", "F12", "Return", "space", "Home", "a", "r", "1"}


def key_exists(name):
    return name in KNOWN_KEYS


def test_with_real_default_file(exe):
    print("Arquivo padrão gerado pela lupa (--print-default-config)")
    text = subprocess.run([exe, "--print-default-config"], capture_output=True, text=True, check=True).stdout
    values = cf.parse(text)
    check(values.get("RefreshRate") == "60", "lê RefreshRate = 60 ignorando o comentário da linha")
    check(values.get("ColourModeCycle") == "Ctrl+Alt+grave", "lê o atalho de cor")
    check(values.get("TrackModes") == "Centre, Border", "lê listas")
    all_keys = {k for keys in cf.SECTIONS.values() for k in keys}
    check(all_keys == set(values), "o mapa de seções da interface tem exatamente as chaves do C++"
          + ("" if all_keys == set(values) else f" (diferença: {all_keys ^ set(values)})"))

    new = cf.update(text, {"RefreshRate": "30", "ColourModeCycle": "Ctrl+Shift+F12", "TrackModes": "Centre"})
    check(cf.parse(new)["RefreshRate"] == "30" and cf.parse(new)["ColourModeCycle"] == "Ctrl+Shift+F12",
          "update troca os valores")
    check("RefreshRate = 30              # quadros por segundo" in new, "update preserva o comentário da linha")
    check(new.count("\n") == text.count("\n"), "update não cria nem apaga linhas")
    check(new.startswith("# Configuração da Lupa Linux"), "cabeçalho preservado")


def test_missing_keys():
    print("Chaves ausentes são criadas na seção certa")
    text = "[Geral]\nRefreshRate = 60\n\n[Atalhos]\nZoomIn = Alt+1\n"
    new = cf.update(text, {"ZoomOut": "Alt+2", "ClampEdge": "true"})
    lines = new.splitlines()
    check(lines.index("ZoomOut = Alt+2") == lines.index("ZoomIn = Alt+1") + 1, "ZoomOut entra em [Atalhos]")
    check("[Rastreamento]" in lines and lines.index("ClampEdge = true") > lines.index("[Rastreamento]"),
          "seção [Rastreamento] criada para ClampEdge")


def test_bindings():
    print("Atalhos")
    n = lambda t: cf.normalize_binding(t, key_exists)  # noqa: E731
    check(n("ctrl + ALT + Grave") == ("Ctrl+Alt+grave", None), "normaliza maiúsculas, espaços e grave")
    check(n("Shift+Alt+Ctrl+R") == ("Ctrl+Alt+Shift+r", None), "ordem dos modificadores como no original")
    check(n("Super+F5") == ("Super+F5", None), "tecla de função")
    check(n("nenhum") == ("nenhum", None) and n("") == ("nenhum", None), "nenhum / vazio desligam")
    check(n("Hyper+a")[0] is None, "modificador inválido")
    check(n("Alt+teclaquenaoexiste")[0] is None, "tecla inválida")
    check(n("Ctrl+Alt_L")[0] is None, "só modificadores")
    d = cf.duplicate_bindings({"ZoomIn": "Alt+1", "ZoomOut": "alt+1", "ZoomOnToggle": "nenhum",
                               "ReadingModeToggle": "nenhum"})
    check(d == [("ZoomIn", "ZoomOut", "alt+1")], "detecta atalho repetido e ignora 'nenhum'")


def test_magnifier_check(exe):
    """--verificar-config: a lupa decide o que vale (a interface usa isso antes de gravar)."""
    import tempfile
    print("Conferência do arquivo pela lupa (--verificar-config)")
    text = subprocess.run([exe, "--print-default-config"], capture_output=True, text=True, check=True).stdout
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "g.conf")

        def run(values):
            with open(path, "w", encoding="utf-8") as f:
                f.write(cf.update(text, values))
            return subprocess.run([exe, "--verificar-config", path], capture_output=True, text=True, timeout=10)

        r = run({})
        check(r.returncode == 0 and r.stdout == "", "arquivo padrão é aceito")
        r = run({"ColourModeCycle": "Ctrl+Alt+ç"})
        check(r.returncode == 0, "atalho com ç é aceito pela lupa")
        r = run({"ColourModeCycle": "Ctrl+Alt+xyz"})
        check(r.returncode == 1 and "ColourModeCycle" in r.stdout and ":80:" in r.stdout,
              "atalho inválido é recusado com a linha e o nome da opção")


if __name__ == "__main__":
    exe = sys.argv[1] if len(sys.argv) > 1 else "./lupa_linux"
    test_with_real_default_file(exe)
    test_missing_keys()
    test_bindings()
    test_magnifier_check(exe)
    if failures:
        print(f"\n{failures} verificação(ões) falharam")
        sys.exit(1)
    print("\nTodos os testes passaram")
