# Atalhos da Lupa Linux (inspirada no Glassbrick)

Todos os atalhos abaixo são **globais**: funcionam em qualquer janela enquanto o `./lupa_linux` estiver
rodando. São os padrões do Glassbrick original (`GlassbrickSettings.ResetToDefaults`), com uma
exceção: o atalho de cor (ver "Cores"). **Todos podem ser trocados ou desligados** no arquivo de
configuração (ver o fim deste documento).

> **Sobre a tecla `` ` `` nesta lista:** é a **tecla física à esquerda do 1, logo abaixo do Esc**.
> No teclado americano ela tem `` ` `` e `~`; **no ABNT2 (brasileiro) é a tecla `'` / `"`**.
> O programa reconhece a tecla pela posição, como o Windows faz, então funciona em qualquer layout.

---

## Resumo rápido

| Atalho | O que faz |
|---|---|
| **Ctrl + roda do mouse** | Aumenta / diminui o zoom |
| **Alt + `` ` ``** | Liga / desliga o zoom (lembra o último nível) |
| **Alt + 1** | Aumenta o zoom |
| **Alt + 2** | Diminui o zoom |
| **Alt + 3** | Troca a velocidade da animação do zoom |
| **Alt + 4** | Troca o tamanho do passo do zoom |
| **Ctrl + `` ` ``** | Troca o modo de rastreamento (como a lupa segue o mouse) |
| **Ctrl + Alt + `` ` ``** | Troca o modo de cor (normal ↔ invertido) |
| **Alt + R** | Liga / desliga o modo leitura |

Dentro do modo leitura:

| Tecla | O que faz |
|---|---|
| **W** / **S** | Sobe / desce o cursor 2 pixels |
| **Shift + W** / **Shift + S** | Vai para a linha anterior / próxima |
| **F** | Vai para a próxima linha |
| **D** / **A** | Rola a tela para a direita / esquerda (repetir acelera) |
| **E** / **Q** | Aumenta / diminui a altura da linha |
| **Esc** ou **clique** | Sai do modo leitura |

---

## Zoom

### Ctrl + roda do mouse — aumentar / diminuir o zoom
A forma principal de usar a lupa. Cada "clique" da roda para cima aproxima e para baixo afasta.

- O zoom trabalha em **níveis**; o fator de ampliação é 2^nível. Nível 1 = 2×, nível 2 = 4×, até o
  máximo de nível 5 = **32×**.
- Quanto cada clique muda depende do **passo** (ver Alt + 4). No passo padrão (0,25), cada clique
  multiplica o zoom por ~1,19; quatro cliques dobram o zoom.
- A mudança é animada suavemente (ver Alt + 3).
- Ao chegar a 1× (sem zoom) a lupa some sozinha, e o cursor e a tela voltam ao normal.
- **Atenção:** enquanto a lupa está rodando, o Ctrl + roda é "capturado" e não chega aos outros
  programas (por exemplo, o zoom do navegador com Ctrl + roda deixa de funcionar). É assim também no
  original.

### Alt + `` ` `` — liga / desliga o zoom
Alterna entre "sem zoom" e o **último nível usado**. Serve para dar uma olhada rápida na tela inteira e
voltar exatamente ao zoom em que você estava. Na primeira vez (sem nível anterior) liga em 2×.

### Alt + 1 / Alt + 2 — aumentar / diminuir o zoom
Mesmo efeito de um clique da roda com Ctrl, para quem prefere o teclado.

> No Glassbrick original, Alt + 3 e Alt + 4 só funcionam na versão Pro (paga). Neste clone funcionam
> sempre.

### Alt + 3 — velocidade da animação do zoom
Alterna, em ciclo, a rapidez com que o zoom vai até o nível desejado:

| Ordem a partir do padrão | Velocidade |
|---|---|
| (padrão) | **Normal** |
| 1º Alt + 3 | **Instantânea** (sem animação) |
| 2º Alt + 3 | **Lenta** |
| 3º Alt + 3 | volta para Normal |

### Alt + 4 — tamanho do passo do zoom
Alterna quanto cada clique da roda (ou Alt + 1/2) muda o zoom:

| Ordem a partir do padrão | Passo (nível) | Efeito de cada clique |
|---|---|---|
| (padrão) | 0,25 | multiplica por ~1,19 (4 cliques = 2×) |
| 1º Alt + 4 | 1,0 | **dobra** o zoom a cada clique |
| 2º Alt + 4 | 0,0625 | ajuste fino, ~1,04 por clique |
| 3º Alt + 4 | volta para 0,25 | |

