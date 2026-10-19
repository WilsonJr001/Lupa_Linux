#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* blink X Y W H RRGGBB RRGGBB ms vezes — abre uma janela sem borda que
   alterna entre as duas cores a cada "ms" milissegundos, "vezes" vezes, e
   termina na 2ª cor (se "vezes" for par, na 1ª). Serve para conferir que a
   lupa redesenha quando o conteúdo de uma janela muda (XDamage).
   Use num servidor aninhado (Xephyr). */
int main(int c, char** v) {
    if (c < 9) { fprintf(stderr, "uso: blink X Y W H RRGGBB RRGGBB ms vezes\n"); return 1; }
    Display* d = XOpenDisplay(0);
    if (!d) return 1;
    int x = atoi(v[1]), y = atoi(v[2]), w = atoi(v[3]), h = atoi(v[4]);
    unsigned long cor[2] = {strtoul(v[5], 0, 16), strtoul(v[6], 0, 16)};
    int ms = atoi(v[7]), vezes = atoi(v[8]);
    XSetWindowAttributes a;
    a.override_redirect = True;
    a.background_pixel = cor[0];
    Window win = XCreateWindow(d, DefaultRootWindow(d), x, y, w, h, 0, CopyFromParent, InputOutput,
                               CopyFromParent, CWOverrideRedirect | CWBackPixel, &a);
    XMapWindow(d, win);
    XSync(d, 0);
    printf("0x%lx\n", win); /* id, para xprop -id */
    fflush(stdout);
    for (int i = 1; i <= vezes; i++) {
        usleep(ms * 1000);
        XSetWindowBackground(d, win, cor[i % 2]);
        XClearWindow(d, win);
        XSync(d, 0);
    }
    pause(); /* mantém a janela aberta até ser morto */
    return 0;
}
