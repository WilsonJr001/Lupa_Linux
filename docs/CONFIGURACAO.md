# Configuração da Lupa Linux

## Janela de configurações

A forma mais simples de mudar as opções é pela janela, igual à do Glassbrick original: clique no
**ícone da lupa (azul) na bandeja do sistema** → *Mostrar configurações*. Ela tem as mesmas abas do
original (Zoom, Rastreamento, Cores, Outros) e os botões:

- **Aplicar** — grava o arquivo e a lupa aplica as mudanças **na hora**; a janela continua aberta,
  para experimentar outra opção;
- **OK** — o mesmo que Aplicar, e fecha a janela;
- **Cancelar** (ou fechar a janela) — descarta as mudanças; a janela some, mas o ícone continua na bandeja;
- **Restaurar padrões** — preenche a janela com os valores padrão (só grava ao clicar em Aplicar ou OK).

Antes de gravar, a janela pede à lupa que confira o arquivo (`./lupa_linux --verificar-config arquivo`);
se a lupa recusar algum valor, aparece uma mensagem com a linha e a opção, e nada é gravado.

No menu do ícone, **Sair** encerra a lupa. A janela também pode ser aberta sem a lupa, só para editar o
arquivo: `python3 ui/lupa_linux_ui.py --show`. Para iniciar a lupa sem o ícone na bandeja:
`./lupa_linux --sem-interface`.

O arquivo continua podendo ser editado à mão; a janela preserva os comentários e só troca os valores.

Só uma lupa roda por vez (como no original). Abrir `./lupa_linux` de novo não cria outra: a janela de
configurações da lupa que já está aberta aparece.

## Onde fica o arquivo

`~/.config/lupa_linux/lupa_linux.conf` (ou `$XDG_CONFIG_HOME/lupa_linux/lupa_linux.conf`).

- É criado na primeira execução, com todos os valores padrão e um comentário explicando cada opção.
- Se você usava a versão de quando o programa se chamava `glassbrick`, a configuração de
  `~/.config/glassbrick/glassbrick.conf` é copiada para o lugar novo nessa primeira execução (a antiga
  não é apagada).
- Se existir um `glassbrick_config.json` (formato antigo) na pasta onde o programa foi aberto, os
  valores dele são aproveitados nessa primeira execução. O arquivo antigo não é apagado.
- Para usar outro arquivo: `./lupa_linux --config caminho/do/arquivo.conf`.
- Para voltar aos padrões: apague o arquivo; ele será recriado.

Alterações feitas à mão no arquivo valem na próxima vez que a lupa for aberta, ou na hora com
`kill -HUP <pid da lupa>` (o mesmo sinal que a janela de configurações envia ao clicar em Aplicar ou OK).

> **Trocas feitas por atalho não são salvas.** Assim como no original (que só grava ao clicar OK na
> janela de configurações), trocar o modo com Alt + 3, Alt + 4, Ctrl + `` ` `` etc. vale só até fechar
> o programa. Para mudar o padrão de forma permanente, use a janela de configurações ou edite o arquivo.

## Formato

```ini
# comentário
[Seção]
Chave = valor        # comentário no fim da linha também vale
```

- Booleanos: `true` / `false`. Números com ponto ou vírgula (`0.5` ou `0,5`).
- As seções servem só para organizar; a exceção é `[Atalhos]`, onde a chave é o nome da ação.
- Valor inválido, fora do intervalo ou opção desconhecida: o programa mostra um aviso no terminal com o
  número da linha e mantém o valor anterior. Exemplo:

  ```
  [Config] Aviso: /home/voce/.config/lupa_linux/lupa_linux.conf:10: RefreshRate = 500: fora do intervalo 10–240; mantido o valor anterior
  ```

## Opções

### [Geral]

| Opção | Padrão | Descrição |
|---|---|---|
| `RefreshRate` | 60 | Quadros por segundo da lupa (10–240). |

### [Zoom]

| Opção | Padrão | Descrição |
|---|---|---|
| `ZoomSpeed` | 2 | Velocidade inicial da animação do zoom: 0 lenta, 1 = 1/16, 2 normal, 3 = 1/4, 4 instantânea. |
| `ZoomSpeedModes` | 0, 2, 4 | Velocidades por onde passa o atalho Alt + 3. |
| `ZoomAmount` | 2 | Passo inicial de cada clique, em níveis: 0 = 0,0625, 1 = 0,125, 2 = 0,25, 3 = 0,5, 4 = 1 (o zoom é 2^nível). |
| `ZoomAmountModes` | 0, 2, 4 | Passos por onde passa o atalho Alt + 4. |
| `ZoomOffAuto` | false | Desliga o zoom sozinho depois de um tempo sem mexer o mouse nem mudar o zoom. |
| `ZoomOffTimeSeconds` | 10 | Tempo para o desligamento automático (1–3600 s). |

### [Mouse e teclado]

| Opção | Padrão | Descrição |
|---|---|---|
| `MouseModifiers` | Ctrl | Tecla(s) que, segurada(s), fazem a roda do mouse controlar o zoom. Ex.: `Ctrl+Shift`. Também é a tecla usada pelos modos de rastreamento KeyCombo e ScreenLocation. |
| `MouseFlip` | false | Inverte o sentido da roda. |
| `MouseAccelerate` | true | Sem efeito no Linux: cada clique da roda já é um passo. Mantida por compatibilidade. |
| `KeyboardEnabled` | true | `false` desliga todos os atalhos de teclado (a roda continua funcionando). |

### [Rastreamento]

| Opção | Padrão | Descrição |
|---|---|---|
| `TrackMode` | Centre | Modo inicial: `Centre`, `Consistent`, `Border`, `KeyCombo`, `ScreenLocation`, `None` (explicados em [ATALHOS.md](ATALHOS.md)). |
| `TrackModes` | Centre, Border | Modos por onde passa o atalho Ctrl + `` ` ``. |
| `TrackSpeedIndex` | 2 | Rapidez com que a vista acompanha: 0 (lenta) a 4 (imediata). |
| `TrackBorderSizeBorder` | 0.5 | Modo Border: fração da tela no centro onde o mouse anda sem a vista se mover. |
| `TrackBorderSizeKeyCombo` | 0.5 | Igual, para o modo KeyCombo. |
| `TrackFrameSize` | 1.1 | Modo Consistent: proporção entre o movimento do mouse e o da vista. |
| `TrackLocationX`, `TrackLocationY` | 0.5, 0.5 | Modo ScreenLocation: ponto fixo da tela (fração da largura/altura). |
| `TrackLocationZoom`, `TrackLocationZoomLevel` | false, 1 | Modo ScreenLocation: aplicar automaticamente esse nível de zoom ao entrar no modo. |
| `TrackTyping` | true | Seguir o cursor de texto e o texto selecionado ao digitar. |
| `TrackFocus` | true | Seguir o elemento focado (Tab, menus, botões). |
| `ClampEdge` | true | Não deixar a vista passar das bordas da tela. |
| `AddFrame` | false | Deixar uma margem de 2 px nas bordas. |
| `PushEdge` | true | O mouse "empurra" a vista e nunca fica fora dela. |
| `TrackScreen` | true | Com vários monitores: a área ampliada acompanha o monitor onde está o foco (e não mostra conteúdo de um monitor no outro). Desligado, fica sempre no monitor principal. |
| `AppAccessibility` | false | **Não existe no original.** Liga a acessibilidade da sessão (`org.a11y.Status.IsEnabled`) para que navegadores como o Firefox mostrem o conteúdo das páginas à lupa; assim ela segue o foco e o cursor de texto dentro delas. Vale para navegadores abertos **depois** de ligar; pode deixá-los um pouco mais pesados. Ao sair, a lupa desliga o sinal se foi ela quem ligou. Chromium/Chrome precisam ser abertos com `--force-renderer-accessibility`. |
| `TrackSpeedSame` | true | Guardada por compatibilidade com o original (só usada pela tela de configurações dele). |

