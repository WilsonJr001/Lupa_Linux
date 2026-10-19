#pragma once

#include "LupaTypes.h"
#include <array>

namespace LupaLinux {

// Camada do modo leitura (equivalente à janela Reading.cs do original, que é
// um formulário "TopMost" semitransparente e por isso é ampliado junto com a
// tela). Todas as coordenadas são em pixels de tela, sem ampliação.
// Ao acrescentar um campo, incluí-lo também em sameReading (MagnifierLinux.cpp).
struct ReadingOverlay {
    bool visible = false;
    float opacity = 0.0f;                 // Form.Opacity (0..0.33)
    float back[3] = {0.0f, 0.0f, 0.0f};   // Form.BackColor (preto, ou branco se cores invertidas)

    // Área da janela Reading: o monitor onde estava o cursor ao ligar (SetWindow)
    int areaX = 0, areaY = 0, areaWidth = 0, areaHeight = 0;

    // FocusPanel: faixa totalmente transparente (TransparencyKey) acima do cursor
    int bandX = 0, bandY = 0, bandWidth = 0, bandHeight = 0;

    // DrawLines: 11 linhas abaixo da faixa, alpha = LineOpacity / 1.25^i
    bool linesVisible = false;
    float linesBaseY = 0.0f;
    int lineHeight = 22;
    int lineOpacity = 128;
    float lineColour[3] = {1.0f, 1.0f, 1.0f};
    bool doubleLines = false;             // linha com 2 px quando ZoomLevel < 1
};

// Interface abstrata do Renderizador da Lupa (equivalente a IMagnifier.cs)
//
// Diferença em relação ao original: no Windows, MagSetFullscreenTransform é
// persistente e o próprio sistema redesenha a tela ampliada. No Linux nós
// somos o compositor da lupa, então render() precisa ser chamado a cada
// quadro pelo UpdateLoop (na mesma thread que chamou initialise()).
class IMagnifier {
public:
    virtual ~IMagnifier() = default;

    virtual void initialise() = 0;
    virtual void update(float zoomFactor, float x, float y) = 0;
    virtual void updateColour(const std::array<float, 25>& colourArray) = 0;
    virtual void setReadingOverlay(const ReadingOverlay& overlay) = 0;
    virtual void render() = 0;
    virtual void shutdown() = 0;
};

} // namespace LupaLinux
