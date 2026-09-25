/*
 * Copyright (C) 2020 The HIME team, Taiwan
 * GTK - The GIMP Toolkit
 * Copyright (C) 2000 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/keysym.h>

#include "gtkimcontexthime.h"
#include "hime-im-client.h"

#define DBG 0

// what the context is attached to: a widget in GTK 4, a window before
#if GTK_CHECK_VERSION(4, 0, 0)
typedef GtkWidget HIMEClient;
#else
typedef GdkWindow HIMEClient;
#endif

struct _GtkIMContextHIME {
    GtkIMContext object;

    HIMEClient *client;

    HIME_client_handle *hime_ch;

    // preedit
    char *pe_str;
    HIME_PREEDIT_ATTR *pe_attr;
    int pe_attrN;
    int pe_cursor;
    gboolean pe_started;
};

static const int BUFFER_SIZE = 256;

static GObjectClass *parent_class;

// GObject functions
static void gtk_im_context_hime_class_init (GtkIMContextHIMEClass *class);
static void gtk_im_context_hime_init (GtkIMContextHIME *im_context_hime);
static void gtk_im_context_hime_finalize (GObject *obj);

// GtkIMContext functions
static void gtk_im_context_hime_set_client (GtkIMContext *context,
                                            HIMEClient *client);
static void gtk_im_context_hime_get_preedit_string (GtkIMContext *context,
                                                    gchar **str,
                                                    PangoAttrList **attrs,
                                                    gint *cursor_pos);
#if GTK_CHECK_VERSION(4, 0, 0)
static gboolean gtk_im_context_hime_filter_keypress (GtkIMContext *context,
                                                     GdkEvent *event);
#else
static gboolean gtk_im_context_hime_filter_keypress (GtkIMContext *context,
                                                     GdkEventKey *event);
#endif
static void gtk_im_context_hime_focus_in (GtkIMContext *context);
static void gtk_im_context_hime_focus_out (GtkIMContext *context);
static void gtk_im_context_hime_reset (GtkIMContext *context);
static void gtk_im_context_hime_set_cursor_location (GtkIMContext *context,
                                                     GdkRectangle *area);
static void gtk_im_context_hime_set_use_preedit (GtkIMContext *context,
                                                 gboolean use_preedit);

static void add_preedit_attr (PangoAttrList *attrs,
                              const gchar *str,
                              HIME_PREEDIT_ATTR *hime_attr);

GType gtk_type_im_context_hime = 0;

void gtk_im_context_hime_register_type (GTypeModule *type_module) {
    static const GTypeInfo im_context_hime_info = {
        sizeof (GtkIMContextHIMEClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) gtk_im_context_hime_class_init,
        NULL, /* class_finalize */
        NULL, /* class_data */
        sizeof (GtkIMContextHIME),
        0,
        (GInstanceInitFunc) gtk_im_context_hime_init,
    };

    gtk_type_im_context_hime =
        g_type_module_register_type (type_module,
                                     GTK_TYPE_IM_CONTEXT,
                                     "GtkIMContextHIME",
                                     &im_context_hime_info, 0);
}

GtkIMContext *gtk_im_context_hime_new (void) {
    GtkIMContextHIME *result = GTK_IM_CONTEXT_HIME (
        g_object_new (GTK_TYPE_IM_CONTEXT_HIME, NULL));

    return GTK_IM_CONTEXT (result);
}

/**
 * gtk_im_context_hime_shutdown:
 *
 * Destroys all the status windows that are kept by the HIME contexts.  This
 * function should only be called by the HIME module exit routine.
 **/
void gtk_im_context_hime_shutdown (void) {
}

