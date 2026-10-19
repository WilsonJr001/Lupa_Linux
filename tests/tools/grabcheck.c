/* grabcheck tecla [mods] — diz se a combinação já está capturada por outro
   programa (ex.: a lupa), SEM apertar nada. mods: C=Ctrl A=Alt S=Shift W=Super.
   Use isto em vez de simular um atalho que pode não estar capturado: a tecla
   iria para o terminal em foco (ex.: o do Claude Code, que a recebe como Esc). */
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
static int err = 0;
static int handler(Display* d, XErrorEvent* e) { (void)d; err = e->error_code; return 0; }
int main(int c, char** v) {
    if (c < 2) { fprintf(stderr, "uso: grabcheck tecla [CASW]\n"); return 1; }
    Display* d = XOpenDisplay(0);
    XSetErrorHandler(handler);
    KeyCode kc = XKeysymToKeycode(d, XStringToKeysym(v[1]));
    unsigned m = 0;
    if (c > 2) {
        if (strchr(v[2], 'C')) m |= ControlMask;
        if (strchr(v[2], 'A')) m |= Mod1Mask;
        if (strchr(v[2], 'S')) m |= ShiftMask;
        if (strchr(v[2], 'W')) m |= Mod4Mask;
    }
    XGrabKey(d, kc, m, DefaultRootWindow(d), False, GrabModeAsync, GrabModeAsync);
    XSync(d, 0);
    printf("%s+%s: %s\n", c > 2 ? v[2] : "-", v[1], err == BadAccess ? "CAPTURADA (atalho ativo)" : "livre");
    if (!err) XUngrabKey(d, kc, m, DefaultRootWindow(d));
    XCloseDisplay(d);
    return 0;
}