---

## Rastreamento — Ctrl + `` ` ``

Define **como a área ampliada acompanha o mouse**. Alterna entre os modos habilitados; por padrão são
dois:

- **Centre (centro)** — padrão. O ponto sob o mouse fica sempre no centro da tela; a imagem se move a
  cada movimento do mouse. Bom para seguir o cursor de perto.
- **Border (borda)** — a imagem fica parada enquanto o mouse se move pela região central e só começa a
  rolar quando ele se aproxima das bordas (a região livre ocupa 50% da tela). Mais confortável para ler
  um trecho sem a tela ficar "nadando".

Em todos os modos a vista para nas bordas da tela (não mostra área preta além do limite) e o mouse nunca
sai da área visível.

O original tem outros modos que existem no código mas **não estão no ciclo por padrão** (precisarão da
tela de configurações para serem ligados):

- **Consistent** — a posição do mouse na tela corresponde proporcionalmente à posição na área ampliada.
- **KeyCombo** — a vista só se move enquanto **Ctrl** está pressionado.
- **ScreenLocation** — a vista fica fixa num ponto da tela; segurando **Ctrl** e mexendo o mouse, você
  "olha em volta" a partir desse ponto.
- **None** — a vista não segue o mouse.

---

## Cores — Ctrl + Alt + `` ` ``

Alterna o efeito de cor aplicado à tela (com ou sem zoom). A transição é suave. Por padrão:

- **Default** — cores normais.
- **Invert** — inverte o **brilho** mantendo os tons (fundos escuros ficam claros e vice-versa; um texto
  vermelho continua avermelhado). Ajuda quem tem sensibilidade à luz ou baixa visão com fundos claros.

O original também tem, fora do ciclo padrão: **Adjust** (correção/simulação de daltonismo:
deuteranomalia, protanomalia, tritanomalia, monocromacia), **Tint** (tingir a tela de uma cor) e
**Contrast** (alto contraste: preto e branco, preto e amarelo, azul e amarelo, azul e branco).