static void gtk_im_context_hime_class_init (GtkIMContextHIMEClass *class) {
    GtkIMContextClass *im_context_class = GTK_IM_CONTEXT_CLASS (class);
    GObjectClass *gobject_class = G_OBJECT_CLASS (class);

    parent_class = g_type_class_peek_parent (class);

#if GTK_CHECK_VERSION(4, 0, 0)
    im_context_class->set_client_widget = gtk_im_context_hime_set_client;
#else
    im_context_class->set_client_window = gtk_im_context_hime_set_client;
#endif
    im_context_class->get_preedit_string = gtk_im_context_hime_get_preedit_string;
    im_context_class->filter_keypress = gtk_im_context_hime_filter_keypress;
    im_context_class->focus_in = gtk_im_context_hime_focus_in;
    im_context_class->focus_out = gtk_im_context_hime_focus_out;
    im_context_class->reset = gtk_im_context_hime_reset;
    im_context_class->set_cursor_location = gtk_im_context_hime_set_cursor_location;
    im_context_class->set_use_preedit = gtk_im_context_hime_set_use_preedit;

    gobject_class->finalize = gtk_im_context_hime_finalize;
}

static void
init_preedit (GtkIMContextHIME *im_context_hime) {
    if (!im_context_hime) {
        return;
    }

    im_context_hime->pe_str = NULL;
    im_context_hime->pe_attr = NULL;
    im_context_hime->pe_attrN = 0;
    im_context_hime->pe_cursor = 0;
    im_context_hime->pe_started = FALSE;
}

static void
gtk_im_context_hime_init (GtkIMContextHIME *im_context_hime) {
    im_context_hime->client = NULL;
    im_context_hime->hime_ch = NULL;
    init_preedit (im_context_hime);
}

void clear_preedit (GtkIMContextHIME *context_hime) {
    if (!context_hime) {
        return;
    }

    if (context_hime->pe_str) {
        free (context_hime->pe_str);
        context_hime->pe_str = NULL;
    }

    if (context_hime->pe_attr) {
        free (context_hime->pe_attr);
        context_hime->pe_attr = NULL;
        context_hime->pe_attrN = 0;
    }

    context_hime->pe_cursor = 0;
    context_hime->pe_started = FALSE;
}

static void gtk_im_context_hime_finalize (GObject *obj) {
    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (obj);

    clear_preedit (context_xim);

    if (context_xim->hime_ch) {
        hime_im_client_close (context_xim->hime_ch);
        context_xim->hime_ch = NULL;
    }

    context_xim->client = NULL;

    parent_class->finalize (obj);
}

// The X display, or NULL on other GDK backends (Wayland): the client
// library then reaches the daemon through its socket path alone.
static Display *get_x_display (GdkDisplay *display) {
#if GTK_CHECK_VERSION(3, 0, 0)
    if (!GDK_IS_X11_DISPLAY (display)) {
        return NULL;
    }
#endif
#if GTK_CHECK_VERSION(4, 0, 0)
    return gdk_x11_display_get_xdisplay (display);
#else
    return GDK_DISPLAY_XDISPLAY (display);
#endif
}

// Tell the daemon the client's X window, which lets it place its window at
// the cursor.  Clients on other backends have none.
static void update_client_window (GtkIMContextHIME *context_xim) {
    if (!context_xim->hime_ch || !context_xim->client) {
        return;
    }

#if GTK_CHECK_VERSION(4, 0, 0)
    GtkNative *native = gtk_widget_get_native (context_xim->client);
    GdkSurface *surface = native ? gtk_native_get_surface (native) : NULL;
    if (!surface || !GDK_IS_X11_SURFACE (surface)) {
        return;
    }
    hime_im_client_set_client_window (context_xim->hime_ch,
                                      gdk_x11_surface_get_xid (surface));
#else
#if GTK_CHECK_VERSION(3, 0, 0)
    if (!GDK_IS_X11_WINDOW (context_xim->client)) {
        return;
    }
#endif
    hime_im_client_set_client_window (context_xim->hime_ch,
                                      GDK_WINDOW_XID (context_xim->client));
#endif
}

