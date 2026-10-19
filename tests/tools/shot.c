/* shot arquivo.ppm — salva um screenshot da tela inteira (inclui a lupa). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
int main(int c, char** v) {
    if (c < 2) { fprintf(stderr, "uso: shot arquivo.ppm\n"); return 1; }
    Display* d = XOpenDisplay(0);
    Window r = DefaultRootWindow(d);
    XWindowAttributes a;
    XGetWindowAttributes(d, r, &a);
    XImage* i = XGetImage(d, r, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    FILE* f = fopen(v[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
    for (int y = 0; y < a.height; y++)
        for (int x = 0; x < a.width; x++) {
            unsigned long p = XGetPixel(i, x, y);
            fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f);
        }
    fclose(f);
    return 0;
}