> **Diferença em relação ao original:** no Glassbrick o atalho de cor é a tecla `` ` `` **sozinha**. No
> ABNT2 essa tecla é o apóstrofo `'`, que ficaria impossível de digitar com a lupa aberta. Por isso o
> padrão aqui é **Ctrl + Alt + `` ` ``**. Para voltar ao comportamento original, use no arquivo de
> configuração `ColourModeCycle = grave`.

---

## Modo leitura — Alt + R

Ajuda a ler textos longos sem se perder entre as linhas. Ao ligar, a tela escurece levemente, exceto uma
**faixa clara da altura de duas linhas logo acima do cursor do mouse** (a linha que você está lendo).
Funciona com ou sem zoom; com zoom, a faixa é ampliada junto.

Enquanto o modo leitura está ligado, as teclas abaixo **são capturadas** (não chegam ao programa em
foco). Ao sair, voltam ao normal.

### W / S — ajuste fino vertical
Move o cursor do mouse 2 pixels para cima ou para baixo, para alinhar a faixa exatamente com a linha de
texto. Ao usar, aparecem as **linhas-guia**: 11 linhas horizontais abaixo da faixa, cada vez mais fracas,
espaçadas com a altura de uma linha de texto, para mostrar onde estão as próximas linhas.

### Shift + S, F / Shift + W — próxima linha / linha anterior
O cursor desliza suavemente exatamente **uma altura de linha** para baixo (ou para cima), levando a faixa
para a próxima linha. Se a tela estiver rolada para o lado (ver D/A), ela volta suavemente para o início
da linha, como uma máquina de escrever.

### D / A — rolagem horizontal (leitura contínua)
Faz a área ampliada **rolar sozinha** para a direita (D) ou esquerda (A), para ler uma linha comprida
com zoom sem mover o mouse.
- O primeiro toque começa a rolar devagar.
- Tocar de novo na **mesma direção acelera** (até 4 níveis de velocidade).
- Tocar na **direção oposta freia** (e, chegando a zero, para).
- **W/S** ou mexer o mouse também param a rolagem. Depois de parada, o próximo W/S, F ou movimento do
  mouse faz a vista voltar suavemente para o início da linha.

### E / Q — altura da linha
Aumenta (E) ou diminui (Q) em 1 pixel a altura da linha usada pela faixa, pelas linhas-guia e pelo
"pular linha". Ajuste até as linhas-guia coincidirem com as linhas do texto (de 8 a 96 pixels; padrão 22).

### Esc ou clique do mouse — sair
A camada escura some com uma animação. Mexer o mouse **não** sai do modo leitura: apenas apaga as
linhas-guia (elas voltam ao usar W/S/F).

---

## Comportamentos automáticos (sem atalho próprio)

### Seguir o teclado (foco, cursor de texto e seleção)
Se você **digitar sem mexer o mouse**, a lupa passa a seguir o teclado:
- o **cursor de texto** (caret) enquanto você digita ou navega com as setas;
- o **texto selecionado**;
- o **botão, campo ou item** que recebeu o foco (por exemplo, ao usar Tab).

Assim que o mouse se mexe, a lupa volta a seguir o mouse. Funciona com programas que expõem
acessibilidade (GTK, Qt e a maioria dos aplicativos do XFCE).

**Navegadores:** por padrão o Firefox e os navegadores baseados em Chromium não mostram o conteúdo das
páginas à lupa. Para o Firefox, marque *Seguir o teclado também em navegadores* (aba Rastreamento) e
reabra o navegador. Para Chromium/Chrome, abra-o com `--force-renderer-accessibility`.

### Alt + mexer o mouse — trazer o mouse até o foco
Se a lupa estiver seguindo o teclado e você **mexer o mouse com Alt pressionado**, o ponteiro pula para
onde estava o foco do teclado. Útil para clicar no ponto onde você estava digitando sem procurar o
mouse na tela ampliada.

---

## Como trocar ou desligar um atalho

**Pela janela de configurações (mais fácil):** clique no ícone da lupa (azul) na bandeja do sistema
→ *Mostrar configurações*. Cada atalho tem um campo com os botões **Gravar** (clique e pressione a
combinação desejada; Esc cancela) e **Limpar** (desliga o atalho). Também dá para digitar no campo, por
exemplo `Ctrl+Alt+ç`. Clique em **Aplicar** (a janela continua aberta) ou **OK**: a lupa aplica na
hora, sem precisar reabrir.

Os atalhos funcionam mesmo com um menu aberto (o menu Iniciar, por exemplo), que normalmente prende o
teclado e o mouse só para ele.

**Pelo arquivo:** os atalhos ficam na seção `[Atalhos]` de `~/.config/lupa_linux/lupa_linux.conf`,
criado na primeira execução. Depois de editar, reabra a lupa (ou envie `kill -HUP <pid da lupa>`):

```ini
[Atalhos]
ZoomIn = Alt+1
ZoomOut = Alt+2
ZoomOnToggle = Alt+grave
ZoomSpeedCycle = Alt+3
ZoomAmountCycle = Alt+4
TrackModeCycle = Ctrl+grave
ColourModeCycle = Ctrl+Alt+grave
ReadingModeToggle = Alt+r
```

- Formato: modificadores e tecla separados por `+`. Modificadores: `Ctrl`, `Alt`, `Shift`, `Super`
  (tecla Windows).
- Teclas: letras e números (`a`, `1`), teclas de função (`F5`) e nomes do X11 (`Return`, `space`,
  `Tab`, `Home`…). `grave` é a tecla à esquerda do 1 (no ABNT2, a `'`).
- `nenhum` desliga o atalho, ex.: `ReadingModeToggle = nenhum`.
- Se um atalho estiver errado ou dois atalhos forem iguais, o programa avisa no terminal ao abrir e
  mantém o valor anterior.
- O modificador do zoom com a roda fica em `[Mouse e teclado]`: `MouseModifiers = Ctrl`.
- **Evite combinações já usadas pelo sistema** (no XFCE, por exemplo, Alt+F4 fecha janelas e Alt+F9
  minimiza): enquanto a lupa estiver aberta, ela "pega" o atalho e o sistema deixa de recebê-lo.

As teclas do modo leitura (W, A, S, D, Q, E, F, Esc) são fixas, como no original.

---

## Controles pelo terminal

Se o programa foi aberto num terminal, dá para digitar um comando e apertar Enter:

| Comando | Ação |
|---|---|
| `+` ou `=` | Aumenta o zoom |
| `-` | Diminui o zoom |
| `z` | Liga / desliga o zoom |
| `t` | Troca o modo de rastreamento |
| `c` | Troca o modo de cor |
| `r` | Liga / desliga o modo leitura |
| `q` | Sai do programa (também: Ctrl + C) |

Ao sair, o zoom e as cores voltam ao normal com uma animação de 1 segundo.