#if GTK_CHECK_VERSION(4, 0, 0)
// GDK 4 modifier masks match the X core ones for Shift, Lock, Control and
// Alt (Mod1); Super is a separate bit, which hime expects as Mod4.
static uint32_t x_modifier_state (GdkModifierType state) {
    uint32_t x_state = state & (GDK_SHIFT_MASK | GDK_LOCK_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK);
    if (state & GDK_SUPER_MASK) {
        x_state |= Mod4Mask;
    }
    return x_state;
}
#endif

static void get_hime_im_client (GtkIMContextHIME *context_xim) {

    if (!context_xim->client) {
        return;
    }

    GdkDisplay *display = gdk_display_get_default ();
    if (!display) {
        return;
    }

    if (!context_xim->hime_ch) {
        context_xim->hime_ch = hime_im_client_open (get_x_display (display));
        if (!context_xim->hime_ch) {
            perror ("cannot open hime_ch");
        }

        init_preedit (context_xim);
    }
}

static void gtk_im_context_hime_set_client (GtkIMContext *context,
                                            HIMEClient *client) {
    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (context);

    context_xim->client = client;

    if (!client) {
        return;
    }

    get_hime_im_client (context_xim);
    update_client_window (context_xim);
}

static void gtk_im_context_hime_get_preedit_string (
    GtkIMContext *context,
    gchar **str,
    PangoAttrList **attrs,
    gint *cursor_pos) {
    GtkIMContextHIME *context_hime = GTK_IM_CONTEXT_HIME (context);

    if (context_hime->hime_ch && cursor_pos) {
        int ret = 0;
        hime_im_client_set_flags (context_hime->hime_ch,
                                  FLAG_HIME_client_handle_use_preedit, &ret);
    }

    if (str) {
        // hime client handle not present, return an empty string
        if (!context_hime->hime_ch) {
            *str = g_strdup ("");
        } else {
            // return preedit buffer if any,
            // otherwise, return an empty string
            if (context_hime->pe_str) {
                *str = g_strdup (context_hime->pe_str);
            } else {
                *str = g_strdup ("");
            }
        }
    }

    if (cursor_pos) {
        *cursor_pos = context_hime->pe_cursor;
    }

    if (attrs) {
        *attrs = pango_attr_list_new ();
        for (int i = 0; i < context_hime->pe_attrN; i++) {
            add_preedit_attr (*attrs, *str, &(context_hime->pe_attr[i]));
        }
    }
}