### [Cores]

| Opção | Padrão | Descrição |
|---|---|---|
| `ColourMode` | Default | Modo inicial: `Default`, `Invert`, `Adjust`, `Tint`, `Contrast`. |
| `ColourModes` | Default, Invert | Modos por onde passa o atalho de cor (Ctrl + Alt + `` ` ``). Ex.: `Default, Invert, Contrast`. |
| `ColourTransitionIndex` | 2 | Velocidade da transição entre cores: 0 (lenta) a 4 (instantânea). |
| `ColourInvertType` | Brightness | Invert: `Brightness` (inverte o brilho mantendo os tons) ou `Colour` (inverte as cores). |
| `ColourInvertSaturation` | 0.5 | Invert: saturação das cores invertidas (0–1). |
| `ColourAdjustType` | Deuteranomaly | Adjust (daltonismo): `Deuteranomaly` (verde), `Protanomaly` (vermelho), `Tritanomaly` (azul), `Monochromacy` (tons de cinza). |
| `ColourAdjustAmount` | 1 | Adjust: intensidade (0–1). |
| `ColourAdjustSimulate` | false | Adjust: `true` simula a deficiência (para quem enxerga normalmente ver como fica) em vez de corrigir. |
| `ColourTint` | Azure | Tint: `Orange`, `Yellow`, `Lime`, `Green`, `Emerald`, `Cyan`, `Azure`, `Blue`, `Purple`, `Magenta`, `Rose`, `Red`, `Black`. |
| `ColourTintAmount` | 0.5 | Tint: intensidade (0–1). |
| `ColourTintParty` | false | Tint: trocar de cor aleatoriamente de tempos em tempos. |
| `ColourContrast` | BlackAndYellow | Contrast: `FullColour`, `BlackAndWhite`, `BlackAndYellow`, `BlueAndYellow`, `BlueAndWhite`. |
| `ColourContrastAmount` | 1 | Contrast: intensidade (0–1). |
| `ColourContrastInvert` | false | Contrast: inverter claro e escuro. |
| `ColourTransitionSame` | true | Guardada por compatibilidade com o original. |

Exemplo: para alternar entre normal, invertido e alto contraste amarelo sobre preto:

```ini
ColourModes = Default, Invert, Contrast
ColourContrast = BlackAndYellow
```

### [Atalhos]

Um atalho por ação; `nenhum` desliga. Formato, nomes de teclas e cuidados estão em
[ATALHOS.md → Como trocar ou desligar um atalho](ATALHOS.md#como-trocar-ou-desligar-um-atalho).

| Ação | Padrão |
|---|---|
| `ZoomIn` | Alt+1 |
| `ZoomOut` | Alt+2 |
| `ZoomOnToggle` | Alt+grave |
| `ZoomSpeedCycle` | Alt+3 |
| `ZoomAmountCycle` | Alt+4 |
| `TrackModeCycle` | Ctrl+grave |
| `ColourModeCycle` | Ctrl+Alt+grave (no original: `grave` sozinha) |
| `ReadingModeToggle` | Alt+r |

## Não incluído (por enquanto)

- `CurrentRenderMode` (Fullscreen/Windowed): só o modo tela cheia existe no clone.
- `CursorKeysWasd`, `CursorKeysArrows`: pertencem ao recurso de mover o ponteiro pelo teclado, que é
  código morto no original.
- `ProVersion`: no clone não há versão paga; tudo fica liberado.
