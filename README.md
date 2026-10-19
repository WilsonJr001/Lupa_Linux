# Lupa Linux

Lupa de tela para Linux (X11), feita para ajudar quem tem baixa visão a usar o computador.

Este projeto recria no Linux o comportamento do **Glassbrick**, uma lupa de tela antiga para Windows,
e o adapta para o sistema de janelas X11. O objetivo é **ajudar a comunidade Linux**: pessoas com
deficiência visual, baixa visão ou qualquer pessoa que precise ampliar a tela encontram poucas opções
de lupa no Linux fora do GNOME e do KDE, e aqui a ideia é oferecer uma que funcione em ambientes como
o XFCE.

> **Sem fins lucrativos.** Este projeto não tem nenhum intuito financeiro. Ele é livre para qualquer
> pessoa usar, copiar, estudar, modificar e distribuir (licença [MIT](LICENSE)).

## O que ele faz

- **Ampliação da tela inteira** com zoom suave, controlado pelo teclado (Alt + 1 / Alt + 2) ou por
  Ctrl + roda do mouse.
- **Acompanha o que você está fazendo**: segue o mouse e, ao digitar, segue o cursor de texto e o
  elemento focado (botões, campos, menus), usando a acessibilidade do sistema (AT-SPI).
- **Cores para facilitar a leitura**: inverter cores ou brilho, alto contraste (preto e amarelo,
  azul e amarelo…), filtros para daltonismo e tons.
- **Modo leitura** (Alt + R): escurece a tela e deixa uma faixa clara acompanhando a linha que está
  sendo lida, com linhas-guia.
- **Vários monitores**: a ampliação acompanha o monitor onde está o mouse ou o foco.
- **Janela de configurações** e ícone na bandeja do sistema, com todos os atalhos configuráveis.
- Leve: só redesenha quando algo muda na área ampliada.

Lista completa de atalhos: [docs/ATALHOS.md](docs/ATALHOS.md) · Todas as opções:
[docs/CONFIGURACAO.md](docs/CONFIGURACAO.md)

## Requisitos

- Sessão **X11** (não funciona em Wayland). Testado no XFCE (Xubuntu/Ubuntu).
- Placa de vídeo com OpenGL 3 (qualquer Intel/AMD/NVIDIA recente; também funciona com renderização
  por software, porém mais lenta).

Pacotes (nomes do Debian/Ubuntu/Mint):

```sh
sudo apt install g++ make libx11-dev libgl-dev \
    libxext6 libxcomposite1 libxfixes3 libxi6 libxrandr2 libxdamage1 libatspi2.0-0 \
    python3-gi gir1.2-gtk-3.0 gir1.2-notify-0.7 gir1.2-ayatanaappindicator3-0.1
```

## Como compilar e usar

```sh
git clone https://github.com/WilsonJr001/Lupa_Linux.git
cd Lupa_Linux
make            # gera ./lupa_linux
make test       # (opcional) roda os testes
./lupa_linux    # inicia a lupa e o ícone na bandeja
```

Atalhos principais (padrão):

| Atalho | Ação |
|---|---|
| Alt + 1 / Alt + 2 (ou Ctrl + roda do mouse) | Aumentar / diminuir o zoom |
| Alt + ` (tecla à esquerda do 1) | Ligar / desligar o zoom |
| Ctrl + Alt + ` | Trocar o modo de cor |
| Ctrl + ` | Trocar o modo de acompanhamento |
| Alt + R | Modo leitura |

A configuração fica em `~/.config/lupa_linux/lupa_linux.conf` e pode ser mudada pela janela de
configurações (clique no ícone da bandeja → *Mostrar configurações*).

Se algo não aparecer na lupa, rode `./lupa_linux --diagnostico` e envie o resultado ao abrir uma
issue.

## Limitações conhecidas

- Só X11.
- No **Cinnamon** e no **GNOME Shell**, o painel, o menu e o papel de parede são desenhados pelo
  próprio compositor e ainda não aparecem dentro da lupa.
- Nos navegadores, seguir o foco dentro das páginas depende de a acessibilidade estar ligada (opção
  *AppAccessibility*).

O estado detalhado do desenvolvimento e os próximos passos estão em
[docs/ESTADO_DO_PROJETO.md](docs/ESTADO_DO_PROJETO.md).

## Como contribuir

Relatos de problemas, sugestões e correções são muito bem-vindos — principalmente de quem usa a
lupa no dia a dia. Abra uma *issue* ou envie um *pull request*.

## Sobre o Glassbrick original

O Glassbrick é um programa de terceiros para Windows. Este projeto **não é afiliado** ao seu autor:
é uma reimplementação independente em C++ e Python, feita a partir do estudo do comportamento da
lupa original, para que ela possa ser usada no Linux. Nenhum código, binário ou imagem do programa
original é distribuído neste repositório.

## Licença

[MIT](LICENSE) — use como quiser, inclusive modificar e redistribuir.
