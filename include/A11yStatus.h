#pragma once

#include <optional>

namespace LupaLinux {

// Liga/desliga o sinal de acessibilidade da sessão (propriedade
// org.a11y.Status.IsEnabled do barramento org.a11y.Bus). Com ele ligado, o
// Firefox expõe o conteúdo das páginas ao AT-SPI e a lupa consegue seguir o
// foco e o cursor de texto dentro delas (opção AppAccessibility).
//
// Só restaura o que ela mesma mudou: se o sinal já estava ligado (por um leitor
// de tela, por exemplo), a lupa não o desliga ao sair.
class A11yStatus {
public:
    ~A11yStatus() { restore(); }

    void apply(bool wanted);
    void restore() { apply(false); }

    static std::optional<bool> isEnabled();

private:
    static bool setEnabled(bool value);
    bool _changedByUs = false;
};

} // namespace LupaLinux
