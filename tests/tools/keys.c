/* keys — simula teclado/mouse com XTest, para testes na tela.
     keys key <tecla>                 ex.: keys key s
     keys shiftkey <tecla>            Shift + tecla
     keys combo <mod1,mod2> <tecla>   ex.: keys combo Control_L,Shift_L F12
     keys alt<c>                      Alt + caractere (ex.: keys alt1, keys altr)
     keys ctrlup | ctrldown           Ctrl + roda do mouse
     keys move X Y                    move o ponteiro
     keys click X Y                   clique esquerdo em X Y
     keys wheel X Y N                 N cliques da roda em X Y (N<0 sobe)
     keys grab SEGUNDOS               captura teclado e mouse (como um menu aberto)
     keys type texto                  digita letras, números, + e espaço
   ATENÇÃO: só simule atalhos que a lupa esteja capturando (confira com
   grabcheck). Teclas não capturadas vão para a janela em foco; se for o
   terminal do Claude Code, Alt+... e F1-F12 viram Esc e interrompem o chat.
   click, wheel, type e grab: só num servidor aninhado (Xephyr, DISPLAY=:5). */
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
static void tap(Display* d, KeyCode k) {
    XTestFakeKeyEvent(d, k, 1, 0); XSync(d, 0); usleep(60000); XTestFakeKeyEvent(d, k, 0, 0);
}
int main(int c, char** v) {
    if (c < 2) { fprintf(stderr, "uso: veja o comentário em tests/tools/keys.c\n"); return 1; }
    Display* d = XOpenDisplay(0);
    KeyCode alt = XKeysymToKeycode(d, XK_Alt_L), ctl = XKeysymToKeycode(d, XK_Control_L);
    if (!strcmp(v[1], "move") && c > 3) {
        XTestFakeMotionEvent(d, -1, atoi(v[2]), atoi(v[3]), 0);
    } else if (!strcmp(v[1], "click") && c > 3) {
        XTestFakeMotionEvent(d, -1, atoi(v[2]), atoi(v[3]), 0); XSync(d, 0); usleep(80000);
        XTestFakeButtonEvent(d, 1, 1, 0); XSync(d, 0); usleep(60000);
        XTestFakeButtonEvent(d, 1, 0, 0);
    } else if (!strcmp(v[1], "grab") && c > 2) {
        Window r = DefaultRootWindow(d);
        int k = XGrabKeyboard(d, r, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        int p = XGrabPointer(d, r, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        printf("grab teclado=%s mouse=%s\n", k == GrabSuccess ? "ok" : "falhou", p == GrabSuccess ? "ok" : "falhou");
        fflush(stdout);
        XSync(d, 0);
        sleep(atoi(v[2]));
    } else if (!strcmp(v[1], "wheel") && c > 4) {
        XTestFakeMotionEvent(d, -1, atoi(v[2]), atoi(v[3]), 0); XSync(d, 0); usleep(80000);
        int n = atoi(v[4]), b = n < 0 ? 4 : 5;
        for (int i = 0; i < abs(n); i++) {
            XTestFakeButtonEvent(d, b, 1, 0); XTestFakeButtonEvent(d, b, 0, 0); XSync(d, 0); usleep(30000);
        }
    } else if (!strcmp(v[1], "type") && c > 2) {
        KeyCode sh = XKeysymToKeycode(d, XK_Shift_L);
        for (const char* p = v[2]; *p; p++) {
            char name[2] = {*p, 0};
            KeySym ks = *p == '+' ? XK_plus : *p == ' ' ? XK_space : XStringToKeysym(name);
            KeyCode k = XKeysymToKeycode(d, ks);
            int shift = (*p >= 'A' && *p <= 'Z') || XkbKeycodeToKeysym(d, k, 0, 0) != ks;
            if (shift) XTestFakeKeyEvent(d, sh, 1, 0);
            tap(d, k);
            if (shift) XTestFakeKeyEvent(d, sh, 0, 0);
            XSync(d, 0); usleep(30000);
        }
    } else if ((!strcmp(v[1], "key") || !strcmp(v[1], "shiftkey")) && c > 2) {
        KeyCode k = XKeysymToKeycode(d, XStringToKeysym(v[2]));
        KeyCode sh = XKeysymToKeycode(d, XK_Shift_L);
        int s = !strcmp(v[1], "shiftkey");
        if (s) XTestFakeKeyEvent(d, sh, 1, 0);
        tap(d, k);
        if (s) XTestFakeKeyEvent(d, sh, 0, 0);
    } else if (!strcmp(v[1], "combo") && c > 3) {
        char buf[64]; strncpy(buf, v[2], 63); buf[63] = 0;
        KeyCode mods[4]; int n = 0;
        for (char* t = strtok(buf, ","); t && n < 4; t = strtok(0, ",")) mods[n++] = XKeysymToKeycode(d, XStringToKeysym(t));
        for (int i = 0; i < n; i++) XTestFakeKeyEvent(d, mods[i], 1, 0);
        tap(d, XKeysymToKeycode(d, XStringToKeysym(v[3])));
        for (int i = n - 1; i >= 0; i--) XTestFakeKeyEvent(d, mods[i], 0, 0);
    } else if (!strncmp(v[1], "alt", 3) && strlen(v[1]) == 4) {
        XTestFakeKeyEvent(d, alt, 1, 0);
        tap(d, XKeysymToKeycode(d, (KeySym)(unsigned char)v[1][3]));
        XTestFakeKeyEvent(d, alt, 0, 0);
    } else if (!strcmp(v[1], "ctrlup") || !strcmp(v[1], "ctrldown")) {
        int b = !strcmp(v[1], "ctrlup") ? 4 : 5;
        XTestFakeKeyEvent(d, ctl, 1, 0); XSync(d, 0); usleep(50000);
        XTestFakeButtonEvent(d, b, 1, 0); XTestFakeButtonEvent(d, b, 0, 0); XSync(d, 0); usleep(50000);
        XTestFakeKeyEvent(d, ctl, 0, 0);
    } else {
        fprintf(stderr, "comando desconhecido\n");
        return 1;
    }
    XSync(d, 0);
    XCloseDisplay(d);
    return 0;
}