#if GTK_CHECK_VERSION(4, 0, 0)
static gboolean gtk_im_context_hime_filter_keypress (GtkIMContext *context,
                                                     GdkEvent *event) {
    const gboolean press = gdk_event_get_event_type (event) == GDK_KEY_PRESS;
    const guint keyval = gdk_key_event_get_keyval (event);
    const uint32_t state = x_modifier_state (gdk_event_get_modifier_state (event));
#else
static gboolean gtk_im_context_hime_filter_keypress (GtkIMContext *context,
                                                     GdkEventKey *event) {
    const gboolean press = event->type == GDK_KEY_PRESS;
    const guint keyval = event->keyval;
    const uint32_t state = event->state;
#endif
    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (context);

    // text of the key, committed as is when hime does not take the key
    gchar buffer[BUFFER_SIZE];

    // TRUE if the input method handled the key event.
    // No further processing should be done for this key event for Gtk.
    gboolean result = FALSE;

    // the final result of preediting to be commited
    char *result_str = NULL;

    // GDK key values are X keysyms, already translated for the modifier
    // state; hime works with those on every backend.
    const KeySym keysym = keyval;

    // Convert from a GDK key symbol to the corresponding ISO10646 (Unicode) character.
    // returns 0 if there is no corresponding character.
    gsize num_bytes = 0;
    const guint32 unicode = gdk_keyval_to_unicode (keyval);
    if (unicode) {
        num_bytes = g_unichar_to_utf8 (unicode, buffer);
    }
    buffer[num_bytes] = '\0';

    // tell hime-im-client to process key event
    // result_str would hold the result
    gboolean context_has_str = context_xim->pe_str && context_xim->pe_str[0];
    if (press) {
        result = hime_im_client_forward_key_press (context_xim->hime_ch,
                                                   keysym, state, &result_str);
    } else {
        result = hime_im_client_forward_key_release (context_xim->hime_ch,
                                                     keysym, state, &result_str);
    }

    char *preedit_str = NULL;
    HIME_PREEDIT_ATTR attr[HIME_PREEDIT_ATTR_MAX_N];
    int cursor_pos = 0;
    int sub_comp_len = 0;
    int attrN = hime_im_client_get_preedit (context_xim->hime_ch,
                                            &preedit_str, attr, &cursor_pos, &sub_comp_len);
    gboolean has_preedit_str = preedit_str && preedit_str[0];
    if (sub_comp_len) {
        has_preedit_str = TRUE;
    }

    if (!context_xim->pe_started && has_preedit_str) {
        g_signal_emit_by_name (context, "preedit-start");
        context_xim->pe_started = TRUE;
    }

    // preedit_str and pe_str hold different strings
    const gboolean different_str = preedit_str &&
                                   context_xim->pe_str &&
                                   (strcmp (preedit_str, context_xim->pe_str) != 0);

    // update preedit string
    if (context_has_str != has_preedit_str || different_str) {
        if (context_xim->pe_str) {
            free (context_xim->pe_str);
        }
        context_xim->pe_str = preedit_str;
    }

    size_t attrsz = sizeof (HIME_PREEDIT_ATTR) * attrN;

    // pe_attr and attr hold different data
    const gboolean different_attr = context_xim->pe_attr &&
                                    (memcmp (context_xim->pe_attr, attr, attrsz) != 0);

    // update pe_attr
    if (context_xim->pe_attrN != attrN || different_attr) {
        context_xim->pe_attrN = attrN;

        if (context_xim->pe_attr) {
            free (context_xim->pe_attr);
        }
        context_xim->pe_attr = NULL;

        if (attrsz) {
            context_xim->pe_attr = malloc (attrsz);

            if (context_xim->pe_attr) {
                memcpy (context_xim->pe_attr, attr, attrsz);
            }
        }
    }

    // update pe_cursor
    if (context_xim->pe_cursor != cursor_pos) {
        context_xim->pe_cursor = cursor_pos;
    }

    const gboolean alt_or_control_pressed = state & (Mod1Mask | ControlMask);
    // GDK_KEY_PRESS event
    // hime_im_client_forward_key_press returns False
    // result_str is empty
    // buffer[0] is printable
    // not alt_or_control_pressed
    if (press &&
        !result &&
        !result_str &&
        num_bytes &&
        isprint (buffer[0]) &&
        !alt_or_control_pressed) {

        // copy buffer into result_str
        result_str = (char *) malloc (num_bytes + 1);
        memcpy (result_str, buffer, num_bytes);
        result_str[num_bytes] = 0;
        result = TRUE;
    }

    if (result_str) {
        g_signal_emit_by_name (context, "commit", result_str);
        free (result_str);
    }

    gboolean preedit_changed = different_str;
    if (preedit_changed) {
        g_signal_emit_by_name (context, "preedit-changed");
    }

    if (!has_preedit_str && context_xim->pe_started) {
        clear_preedit (context_xim);
        g_signal_emit_by_name (context, "preedit-end");
    }

    return result;
}

static void gtk_im_context_hime_focus_in (GtkIMContext *context) {
    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (context);

    // a GTK 4 widget may have been realized since it was attached
    update_client_window (context_xim);

    if (context_xim->hime_ch) {
        hime_im_client_focus_in (context_xim->hime_ch);
    }
}

static void gtk_im_context_hime_focus_out (GtkIMContext *context) {
    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (context);

    if (context_xim->hime_ch) {
        char *result_str = NULL;
        hime_im_client_focus_out2 (context_xim->hime_ch, &result_str);

        if (result_str) {
            g_signal_emit_by_name (context, "commit", result_str);
            clear_preedit (context_xim);
            g_signal_emit_by_name (context, "preedit-changed");
            free (result_str);
        }
    }
}

static void gtk_im_context_hime_set_cursor_location (GtkIMContext *context,
                                                     GdkRectangle *area) {
    if (!area) {
        return;
    }

    GtkIMContextHIME *context_xim = GTK_IM_CONTEXT_HIME (context);

    if (!context_xim->hime_ch) {
        get_hime_im_client (context_xim);
    }

    int x = area->x;
    int y = area->y + area->height;

#if GTK_CHECK_VERSION(4, 0, 0)
    // area is relative to the client widget; the daemon expects coordinates
    // relative to the window, i.e. the native surface
    GtkNative *native = context_xim->client ? gtk_widget_get_native (context_xim->client) : NULL;
    graphene_point_t point;
    if (native &&
        gtk_widget_compute_point (context_xim->client, GTK_WIDGET (native),
                                  &GRAPHENE_POINT_INIT (x, y), &point)) {
        double surface_x = 0, surface_y = 0;
        gtk_native_get_surface_transform (native, &surface_x, &surface_y);
        x = point.x + surface_x;
        y = point.y + surface_y;
    }
#endif

    if (context_xim->hime_ch) {
        hime_im_client_set_cursor_location (context_xim->hime_ch, x, y);
    }
}

static void gtk_im_context_hime_set_use_preedit (GtkIMContext *context,
                                                 gboolean use_preedit) {
    GtkIMContextHIME *context_hime = GTK_IM_CONTEXT_HIME (context);

    if (!context_hime->hime_ch) {
        return;
    }

    int ret = 0;

    if (use_preedit) {
        hime_im_client_set_flags (context_hime->hime_ch,
                                  FLAG_HIME_client_handle_use_preedit, &ret);
    } else {
        hime_im_client_clear_flags (context_hime->hime_ch,
                                    FLAG_HIME_client_handle_use_preedit, &ret);
    }
}

static void gtk_im_context_hime_reset (GtkIMContext *context) {
    GtkIMContextHIME *context_hime = GTK_IM_CONTEXT_HIME (context);

    if (context_hime->hime_ch) {
        hime_im_client_reset (context_hime->hime_ch);
        clear_preedit (context_hime);
        g_signal_emit_by_name (context, "preedit-changed");
    }
}

/*
 * Mask of feedback bits that we render
 */
static void add_preedit_attr (PangoAttrList *attrs,
                              const gchar *str,
                              HIME_PREEDIT_ATTR *hime_attr) {
    PangoAttribute *attr = NULL;
    gint start_index = g_utf8_offset_to_pointer (str, hime_attr->ofs0) - str;
    gint end_index = g_utf8_offset_to_pointer (str, hime_attr->ofs1) - str;

    if (hime_attr->flag & HIME_PREEDIT_ATTR_FLAG_UNDERLINE) {
        attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);
        attr->start_index = start_index;
        attr->end_index = end_index;
        pango_attr_list_change (attrs, attr);
    }

    if (hime_attr->flag & HIME_PREEDIT_ATTR_FLAG_REVERSE) {
        const guint16 rgb_min = 0x0000;
        const guint16 rgb_max = 0xffff;

        // set foreground = white
        attr = pango_attr_foreground_new (rgb_max, rgb_max, rgb_max);
        attr->start_index = start_index;
        attr->end_index = end_index;
        pango_attr_list_change (attrs, attr);

        // set background = black
        attr = pango_attr_background_new (rgb_min, rgb_min, rgb_min);
        attr->start_index = start_index;
        attr->end_index = end_index;
        pango_attr_list_change (attrs, attr);
    }
}
