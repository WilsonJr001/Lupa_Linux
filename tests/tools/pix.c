/* pix X Y — imprime a cor (RRGGBB) de um pixel da tela. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
int main(int c, char** v) {
    if (c < 3) { fprintf(stderr, "uso: pix X Y\n"); return 1; }
    Display* d = XOpenDisplay(0);
    XImage* i = XGetImage(d, DefaultRootWindow(d), atoi(v[1]), atoi(v[2]), 1, 1, AllPlanes, ZPixmap);
    printf("%06lx\n", XGetPixel(i, 0, 0) & 0xffffff);
    return 0;
}
