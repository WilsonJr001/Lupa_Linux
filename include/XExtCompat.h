#pragma once

// Declarações mínimas das extensões X11 usadas pela lupa.
//
// O sistema de desenvolvimento não tem os pacotes libxcomposite-dev,
// libxfixes-dev e libxext-dev (só as bibliotecas .so de runtime), então os
// protótipos abaixo foram copiados dos headers oficiais (Xcomposite.h,
// Xfixes.h, shape.h). Se os pacotes -dev forem instalados, este arquivo pode
// ser substituído pelos includes oficiais:
//   #include <X11/extensions/Xcomposite.h>
//   #include <X11/extensions/Xfixes.h>
//   #include <X11/extensions/shape.h>
//   #include <X11/extensions/XInput2.h>
//   #include <X11/extensions/Xrandr.h>
//   #include <X11/extensions/Xdamage.h>

#include <X11/Xlib.h>
#include <X11/extensions/shapeconst.h>

extern "C" {

// ---- XComposite ----------------------------------------------------------
#define CompositeRedirectAutomatic 0
#define CompositeRedirectManual    1

Bool   XCompositeQueryExtension(Display* dpy, int* event_base_return, int* error_base_return);
Status XCompositeQueryVersion(Display* dpy, int* major_version_return, int* minor_version_return);
void   XCompositeRedirectSubwindows(Display* dpy, Window window, int update);
void   XCompositeUnredirectSubwindows(Display* dpy, Window window, int update);
void   XCompositeUnredirectWindow(Display* dpy, Window window, int update);
Pixmap XCompositeNameWindowPixmap(Display* dpy, Window window);

// ---- XFixes --------------------------------------------------------------
#define XFixesCursorNotify            1
#define XFixesDisplayCursorNotifyMask (1L << 0)

typedef struct {
    short          x, y;
    unsigned short width, height;
    unsigned short xhot, yhot;
    unsigned long  cursor_serial;
    unsigned long* pixels;   // ARGB pré-multiplicado, 1 pixel por unsigned long
    Atom           atom;
    const char*    name;
} XFixesCursorImage;

Bool   XFixesQueryExtension(Display* dpy, int* event_base_return, int* error_base_return);
Status XFixesQueryVersion(Display* dpy, int* major_version_return, int* minor_version_return);
void   XFixesSelectCursorInput(Display* dpy, Window win, unsigned long eventMask);
XFixesCursorImage* XFixesGetCursorImage(Display* dpy);
void   XFixesHideCursor(Display* dpy, Window win);
void   XFixesShowCursor(Display* dpy, Window win);

// ---- XShape --------------------------------------------------------------
void XShapeCombineRectangles(Display* dpy, Window dest, int dest_kind, int x_off, int y_off,
                             XRectangle* rectangles, int n_rects, int op, int ordering);

// ---- XInput2 (eventos "raw": todas as teclas/cliques, mesmo com grabs) ---
#define XIAllMasterDevices 1
#define XI_RawKeyPress     13
#define XI_RawKeyRelease   14
#define XI_RawButtonPress  15
#define XI_LASTEVENT       32
#define XISetMask(ptr, event) (((unsigned char*)(ptr))[(event) >> 3] |= (1 << ((event) & 7)))
#define XIMaskLen(event)      (((event) >> 3) + 1)

typedef struct {
    int            deviceid;
    int            mask_len;
    unsigned char* mask;
} XIEventMask;

// Só o começo da estrutura (os campos usados); ela é lida, nunca alocada aqui.
typedef struct {
    int           type;
    unsigned long serial;
    Bool          send_event;
    Display*      display;
    int           extension;
    int           evtype;
    Time          time;
    int           deviceid;
    int           sourceid;
    int           detail;   // keycode ou botão
    int           flags;
} XIRawEvent;

Status XIQueryVersion(Display* dpy, int* major_version_inout, int* minor_version_inout);
int    XISelectEvents(Display* dpy, Window win, XIEventMask* masks, int num_masks);

// ---- XRandR 1.5 (lista de monitores) --------------------------------------
typedef XID RROutput;
typedef struct {
    Atom      name;
    Bool      primary;
    Bool      automatic;
    int       noutput;
    int       x, y, width, height;
    int       mwidth, mheight;
    RROutput* outputs;
} XRRMonitorInfo;

Bool XRRQueryExtension(Display* dpy, int* event_base_return, int* error_base_return);
XRRMonitorInfo* XRRGetMonitors(Display* dpy, Window window, Bool get_active, int* nmonitors);
void XRRFreeMonitors(XRRMonitorInfo* monitors);

// ---- XDamage (avisa quando o conteúdo de uma janela muda) -----------------
typedef XID Damage;
typedef XID XserverRegion;
#define XDamageNotify              0
#define XDamageReportBoundingBox   2

typedef struct {
    int           type;
    unsigned long serial;
    Bool          send_event;
    Display*      display;
    Drawable      drawable;
    Damage        damage;
    int           level;
    Bool          more;
    Time          timestamp;
    XRectangle    area;      // retângulo danificado, relativo ao drawable
    XRectangle    geometry;  // geometria do drawable
} XDamageNotifyEvent;

Bool   XDamageQueryExtension(Display* dpy, int* event_base_return, int* error_base_return);
Status XDamageQueryVersion(Display* dpy, int* major_version_return, int* minor_version_return);
Damage XDamageCreate(Display* dpy, Drawable drawable, int level);
void   XDamageDestroy(Display* dpy, Damage damage);
void   XDamageSubtract(Display* dpy, Damage damage, XserverRegion repair, XserverRegion parts);

} // extern "C"
