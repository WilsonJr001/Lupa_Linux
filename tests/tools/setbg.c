#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

/* setbg RRGGBB RRGGBB [so-prop] — define um papel de parede de teste na raiz (metade
   esquerda com a 1ª cor, direita com a 2ª) e publica em _XROOTPMAP_ID, como
   fazem feh/nitrogen/hsetroot. Com "so-prop" só publica a propriedade, sem
   pintar a raiz: o que aparecer na tela foi desenhado pela lupa.
   Use num servidor aninhado iniciado com -noreset (Xephyr :5 -noreset), senão
   o servidor reinicia quando o último cliente sai e o pixmap some. */
int main(int c, char** v) {
    if (c < 3) { fprintf(stderr, "uso: setbg RRGGBB RRGGBB\n"); return 1; }
    Display* d = XOpenDisplay(0);
    if (!d) return 1;
    int s = DefaultScreen(d);
    Window r = RootWindow(d, s);
    int w = DisplayWidth(d, s), h = DisplayHeight(d, s);
    Pixmap p = XCreatePixmap(d, r, w, h, DefaultDepth(d, s));
    GC gc = XCreateGC(d, p, 0, 0);
    XSetForeground(d, gc, strtoul(v[1], 0, 16));
    XFillRectangle(d, p, gc, 0, 0, w / 2, h);
    XSetForeground(d, gc, strtoul(v[2], 0, 16));
    XFillRectangle(d, p, gc, w / 2, 0, w - w / 2, h);
    XChangeProperty(d, r, XInternAtom(d, "_XROOTPMAP_ID", False), XA_PIXMAP, 32, PropModeReplace,
                    (unsigned char*)&p, 1);
    if (c < 4) {
        XSetWindowBackgroundPixmap(d, r, p);
        XClearWindow(d, r);
    }
    XSetCloseDownMode(d, RetainPermanent); /* o pixmap sobrevive ao fim do programa */
    XCloseDisplay(d);
    return 0;
}
