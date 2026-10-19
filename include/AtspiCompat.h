#pragma once

// Declarações mínimas de glib/libatspi (AT-SPI2, a API de acessibilidade do
// Linux, equivalente ao UI Automation do Windows).
//
// O sistema tem as bibliotecas (libatspi.so.0, libgobject-2.0.so.0) mas não os
// pacotes -dev. Assinaturas conferidas com at-spi-2.0/atspi/*.h. Com
// libatspi2.0-dev instalado, troque por: #include <atspi/atspi.h>

extern "C" {

typedef int gint;
typedef int gboolean;
typedef char gchar;
typedef void* gpointer;
typedef unsigned int GQuark;
typedef void (*GDestroyNotify)(gpointer data);

typedef struct {
    GQuark domain;
    gint code;
    gchar* message;
} GError;

typedef struct _AtspiAccessible AtspiAccessible;
typedef struct _AtspiComponent AtspiComponent;
typedef struct _AtspiText AtspiText;
typedef struct _AtspiEventListener AtspiEventListener;

// Só os campos iniciais de AtspiEvent (o restante inclui um GValue)
typedef struct {
    gchar* type;
    AtspiAccessible* source;
    gint detail1;
    gint detail2;
} AtspiEvent;

typedef struct { gint x, y, width, height; } AtspiRect;
typedef struct { gint start_offset, end_offset; } AtspiRange;

typedef enum { ATSPI_COORD_TYPE_SCREEN = 0 } AtspiCoordType;

typedef void (*AtspiEventListenerSimpleCB)(const AtspiEvent* event);

gint atspi_init(void);
gint atspi_exit(void);
void atspi_event_main(void);
void atspi_event_quit(void);

AtspiEventListener* atspi_event_listener_new_simple(AtspiEventListenerSimpleCB callback,
                                                    GDestroyNotify callback_destroyed);
gboolean atspi_event_listener_register(AtspiEventListener* listener, const gchar* event_type, GError** error);
gboolean atspi_event_listener_deregister(AtspiEventListener* listener, const gchar* event_type, GError** error);

AtspiComponent* atspi_accessible_get_component_iface(AtspiAccessible* obj);
AtspiText* atspi_accessible_get_text_iface(AtspiAccessible* obj);
AtspiRect* atspi_component_get_extents(AtspiComponent* obj, AtspiCoordType ctype, GError** error);
AtspiRect* atspi_text_get_character_extents(AtspiText* obj, gint offset, AtspiCoordType type, GError** error);
gint atspi_text_get_n_selections(AtspiText* obj, GError** error);
AtspiRange* atspi_text_get_selection(AtspiText* obj, gint selection_num, GError** error);
AtspiRect* atspi_text_get_range_extents(AtspiText* obj, gint start_offset, gint end_offset,
                                        AtspiCoordType type, GError** error);

void g_object_unref(gpointer object);
void g_free(gpointer mem);
void g_error_free(GError* error);

} // extern "C"
