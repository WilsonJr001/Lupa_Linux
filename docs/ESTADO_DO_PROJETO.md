# Lupa Linux — lupa de tela inspirada no Glassbrick

Clone para Linux (X11) da lupa de tela **Glassbrick** (Windows, C#/.NET), escrito em C++17 com Xlib + OpenGL.
O código descompilado do original (`codigo_glassbrick/`) é a referência de comportamento. Ele, o binário
original e o projeto do Ghidra ficam só na máquina de desenvolvimento e **não vão para o repositório**
(são do autor do Glassbrick; ver `.gitignore`).

Este arquivo é o registro vivo do projeto: o que existe, o que foi corrigido e o que falta.
Atualize-o a cada sessão de trabalho.

---

## 1. Como compilar, testar e executar

Requisitos (já presentes nesta máquina): `g++`, `libx11-dev`, Mesa/OpenGL com GLX e as bibliotecas de
runtime `libXext.so.6`, `libXcomposite.so.1`, `libXfixes.so.3`, `libXi.so.6`, `libXrandr.so.2`,
`libXdamage.so.1`, `libatspi.so.0`,
`libgobject-2.0.so.0`, `libglib-2.0.so.0`.

```sh
make            # gera ./lupa_linux
make test       # testes da Camera, do arquivo de configuração e da interface (sem precisar de tela)
./lupa_linux    # lê/cria ~/.config/lupa_linux/lupa_linux.conf e abre o ícone na bandeja
./lupa_linux --sem-interface      # sem ícone na bandeja / janela de configurações
LUPA_LINUX_DEBUG=1 ./lupa_linux   # mostra eventos de foco/caret e do modo leitura
./lupa_linux --diagnostico        # o que a lupa enxerga do ambiente (para relatos de problemas)
```

Requer sessão **X11** com a extensão Composite (testado em XFCE/xfwm4 com compositor ligado,
Intel HD 5500, 1366x768). Wayland não é suportado (ver Limitações).

> Sem os pacotes `-dev` (sem sudo nesta máquina), os protótipos necessários estão em
> `include/XExtCompat.h` (Composite, XFixes, Shape, XInput2, XRandR, XDamage) e `include/AtspiCompat.h` (glib/AT-SPI),
> conferidos com os headers oficiais do SDK flatpak em `~/.local/share/flatpak/runtime/org.freedesktop.Sdk`.
> O link usa `-l:libXcomposite.so.1` etc. Instalando `libxcomposite-dev libxfixes-dev libxext-dev
> libxi-dev libatspi2.0-dev`, dá para trocar pelos headers oficiais.

### Atalhos

A lista completa, com a explicação de cada atalho, está em **[`docs/ATALHOS.md`](ATALHOS.md)**.
Resumo: Ctrl + roda (zoom), Alt + \` (liga/desliga), Alt + 1/2/3/4, Ctrl + \` (rastreamento),
Ctrl + Alt + \` (cor), Alt + R (modo leitura). Todos configuráveis no arquivo de configuração. A tecla \` é a **tecla física à esquerda do 1** (no ABNT2, a `'`).

No terminal (se for TTY): `+ - z t c r q` + Enter.

---

## 2. Arquitetura

```
main.cpp
 ├─ MagnifierLinux   (IMagnifier)  janela overlay + mini-compositor OpenGL
 ├─ Camera                         física de zoom/rastreamento + FocusStep (Camera.cs)
 ├─ Colour                         matrizes de cor animadas (Colour.cs)
 ├─ Reading                        modo leitura (Reading.cs)
 ├─ InputManager                   atalhos (grabs X11) + teclas/cliques (XInput2 raw) (Input.cs)
 ├─ FocusTracker                   foco/caret/seleção de outros apps via AT-SPI2
 └─ UpdateLoop                     loop de 60 Hz (UpdateLoop.cs)

ui/lupa_linux_ui.py  (processo Python/GTK3 aberto pela lupa)  ícone na bandeja + janela de
                     configurações (SettingsForm.cs). OK grava o arquivo e envia SIGHUP à lupa,
                     que recarrega no início do próximo quadro (SwitchToModifiedSettings).
```

**Threads**
- *principal*: inicialização, depois só espera sinal de saída.
- *entrada* (`InputManager`): conexão X própria; recebe teclas/roda "grabbadas", eventos raw do
  XInput2 (toda tecla e clique, sem consumir) e consulta o ponteiro a cada 8 ms (modificadores →
  `TrackModifiersDown/Up`). Enquanto o modo leitura está ativo, também grabba W A S D Q E F Esc.
- *foco* (`FocusTracker`): loop GMainLoop do AT-SPI; chama `Camera::onGuiFocusChanged`,
  `onCaretMoved` e `onTextSelectionChanged`.
- *loop* (`UpdateLoop`): `Camera::updateLoop` → `Colour::colourUpdateStep` → `Reading::updateLoop`
  → `MagnifierLinux::render`. **Todo o OpenGL vive nesta thread**, e também a leitura/movimentação do
  ponteiro usada por Camera e Reading.
- `Camera`, `Colour` e `Reading` têm mutex porque recebem eventos das outras threads.

**Renderização (MagnifierLinux)** — o ponto mais diferente do original:
No Windows, `MagSetFullscreenTransform` faz o sistema ampliar a tela sozinho. No X11 não há API
equivalente, então a lupa é um mini-compositor:
1. Todas as janelas de nível superior são redirecionadas (`XCompositeRedirectSubwindows`, modo
   automático) e cada uma vira uma textura via `GLX_EXT_texture_from_pixmap` (sem cópia pela CPU).
2. Passo 1 (cena, pixels de tela sem ampliação): janelas em ordem de empilhamento, **excluindo a
   janela da lupa**, só na área visível (scissor) → camada do modo leitura (no original é uma janela
   TopMost, por isso é ampliada junto) → cursor (imagem via XFixes; o cursor real fica escondido).
3. Passo 2: a cena é desenhada ampliada na janela overlay com a matriz de cor 5×5 do Windows
   convertida para `mat3 + offset` no shader.
4. A overlay é override-redirect, ocupa a tela, tem região de entrada vazia (cliques passam) e é
   reerguida quando outro menu/dica aparece. Só é mapeada se houver zoom, cor ou leitura.
5. Antes das janelas vem o papel de parede da raiz (`_XROOTPMAP_ID`), para quando não há janela de
   área de trabalho.
6. **Só redesenha quando algo muda**: cada janela tem um `XDamage` (caixa envolvente); dano dentro
   da área visível, eventos de janelas, troca de cursor/papel de parede e `Expose` da lupa marcam a
   cena como suja, e zoom/posição/cor/leitura/ponteiro são comparados com o último quadro. Sem
   mudança, o quadro não é desenhado (o conteúdo anterior continua na tela); por segurança, um
   quadro por segundo é desenhado de qualquer jeito.

**Foco de teclado (Camera::focusStep)**: modo *Mouse* (segue o cursor) ↔ modo *Teclado* (tecla
pressionada sem mexer o mouse → segue o elemento focado, a seleção de texto ou o caret). Mexer o
mouse volta ao modo Mouse; se Alt estiver pressionado nesse momento, o ponteiro pula para o foco.

---

## 3. Mapeamento do original (C#) → C++

| Original (`codigo_glassbrick/Glassbrick/`) | C++ | Estado |
|---|---|---|
| `Program.cs` | `src/main.cpp` | Parcial: sem instância única, sem janela de configurações |
| `UpdateLoop.cs` | `src/UpdateLoop.cpp` | ✅ fiel (inclui desligamento animado de 1 s) + `render()` por quadro |
| `Camera.cs` | `src/Camera.cpp` | ✅ todos os passos, incluindo FocusStep e troca de monitor (ChangedScreen). MoveStep não portado: é código morto no original (ver §4) |
| `Colour.cs`, `Matrix.cs` | `src/Colour.cpp`, `LupaTypes.h` | ✅ fiel (todas as matrizes, "party mode") |
| `Reading.cs` | `src/Reading.cpp` + camada em `MagnifierLinux` | ✅ fiel (faixa, linhas-guia, navegação, pan, fade, cor invertida) |
| `GlassbrickSettings.cs`, `IntModeCycler.cs`, `EnumModeCycler.cs` | `LupaSettings.*`, `LupaTypes.h` | ✅ valores e padrões fiéis (exceto o atalho de cor, trocado de propósito) |
| `Input.cs` (+ `Gma.UserActivityMonitor`) | `src/InputManager.cpp` | ✅ atalhos, Ctrl+roda, modificadores, todas as teclas/cliques (XInput2) |
| UI Automation em `Camera.cs` | `src/FocusTracker.cpp` | ✅ foco, caret e seleção via AT-SPI2 |
| `GuiFocus.cs` | — | Não necessário: no original ninguém assina os eventos dessa classe |
| `FullscreenMagnifier.cs` / `Win7FullscreenMagnifier.cs` | `src/MagnifierLinux.cpp` | ✅ reimplementado como compositor (§2) |
| `WindowedMagnifier.cs`, `OverlayForm.cs` | — | ❌ modo janela não implementado |
| `SettingsManager.cs`, `SettingsFileSettingsManager.cs` | `SettingsManager` + `src/SettingsFile.cpp` | ✅ todas as opções e atalhos em `~/.config/lupa_linux/lupa_linux.conf`. Sem o evento `Modified` (só faz falta quando houver interface) |
| `SettingsForm.cs` (6.3 mil linhas) | `ui/lupa_linux_ui.py` (Python + GTK3) | ✅ 4 abas, OK/Cancelar/Restaurar padrões, ícone na bandeja com Mostrar/Sair, aviso ao iniciar |
| `Glassbrick.Annotations`, `Autofac*`, `GlassbrickEventSource.cs` | — | Não necessário (DI/telemetria) |

---

## 4. Histórico

### Sessão 2026-10-19 (7) — novo nome: Lupa Linux

- O programa deixou de se chamar Glassbrick para deixar claro que é um projeto independente:
  executável `lupa_linux`, interface `ui/lupa_linux_ui.py`, ícone `ui/lupa_linux.png`, configuração
  em `~/.config/lupa_linux/lupa_linux.conf`, trava em `$XDG_RUNTIME_DIR/lupa_linux.lock`, variável
  `LUPA_LINUX_DEBUG`, namespace C++ `LupaLinux`, `LupaSettings.*` / `LupaTypes.h` (antes
  `GlassbrickSettings.*` / `GlassbrickTypes.h`). Janela e bandeja mostram "Lupa Linux".
- Menções ao programa **original** (nomes dos arquivos C#, comportamento "do Glassbrick original")
  continuam com o nome original.
- Migração: na primeira execução, se `~/.config/lupa_linux/lupa_linux.conf` não existe e
  `~/.config/glassbrick/glassbrick.conf` existe, a configuração é copiada
  (`SettingsManager::previousConfigPath`). Testado no Xephyr com um atalho personalizado.
- As entradas antigas deste histórico foram atualizadas para os nomes novos de arquivos e comandos.

### Sessão 2026-10-19 (6) — repositório no GitHub

- Repositório público criado com `README.md` (objetivo do projeto, sem fins lucrativos, uso livre) e
  licença MIT para o código deste projeto.
- Ficam de fora (`.gitignore`): código descompilado, `.exe` original, projeto do Ghidra, binários.
- O ícone da bandeja era o logotipo do Glassbrick (extraído do `app.ico`); foi trocado por um desenho
  próprio (lupa com "A"), gerado com cairo.

### Sessão 2026-10-19 (5) — observações de um testador

Um amigo do usuário testou e relatou quatro problemas (ambiente dele ainda não confirmado; o relato
— "Menu Iniciar", painel e papel de parede sumindo, minimizados aparecendo — bate com **Cinnamon**
(Linux Mint) ou GNOME):

1. **Janelas minimizadas apareciam na lupa** — Mutter/Muffin mantêm mapeadas as janelas minimizadas e
   as de outras áreas de trabalho (para miniaturas) e só deixam de pintá-las. Agora `MagnifierLinux`
   acha a janela do aplicativo dentro da moldura (`findClient`, a que tem `WM_STATE`) e pula as
   escondidas (`isHidden`): `WM_STATE` = Iconic, `_NET_WM_STATE_HIDDEN` ou `_NET_WM_DESKTOP` diferente
   de `_NET_CURRENT_DESKTOP`. Mudanças chegam por `PropertyNotify` (a lupa seleciona
   `PropertyChangeMask` nas janelas dos aplicativos). Testado no Xephyr marcando uma janela mapeada
   com `xprop`: some e volta nos 7 casos (Iconic, Normal, HIDDEN, sem HIDDEN, outra área, troca de
   área, todas as áreas).
2. **Botão "Aplicar"** na janela de configurações: grava e aplica sem fechar a janela (pedido do
   usuário: preferiu o botão a aplicar a cada mudança).
3. **Atalhos não funcionavam com o menu aberto** — o menu captura teclado e mouse (`XGrabKeyboard`) e
   os grabs passivos não disparam. Os eventos raw do XInput2 (versão ≥ 2.1) chegam mesmo assim, então
   `InputManager` guarda o atalho visto pelo raw (`rawShortcut`) e o executa se o evento do grab não
   chegar em 60 ms (`flushPendingShortcuts`); quando o grab funciona, o mesmo toque é descartado
   (`coreShortcut`). Vale para os atalhos de teclado e para Ctrl+roda. Testado no Xephyr com
   `keys grab` (simula o menu): antes nada acontecia, agora Alt+1 e Ctrl+roda dão zoom; sem menu,
   3 toques = 3 ações (sem duplicar).
4. **Atalho do ciclo de cores "não salvava"** — dois defeitos reais:
   - *Gravar* registrava só o modificador ("Control_L") ao pressionar Ctrl+Alt+J, porque
     `event.is_modifier` não vem marcado em todo sistema; ao salvar dava erro. Agora as teclas
     modificadoras também são reconhecidas pelo nome (`config_file.MODIFIER_KEYS`). Reproduzido e
     corrigido no Xephyr.
   - Digitar `Ctrl+Alt+ç` era aceito pela interface (1 caractere em Python) mas recusado em silêncio
     pela lupa (2 bytes em UTF-8), que mantinha o atalho antigo. `keySymFromName` agora aceita um
     caractere UTF-8 (Latin-1 → KeySym igual ao código; demais → KeySym Unicode; maiúscula =
     minúscula). E a interface passou a pedir à lupa que confira o arquivo antes de gravar
     (`--verificar-config`, nova opção): se a lupa recusar, mostra a linha e a opção e não grava.
- **Painel e papel de parede sumindo**: no Cinnamon/GNOME Shell eles são desenhados dentro do
  compositor, sem janela X, e uma aplicação X11 não consegue lê-los sem ler também a própria lupa.
  Nova opção `--diagnostico` (`DesktopInfo`) mostra o que a lupa enxerga (sessão, gerenciador,
  compositor, painéis, janela de área de trabalho, `_XROOTPMAP_ID`); a lupa também avisa na
  inicialização quando não há painel ou papel de parede visível para ela. Solução possível, se o
  ambiente for confirmado: ver próximos passos.
- **Ferramentas**: `keys` ganhou `click`, `wheel`, `type` e `grab`; `blink` imprime o id da janela.
- **Cuidado registrado**: duas vezes um build com erro deixou o binário antigo rodar como lupa na tela
  real (o comando seguinte usava a opção nova, que o antigo não conhece). Conferir o código de saída
  do `make` antes de rodar o binário.

### Sessão 2026-10-19 (4) — redesenho só quando algo muda (XDamage)

- **XDamage** (`trackDamage`/`forgetDamage`/`handleDamage` em `MagnifierLinux`; protótipos em
  `XExtCompat.h`, link `-l:libXdamage.so.1`): um Damage por janela de nível superior (criado quando os
  atributos são lidos) e um no pixmap do papel de parede. Dano fora da área visível da cena é
  ignorado. `render()` só desenha se a cena estiver suja ou se zoom, posição, cor, camada de leitura
  ou ponteiro mudaram (`sameReading` compara a `ReadingOverlay` campo a campo — ao acrescentar um
  campo nela, incluir lá). A lupa agora recebe `Expose` para redesenhar se for descoberta.
  Sem a extensão, continua funcionando e redesenha sempre que a cena muda por outros motivos e ao
  menos uma vez por segundo.
- **Medições** (zoom ligado):
  - Xephyr 800×600 com llvmpipe, tela parada: **101% → 8,4%** de CPU; com uma janela piscando fora da
    vista: 101% → 8,0%; mouse em movimento: 96% → 24%.
  - Tela real (Intel HD 5500, terminal animando dentro da vista): **10,3% → 5,7%**.
- **Redesenho conferido**: janela piscando dentro da vista (ferramenta nova `tests/tools/blink.c`)
  muda de cor na lupa a cada troca, igual à versão que redesenhava sempre.
- Observação de teste: a lupa rodando num Xephyr recebe os eventos AT-SPI da sessão real (o barramento
  de acessibilidade é da sessão), então pode seguir o caret do terminal real. Na hora de medir onde a
  vista está, levar isso em conta.

### Sessão 2026-10-19 (3) — papel de parede sem janela de área de trabalho

- **Fundo da área de trabalho** (`MagnifierLinux::refreshBackground`): o pixmap publicado em
  `_XROOTPMAP_ID` (ou `ESETROOT_PMAP_ID`) por feh/nitrogen/hsetroot/xfdesktop é desenhado na cena antes
  das janelas. Antes, sem uma janela de área de trabalho (xfdesktop desligado, gerenciadores leves), o
  fundo ampliado saía preto. O pixmap não é da lupa: ela só cria o GLXPixmap/textura e nunca chama
  `XFreePixmap`. `PropertyChangeMask` na raiz recria a textura quando o papel de parede é trocado.
- `XSync` antes de contar erros X ao validar o pixmap — erros pendentes de outras requisições fariam a
  validação falhar à toa.
- **Teste em servidor aninhado** (sem mexer na tela real): `Xephyr :5 -screen 800x600 -noreset`,
  raiz pintada de preto, `obj/tools/setbg 00ff00 ffff00 so-prop` (nova ferramenta: publica só a
  propriedade). Com zoom, a lupa mostra verde/amarelo (a raiz real é preta) e a divisa entre as cores
  se desloca conforme o cursor (ampliação confirmada); trocar o papel de parede com a lupa ligada
  atualiza a imagem na hora. `-noreset` é obrigatório: sem ele o Xephyr reinicia quando o último
  cliente sai e o pixmap some.

### Sessão 2026-10-19 (2) — vários monitores

- **Monitores via XRandR 1.5** (`MagnifierLinux::queryMonitors`, `XRRGetMonitors`; protótipos em
  `XExtCompat.h`, link `-l:libXrandr.so.2`). Mudanças na disposição (tela virtual redimensionada →
  `ConfigureNotify` da raiz) redimensionam a overlay e a textura da cena e chamam
  `Camera::setMonitors` / `Reading::setMonitors` (equivale a `SystemEvents.DisplaySettingsChanged`).
- **Camera**: portado `ChangedScreen` e o início de `RegionStep` — quando o foco sai do monitor atual,
  `Region.Focused` passa ao monitor que contém o foco, `Region.Current` = esse monitor (ou o principal
  se `TrackScreen` = false) e o retângulo é animado. O renderizador já aplicava a transformação de tela
  cheia do Windows sobre a tela virtual inteira, então nada mudou nele.
- **Modo leitura**: a camada fica só no monitor onde estava o cursor ao ligar (`Reading.SetWindow`).
- **Testes**: dois monitores simulados em `test_camera` (vista segue o cursor para o outro monitor, não
  mostra conteúdo de um no outro, volta ao principal se um monitor é desconectado, `TrackScreen`
  desligado). Conferido que os testes falham se a troca de monitor for desativada. Na máquina real (um
  monitor) o XRandR detecta o eDP-1 e o modo leitura continua correto. **Sem teste com dois monitores
  físicos** — não há um disponível.
- Chromium retirado dos próximos passos (decisão do usuário: não será testado).

### Sessão 2026-10-19 — instância única, navegadores, ferramentas de teste

- **Instância única** (`src/SingleInstance.cpp`, equivalente ao Mutex "Glassbrick" de `Program.cs`):
  `flock` em `$XDG_RUNTIME_DIR/lupa_linux.lock` (liberado pelo sistema mesmo após `kill -9`; o arquivo
  não é apagado de propósito, para evitar corrida). A segunda lupa avisa e envia SIGUSR1 à primeira,
  que pede à interface para mostrar a janela de configurações (o original traz a janela à frente).
  Verificado: mensagem + código 0, janela aparecendo, lock liberado após `kill -9`.
- **Navegadores (teste)**: no AT-SPI, por padrão, Chromium/Discord/Steam e Firefox expõem só a moldura
  das janelas, sem o conteúdo das páginas. Com `org.a11y.Status.IsEnabled = true`, o Firefox (aberto
  depois) expõe documentos, campos e interface de texto (testado com perfil temporário). Não há Chromium
  de verdade instalado para testar `--force-renderer-accessibility`.
- **Nova opção `AppAccessibility`** (padrão false; não existe no original), com caixa na aba
  Rastreamento: liga `IsEnabled` ao abrir a lupa (`src/A11yStatus.cpp`, via `dbus-send`) e só desliga ao
  sair se foi a lupa que ligou. Verificado nos dois casos.
- **Ferramentas de teste no projeto**: `tests/tools/` (`make tools` → `obj/tools/`): `shot`, `pix`,
  `grabcheck` (verifica se um atalho está capturado sem apertá-lo) e `keys` (XTest). O scratchpad
  temporário é apagado entre sessões; agora elas não se perdem.

### Sessão 2026-10-17 (4) — janela de configurações e ícone na bandeja

- **Interface** `ui/lupa_linux_ui.py` (Python + GTK3, processo separado: não há headers de GTK para
  C++ aqui). Abas do original (Zoom, Rastreamento, Cores, Outros), botões OK / Cancelar / Restaurar
  padrões, campos de atalho com "Gravar" (captura a combinação com grab ativo do teclado) e "Limpar".
  Ícone na bandeja (AyatanaAppIndicator, com fallback Gtk.StatusIcon) com "Mostrar configurações" e
  "Sair", e notificação ao iniciar (o balão do original). Grava preservando comentários
  (`ui/config_file.py`), de forma atômica (arquivo .tmp + rename).
- **Recarga sem reiniciar**: SIGHUP → `SettingsManager::applyPendingReload()` na thread do loop, com
  mutex de configurações (threads de entrada/foco/terminal o seguram; ordem: configurações antes de
  Camera/Colour/Reading). Ouvintes do evento "Modified": cores, câmera e re-registro dos atalhos.
- Lupa abre a interface (`--sem-interface` desliga) e a fecha ao sair; a interface também fecha se a
  lupa morrer. `--print-default-config` fornece os padrões para "Restaurar padrões".
- **Testes**: `tests/test_ui_config.py` (em `make test`) e recarga em `test_settings`. Verificado na
  tela: ícone na bandeja, as 4 abas, recarga aplicando atalho novo e liberando os antigos, interface
  fechando com a lupa (inclusive após `kill -9`).
- **Lição sobre os testes**: as "interrupções sozinhas" do chat eram causadas pelos próprios testes —
  teclas simuladas que a lupa não capturava (Alt+…, F12) iam para o terminal do Claude Code, que as
  recebe como Esc e interrompe. Agora só se simulam teclas capturadas; para checar que um atalho está
  livre usa-se um `XGrabKey` de teste (`grabcheck`), sem apertar nada.

### Sessão 2026-10-17 (3) — configurações completas e atalhos configuráveis

- **Arquivo de configuração completo** (`src/SettingsFile.cpp`): todas as opções de
  `LupaSettings`, os modos de cada ciclo (`ZoomSpeedModes`, `TrackModes`, `ColourModes`…) e os
  8 atalhos, em `~/.config/lupa_linux/lupa_linux.conf` (respeita `$XDG_CONFIG_HOME`; `--config`
  escolhe outro). Formato `Chave = valor` com seções e um comentário explicando cada opção. Valores
  inválidos geram aviso com o número da linha e mantêm o valor anterior. Na primeira execução o
  `glassbrick_config.json` antigo é migrado (sem ser apagado). Documentado em `docs/CONFIGURACAO.md`.
- **Atalhos configuráveis**: `Ação = Ctrl+Alt+grave`, `nenhum` desliga; avisa sobre atalhos repetidos.
- **Atalho de cor trocado** a pedido do usuário: `Ctrl+Alt+grave` em vez de `grave` sozinha (no
  ABNT2 a tecla sozinha impedia digitar o apóstrofo). Verificado na tela: apóstrofo livre e
  Ctrl+Alt+`'` alternando as cores. `ColourModeCycle = grave` restaura o comportamento original.
- **Detalhes do original portados**: trava de 300 ms contra repetição em Ctrl+\` e no atalho de cor
  (`SettingsForm._lastToggleTime`); trocas feitas por atalho não são gravadas (o original só grava no
  OK da janela de configurações). Alt+3/Alt+4 ficam sempre liberados (no original exigem a versão Pro).
- **Testes**: `tests/test_settings.cpp` (38 verificações: padrões, salvar/ler sem perdas, texto de
  atalhos, valores inválidos, formato antigo, caminho padrão).
- **Verificado na tela** com uma configuração temporária (`--config`): `ColourModeCycle =
  Ctrl+Shift+F12` inverte e volta as cores, o atalho antigo deixa de funcionar,
  `ReadingModeToggle = nenhum` desliga o Alt+R e `RefreshRate = 500` gera aviso com o número da linha.

### Sessão 2026-10-17 (2) — testes na tela real, bug do ABNT2, documento de atalhos

- **Testes na tela real refeitos com o mouse parado** (o usuário colaborou): sem nenhum movimento
  espúrio detectado.
  - Modo leitura: linhas-guia nas posições previstas (base + altura·i, conferido por análise de
    pixels), Shift+S pulando exatamente uma linha, E alterando a altura (22→26), zoom ampliando a camada
    e D rolando a vista ~120 px.
  - Foco do teclado: com uma janela GTK de teste, a lupa foi até o caret ao "digitar" e voltou ao
    mouse quando ele se mexeu.
  - Cores: a inversão foi conferida por pixel (`2e2e33` → `b4b4b7`, o valor da matriz Brightness com
    saturação 0,5) e sem piscar (40/40 amostras).
- **Bug do teclado ABNT2 corrigido.** O original usa a tecla virtual 192 (`VK_OEM_3`), que no Windows é
  a tecla física à esquerda do 1 (`` ` `` no americano, `'` no ABNT2). A versão anterior usava o
  *símbolo* `XK_grave`, que no ABNT2 fica na tecla de acento (´/`). Consequências: a lupa capturava a
  tecla de acento (impossível digitar á, é…) e os atalhos `` ` ``, Alt+`` ` `` e Ctrl+`` ` `` nunca
  disparavam (a tecla gera `dead_acute`). Agora a tecla é localizada pela posição física (nome XKB
  `TLDE`) e os atalhos são comparados por keycode. Verificado: tecla de acento livre, `'` alterna a
  cor, Alt+`'` liga o zoom.
- **Novo `docs/ATALHOS.md`**: todos os atalhos com descrição detalhada dos mais complexos (ciclos de
  velocidade e passo, modos de rastreamento e de cor, modo leitura, comportamentos automáticos).

### Sessão 2026-10-17 — modo leitura, foco de teclado, testes

- **Modo leitura portado** (`Reading.cs` → `src/Reading.cpp`): fade in/out (TurnOn/TurnOffTimer),
  faixa transparente de 2 linhas acima do cursor (altura `Line.Height*2*0.8^ZoomLevel`), 11 linhas-guia
  com opacidade `128/1.25^i`, navegação W/S/F/Shift, pan horizontal A/D via `Camera::setReadingOffset`,
  altura Q/E, Esc/clique desliga, fundo branco quando a cor está invertida. A camada é desenhada na
  cena (antes do cursor), então é ampliada junto com a tela, como a janela TopMost do original.
  Movimentos de ponteiro feitos pelo próprio modo leitura não contam como "mouse movido" (como
  `SetCursorPos` no Windows).
- **Descoberta: `MoveStep` é código morto no original** — `MoveCursorKeyDown` é declarado em
  `Camera.cs` mas nunca chamado, então `Move.Active` nunca fica verdadeiro. Não foi portado.
- **Descoberta: `GuiFocus.cs` não tem efeito** — a classe registra handlers de UI Automation, mas
  ninguém assina seus eventos. Quem rastreia o foco é a própria `Camera`.
- **FocusStep completo** com os modos Mouse/Teclado e os tipos Cursor/Gui/Text/Caret, incluindo
  as peculiaridades do original (`KeyCodePrevious` nunca zerado; "isControlDown" na verdade é Alt).
- **XInput2 raw** no `InputManager`: todas as teclas (→ `Camera::onKeyDown/onCharTyped/onKeyUp`,
  `Reading::onOtherKey`) e cliques (→ `Camera::onCursorClicked`, `Reading::onMouseClick`), sem
  consumir os eventos, como os hooks de baixo nível do Windows.
- **FocusTracker (AT-SPI2)**: `object:state-changed:focused`, `object:text-caret-moved`,
  `object:text-selection-changed`. Diferença intencional: o original lia o caret
  (`GetGUIThreadInfo`) só quando uma tecla gerava caractere; aqui o próprio evento de caret dispara
  a atualização (cobre setas, Home/End e cliques no texto).
- **Testes automatizados** (`tests/test_camera.cpp`, `make test`): lupa e cursor simulados, sem X11.
  Cobrem zoom e liga/desliga, rastreamento Centre e bordas, modo Teclado (caret, seleção, elemento
  focado), volta ao mouse, pulo do ponteiro com Alt e TrackFocus/TrackTyping desligados.
- `LUPA_LINUX_DEBUG=1` imprime eventos de foco/caret/seleção e do modo leitura.

Verificado na máquina: modo leitura (faixa, linhas-guia nas posições exatas, ampliação junto com a
tela, Esc), eventos AT-SPI chegando de apps GTK (caret, seleção, foco), saída limpa. Testes da tela
real com o mouse em uso não são determinísticos (qualquer movimento volta ao modo Mouse e apaga as
linhas), por isso a lógica é validada pelos testes automatizados.

### Sessão 2026-10-16 — revisão do trabalho anterior e correções

A versão anterior compilava, mas a lupa não funcionava. Problemas encontrados e corrigidos:

1. **Captura recursiva ("efeito túnel")** — a tela era capturada com `XGetImage`/`XShmGetImage`
   da janela raiz, que inclui a própria overlay da lupa (confirmado com um teste). Cada quadro
   ampliava o anterior. → Renderizador reescrito como compositor XComposite + texture_from_pixmap.
2. **Contexto OpenGL na thread errada** — criado na thread principal e usado na do loop.
   → OpenGL criado sob demanda dentro de `render()`, na thread do loop; `XInitThreads()` no início.
3. **Imagem congelada** — o render só acontecia quando a câmera mudava. → `IMagnifier::render()`
   chamado a cada quadro.
4. **Faltava Ctrl+roda do mouse**. → `XGrabButton` nos botões 4/5 com `MouseModifiers`.
5. **Camera simplificada demais** (KeyCombo, ScreenLocation, Clamp/Frame, AutoOff, `Zoom.Saved`…)
   → portada de novo passo a passo.
6. **Condições de corrida** em Camera/Colour → mutex.
7. **Atalhos inventados** → exatamente os 8 do original.
8. **Cursor** → imagem real via XFixes, ampliada junto com a tela.
9. **Makefile** sem dependência de headers → `-MMD -MP`.

Resultado: zoom 2× com vídeo ao vivo sem efeito túnel, CPU ≈ 8% com zoom e ≈ 2% ocioso (antes do
XDamage; ver sessão 2026-10-19 (4)).

---

## 5. Próximos passos (em ordem sugerida)

1. **Confirmar o ambiente do testador** (pedir a saída de `./lupa_linux --diagnostico`). Se for
   Cinnamon ou GNOME Shell: criar um segundo renderizador que use a lupa do próprio compositor pela
   interface D-Bus `org.gnome.Magnifier` (zoom, posição, inversão/contraste), como o original usa a
   API de ampliação do Windows — assim o painel, o menu e o papel de parede aparecem. A camada do
   modo leitura e parte das cores precisariam de outra solução nesse modo.
2. **Modo janela** (`WindowedMagnifier.cs`) — opcional: lupa numa janela separada em vez de tela cheia.
3. Teste com dois monitores físicos, quando houver um disponível.

---

## 6. Limitações conhecidas

- Vários monitores: implementado e testado com monitores simulados, mas nunca com dois monitores
  físicos. Monitores com escalas (DPI) diferentes não são tratados de forma especial.

- Em ambientes cujo compositor desenha o painel, o menu e o papel de parede sem janela X (Cinnamon,
  GNOME Shell), esses elementos não aparecem na lupa (fundo preto). `--diagnostico` mostra isso.
- **Só X11.** Em Wayland um aplicativo não pode ler outras janelas nem desenhar por cima de tudo.
- O modo Teclado depende de o aplicativo expor acessibilidade (GTK/Qt sim; navegadores só com
  `AppAccessibility`/`--force-renderer-accessibility`).
- Se a lupa for morta com `kill -9` com `AppAccessibility` ligada, o sinal de acessibilidade fica
  ligado até o fim da sessão (inofensivo).
- Na inicialização o AT-SPI pode imprimir `dbind-WARNING ... Unable to open bus connection` por causa
  de sockets de aplicativos já fechados em `/run/user/1000/at-spi2-*`; é inofensivo.
- A interface não reflete trocas feitas por atalho durante a execução (mostra o que está no arquivo).
- Cliques muito rápidos só são perdidos sem XInput2 (fallback por amostragem a cada 8 ms).

## 7. Arquivos auxiliares

- `Lupa_Linux_Project.gpr` / `Lupa_Linux_Project.rep/`: projeto do Ghidra (descompilação) — só local.
- `Glassbrick (Microsoft Store).exe`: binário original — só local.
- `include/XExtCompat.h`, `include/AtspiCompat.h`: protótipos sem os pacotes -dev (ver §1).
- `tests/test_camera.cpp`, `tests/test_settings.cpp`: testes (`make test`).
- `src/SettingsFile.cpp`: leitura/escrita do arquivo de configuração.
- `docs/CONFIGURACAO.md`: todas as opções do arquivo de configuração.
- `docs/ATALHOS.md`: lista de atalhos para o usuário.
- `ui/lupa_linux_ui.py`, `ui/config_file.py`, `ui/lupa_linux.png`: interface, leitura/escrita do arquivo, ícone.
- `tests/test_ui_config.py`: testes da interface sem tela.
- `tests/tools/`: ferramentas para testes na tela (`make tools`); leia o aviso em `keys.c` antes de usar.
  `setbg` define um papel de parede de teste e `blink` abre uma janela que pisca — use só num
  Xephyr (`Xephyr :5 -noreset`, `DISPLAY=:5`). `cpu.sh PID SEGUNDOS` mede o uso de CPU. `keys click`,
  `wheel`, `type` e `grab` (simula um menu aberto) também só no Xephyr.
- `src/DesktopInfo.cpp`: `--diagnostico` e avisos sobre o que o ambiente desenha fora de janelas X.
- `src/SingleInstance.cpp`, `src/A11yStatus.cpp`: instância única e opção AppAccessibility.
