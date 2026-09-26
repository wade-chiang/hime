/*
 * Copyright (C) 2020 The HIME team, Taiwan
 * Copyright (C) 2011 Edward Der-Hua Liu, Hsin-Chu, Taiwan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version 2.1
 * of the License.
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

#include <signal.h>

#include "hime.h"

#include "im-srv.h"
#include "win-gtab.h"
#include "win-kbm.h"
#include "win-pho.h"
#include "win0.h"
#include "win1.h"

Window root;

int input_window_width, input_window_height;
int win_x, win_y;  // actual win x/y

// display width/height in pixels
// display_width and display_height are global variable shared across files
int display_width, display_height;

gboolean key_press_shift;

Window xim_xwin;

extern unich_t *fullchar[];
gboolean win_kbm_inited;

char *half_char_to_full_char (KeySym xkey) {
    if (xkey < ' ' || xkey > 127)
        return NULL;
    return _ (fullchar[xkey - ' ']);
}

// The hidden X window that owns the XIM server and the selections; there is
// none without an X display.
static void start_inmd_window () {
    if (!dpy) {
        return;
    }

    GtkWidget *win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_realize (win);
    xim_xwin = GDK_WINDOW_XWINDOW (gtk_widget_get_window (win));
    dbg ("xim_xwin %x\n", xim_xwin);
}

#if USE_XIM
char *lc;

static XIMStyle Styles[] = {
#if 1
    XIMPreeditCallbacks | XIMStatusCallbacks,  // OnTheSpot
    XIMPreeditCallbacks | XIMStatusArea,       // OnTheSpot
    XIMPreeditCallbacks | XIMStatusNothing,    // OnTheSpot
#endif
    XIMPreeditPosition | XIMStatusArea,     // OverTheSpot
    XIMPreeditPosition | XIMStatusNothing,  // OverTheSpot
    XIMPreeditPosition | XIMStatusNone,     // OverTheSpot
#if 1
    XIMPreeditArea | XIMStatusArea,     // OffTheSpot
    XIMPreeditArea | XIMStatusNothing,  // OffTheSpot
    XIMPreeditArea | XIMStatusNone,     // OffTheSpot
#endif
    XIMPreeditNothing | XIMStatusNothing,  // Root
    XIMPreeditNothing | XIMStatusNone,     // Root
};
static XIMStyles im_styles;

#if 1
static XIMTriggerKey trigger_keys[] = {
    {XK_space, ControlMask, ControlMask},
    {XK_space, ShiftMask, ShiftMask},
    {XK_space, Mod1Mask, Mod1Mask},  // Alt
    {XK_space, Mod4Mask, Mod4Mask},  // Windows
};
#endif

/* Supported Encodings */
static XIMEncoding chEncodings[] = {
    "COMPOUND_TEXT",
    0};
static XIMEncodings encodings;

int xim_ForwardEventHandler (IMForwardEventStruct *call_data);

XIMS current_ims;
extern void toggle_im_enabled ();

int MyTriggerNotifyHandler (IMTriggerNotifyStruct *call_data) {
    //    dbg("MyTriggerNotifyHandler %d %x\n", call_data->key_index, call_data->event_mask);

    if (call_data->flag == 0) { /* on key */
                                //        db(g("trigger %d\n", call_data->key_index);
        if ((call_data->key_index == 0 && hime_im_toggle_keys == Control_Space) ||
            (call_data->key_index == 3 && hime_im_toggle_keys == Shift_Space) ||
            (call_data->key_index == 6 && hime_im_toggle_keys == Alt_Space) ||
            (call_data->key_index == 9 && hime_im_toggle_keys == Windows_Space)) {
            toggle_im_enabled ();
        }
        return True;
    } else {
        /* never happens */
        return False;
    }
}

#if 0
void switch_IC_index(int index);
#endif
void CreateIC (IMChangeICStruct *call_data);
void DeleteIC (CARD16 icid);
void SetIC (IMChangeICStruct *call_data);
void GetIC (IMChangeICStruct *call_data);
int xim_hime_FocusIn (IMChangeFocusStruct *call_data);
int xim_hime_FocusOut (IMChangeFocusStruct *call_data);

int hime_ProtoHandler (XIMS ims, IMProtocol *call_data) {
    //  dbg("hime_ProtoHandler %x ims\n", ims);

    current_ims = ims;

    switch (call_data->major_code) {
    case XIM_OPEN:
#define MAX_CONNECT 20000
    {
        IMOpenStruct *pimopen = (IMOpenStruct *) call_data;

        if (pimopen->connect_id > MAX_CONNECT - 1)
            return True;

#if DEBUG && 0
        dbg ("open lang %s  connectid:%d\n", pimopen->lang.name, pimopen->connect_id);
#endif
        return True;
    }
    case XIM_CLOSE:
#if DEBUG && 0
        dbg ("XIM_CLOSE\n");
#endif
        return True;
    case XIM_CREATE_IC:
#if DEBUG && 0
        dbg ("CREATE_IC\n");
#endif
        CreateIC ((IMChangeICStruct *) call_data);
        return True;
    case XIM_DESTROY_IC: {
        IMChangeICStruct *pimcha = (IMChangeICStruct *) call_data;
#if DEBUG && 0
        dbg ("DESTROY_IC %d\n", pimcha->icid);
#endif
        DeleteIC (pimcha->icid);
    }
        return True;
    case XIM_SET_IC_VALUES:
#if DEBUG && 0
        dbg ("SET_IC\n");
#endif
        SetIC ((IMChangeICStruct *) call_data);
        return True;
    case XIM_GET_IC_VALUES:
#if DEBUG && 0
        dbg ("GET_IC\n");
#endif
        GetIC ((IMChangeICStruct *) call_data);
        return True;
    case XIM_FORWARD_EVENT:
#if DEBUG && 0
        dbg ("XIM_FORWARD_EVENT\n");
#endif
        return xim_ForwardEventHandler ((IMForwardEventStruct *) call_data);
    case XIM_SET_IC_FOCUS:
#if DEBUG && 0
        dbg ("XIM_SET_IC_FOCUS\n");
#endif
        return xim_hime_FocusIn ((IMChangeFocusStruct *) call_data);
    case XIM_UNSET_IC_FOCUS:
#if DEBUG && 0
        dbg ("XIM_UNSET_IC_FOCUS\n");
#endif
        return xim_hime_FocusOut ((IMChangeFocusStruct *) call_data);
    case XIM_RESET_IC:
#if DEBUG && 0
        dbg ("XIM_UNSET_IC_FOCUS\n");
#endif
        return True;
    case XIM_TRIGGER_NOTIFY:
#if DEBUG && 0
        dbg ("XIM_TRIGGER_NOTIFY\n");
#endif
        MyTriggerNotifyHandler ((IMTriggerNotifyStruct *) call_data);
        return True;
    case XIM_PREEDIT_START_REPLY:
#if DEBUG && 1
        dbg ("XIM_PREEDIT_START_REPLY\n");
#endif
        return True;
    case XIM_PREEDIT_CARET_REPLY:
#if DEBUG && 1
        dbg ("XIM_PREEDIT_CARET_REPLY\n");
#endif
        return True;
    case XIM_STR_CONVERSION_REPLY:
#if DEBUG && 1
        dbg ("XIM_STR_CONVERSION_REPLY\n");
#endif
        return True;
    default:
        printf ("Unknown major code.\n");
        break;
    }

    return True;
}

void open_xim () {
    XIMTriggerKeys triggerKeys;

    im_styles.supported_styles = Styles;
    im_styles.count_styles = sizeof (Styles) / sizeof (Styles[0]);

    triggerKeys.count_keys = sizeof (trigger_keys) / sizeof (trigger_keys[0]);
    triggerKeys.keylist = trigger_keys;

    encodings.count_encodings = sizeof (chEncodings) / sizeof (XIMEncoding) - 1;
    encodings.supported_encodings = chEncodings;

    char *xim_name = get_hime_xim_name ();

    XIMS xims = IMOpenIM (dpy,
                          IMServerWindow, xim_xwin,  // input window
                          IMModifiers, "Xi18n",      // X11R6 protocol
                          IMServerName, xim_name,    // XIM server name
                          IMLocale, lc,
                          IMServerTransport, "X/",    // Comm. protocol
                          IMInputStyles, &im_styles,  // faked styles
                          IMEncodingList, &encodings,
                          IMProtocolHandler, hime_ProtoHandler,
                          IMFilterEventMask, KeyPressMask | KeyReleaseMask,
                          IMOnKeysList, &triggerKeys,
                          NULL);

    if (xims == NULL) {
        p_err ("IMOpenIM '%s' failed. Maybe another XIM server is running.\n",
               xim_name);
    }
}

#endif  // if USE_XIM

void load_tsin_db ();
void load_tsin_conf (), load_settings (), load_tab_pho_file ();

void disp_hide_tsin_status_row (), update_win_kbm_inited ();
void change_tsin_line_color (), change_win0_style ();
void change_win_gtab_style ();
#if TRAY_ENABLED
void update_item_active_all ();
#endif
void destroy_inmd_menu ();
void load_gtab_list (gboolean);
void change_win1_font ();
void set_wselkey (char *s);

#if TRAY_ENABLED
void disp_tray_icon ();
#endif
gboolean init_in_method (int in_no);
#include "hime-protocol.h"
#include "im-srv.h"

static int get_in_method_by_filename (char filename[]) {
    int i, in_method = 0;
    gboolean found = FALSE;
    for (i = 0; i < inmdN; i++) {
        if (strcmp (filename, inmd[i].filename))
            continue;
        found = TRUE;
        in_method = i;
        break;
    }
    if (!found)
        in_method = default_input_method;
    return in_method;
}

static void reload_data () {
    dbg ("reload_data\n");
    //  Save input method state before reload
    char temp_inmd_filenames[hime_clientsN][128];
    gboolean temp_CS_im_enabled[hime_clientsN];
    char temp_current_CS_inmd_filename[128] = "";
    gboolean temp_current_CS_im_enabled = FALSE;
    if (current_CS) {
        temp_current_CS_im_enabled = current_CS->b_im_enabled;
        strcpy (temp_current_CS_inmd_filename, inmd[current_CS->in_method].filename);
    }
    int c;
    for (c = 0; c < hime_clientsN; c++) {
        strcpy (temp_inmd_filenames[c], "");
        temp_CS_im_enabled[c] = FALSE;
        if (!hime_clients[c].cs)
            continue;
        ClientState *cs = hime_clients[c].cs;
        temp_CS_im_enabled[c] = cs->b_im_enabled;
        strcpy (temp_inmd_filenames[c], inmd[cs->in_method].filename);
    }
    free_omni_config ();
    load_settings ();
    if (current_method_type () == method_type_TSIN)
        set_wselkey (pho_selkey);

    //  load_tsin_db();
    change_win0_style ();
    change_win1_font ();
    change_win_gtab_style ();
    //  change_win_pho_style();
    load_tab_pho_file ();
    update_win_kbm_inited ();

    destroy_inmd_menu ();
    load_gtab_list (TRUE);

#if TRAY_ENABLED
    update_item_active_all ();
#endif

    //  Load input method state after reload, which may change inmd
    // load clientstate properties back
    for (c = 0; c < hime_clientsN; c++) {
        if (!hime_clients[c].cs)
            continue;
        hime_clients[c].cs->b_im_enabled = TRUE;
        hime_clients[c].cs->in_method = get_in_method_by_filename (temp_inmd_filenames[c]);
        init_in_method (hime_clients[c].cs->in_method);
        if (!temp_CS_im_enabled[c])
            toggle_im_enabled ();
        hime_clients[c].cs->b_im_enabled = temp_CS_im_enabled[c];
    }
    current_CS->b_im_enabled = TRUE;
    init_in_method (get_in_method_by_filename (temp_current_CS_inmd_filename));
    if (!temp_current_CS_im_enabled)
        toggle_im_enabled ();
    current_CS->b_im_enabled = temp_current_CS_im_enabled;
}

void change_tsin_font_size ();
void change_gtab_font_size ();
void change_pho_font_size ();
void change_win_sym_font_size ();
void change_win_gtab_style ();
extern gboolean win_kbm_on;
extern void change_module_font_size ();

static void change_font_size () {
    load_settings ();
    change_tsin_font_size ();
    change_gtab_font_size ();
    change_pho_font_size ();
    change_win_sym_font_size ();
    change_win0_style ();
    change_win_gtab_style ();
    update_win_kbm_inited ();
    change_win1_font ();
    //  change_win_pho_style();
    change_module_font_size ();
}

static int xerror_handler (Display *d, XErrorEvent *eve) {
    return 0;
}

Atom hime_atom;

void toggle_gb_output ();

void cb_trad_sim_toggle () {
    toggle_gb_output ();
#if TRAY_ENABLED
    disp_tray_icon ();
#endif
}
void execute_message (char *message);
void disp_win_kbm_capslock_init ();

extern int hime_show_win_kbm;
void kbm_open_close (GtkButton *checkmenuitem, gboolean b_show) {
    hime_show_win_kbm = b_show;

    if (hime_show_win_kbm) {
        show_win_kbm ();
        disp_win_kbm_capslock_init ();
    } else
        hide_win_kbm ();
}

void kbm_toggle () {
    win_kbm_inited = 1;
    kbm_open_close (NULL, !hime_show_win_kbm);
}

void reload_tsin_db ();
void do_exit ();

void message_cb (char *message) {
    void sim_output ();   // FIXME
    void trad_output ();  // FIXME
                          //   dbg("message '%s'\n", message);

    /* TODO: rewrite the mess with case() ? */
    if (!strcmp (message, CHANGE_FONT_SIZE)) {
        change_font_size ();
    } else if (!strcmp (message, GB_OUTPUT_TOGGLE)) {
        cb_trad_sim_toggle ();
#if TRAY_ENABLED
        update_item_active_all ();
#endif
    } else if (!strcmp (message, SIM_OUTPUT_TOGGLE)) {
        sim_output ();
#if TRAY_ENABLED
        disp_tray_icon ();
        update_item_active_all ();
#endif
    } else if (!strcmp (message, TRAD_OUTPUT_TOGGLE)) {
        trad_output ();
#if TRAY_ENABLED
        disp_tray_icon ();
        update_item_active_all ();
#endif
    } else if (!strcmp (message, KBM_TOGGLE)) {
        kbm_toggle ();
    } else if (strstr (message, "#hime_message")) {
        execute_message (message);
    } else if (!strncmp (message, "#hime_test ", 11) && getenv ("HIME_TEST_HOOKS")) {
        hime_test_hook (message + 11);
    } else
#if TRAY_ENABLED
        if (!strcmp (message, UPDATE_TRAY)) {
        disp_tray_icon ();
    } else
#endif
        if (!strcmp (message, RELOAD_TSIN_DB)) {
        reload_tsin_db ();
    } else if (!strcmp (message, HIME_EXIT_MESSAGE)) {
        do_exit ();
    } else
        reload_data ();
}

static GdkFilterReturn my_gdk_filter (GdkXEvent *xevent,
                                      GdkEvent *event,
                                      gpointer data) {
    XEvent *xeve = (XEvent *) xevent;
#if 0
   dbg("a zzz %d\n", xeve->type);
#endif

    // only very old WM will enter this
    if (xeve->type == FocusIn || xeve->type == FocusOut) {
#if 0
     dbg("focus %s\n", xeve->type == FocusIn ? "in":"out");
#endif
        return GDK_FILTER_REMOVE;
    }

#if USE_XIM
    if (XFilterEvent (xeve, None) == True)
        return GDK_FILTER_REMOVE;
#endif

    return GDK_FILTER_CONTINUE;
}

void init_atom_property () {
    hime_atom = get_hime_atom (dpy);
    XSetSelectionOwner (dpy, hime_atom, xim_xwin, CurrentTime);
}

void free_pho_mem (), free_tsin (), free_all_IC (), free_gtab (), free_phrase ();
#if TRAY_ENABLED
void destroy_tray ();
#endif

void do_exit () {
    dbg ("----------------- do_ exit ----------------\n");

    free_pho_mem ();
    free_tsin ();
#if USE_XIM
    free_all_IC ();
#endif
    free_gtab ();
    free_phrase ();

#if 1
    destroy_win0 ();
    destroy_win1 ();
    destroy_win_pho ();
    destroy_win_gtab ();
#endif

#if TRAY_ENABLED
    destroy_tray ();
#endif

    free_omni_config ();
    gtk_main_quit ();
}

void sig_do_exit (int sig) {
    do_exit ();
}

void load_phrase ();
void init_tray (), exec_setup_scripts ();
void init_tray_double ();

#if TRAY_UNITY
void init_tray_appindicator ();
#endif

gboolean delayed_start_cb (gpointer data) {
#if TRAY_ENABLED
    if (hime_status_tray) {
        if (hime_tray_display == HIME_TRAY_DISPLAY_SINGLE)
            init_tray ();
        else if (hime_tray_display == HIME_TRAY_DISPLAY_DOUBLE)
            init_tray_double ();
#if TRAY_UNITY
        else if (hime_tray_display == HIME_TRAY_DISPLAY_APPINDICATOR)
            init_tray_appindicator ();
#endif
    }
#endif

    dbg ("after init_tray\n");

    return FALSE;
}

static void get_display_size () {
#if !GTK_CHECK_VERSION(3, 0, 0)
    display_width = gdk_screen_width ();
    display_height = gdk_screen_height ();
#else
    GdkMonitor *monitor = get_primary_monitor ();
    if (!monitor) {
        // no output (yet): keep windows within a common screen size
        display_width = 1920;
        display_height = 1080;
        return;
    }

    // Layer surfaces are placed on that one output (hime_window_init),
    // with margins from its edges.
    if (hime_use_layer_shell ()) {
        GdkRectangle geometry;
        gdk_monitor_get_geometry (monitor, &geometry);
        display_width = geometry.width;
        display_height = geometry.height;
        return;
    }

    GdkRectangle work_area;
    gdk_monitor_get_workarea (monitor, &work_area);

    // TODO:
    // The workaround only fixes the wrong input window position bug when using multiple monitors with the same resolution.
    // The different resolution scenario should be handled well in the future.
    display_width = work_area.width * gdk_display_get_n_monitors (get_default_display ());
    display_height = work_area.height;
#endif
}

static void screen_size_changed (GdkScreen *screen, gpointer user_data) {
    get_display_size ();
}

#include "lang.h"

#if HIME_LAYER_SHELL
#include <poll.h>

#include <wayland-client.h>

static void registry_global (void *data, struct wl_registry *registry,
                             uint32_t name, const char *interface, uint32_t version) {
    if (!strcmp (interface, "zwlr_layer_shell_v1")) {
        *(gboolean *) data = TRUE;
    }
}

static void registry_global_remove (void *data, struct wl_registry *registry, uint32_t name) {
}

static void sync_done (void *data, struct wl_callback *callback, uint32_t serial) {
    *(gboolean *) data = TRUE;
}

// Like wl_display_roundtrip (), but gives up after timeout_ms: a wedged
// compositor must not hang the daemon forever.
static gboolean roundtrip_with_timeout (struct wl_display *display, int timeout_ms) {
    static const struct wl_callback_listener listener = {sync_done};
    gboolean done = FALSE;
    struct wl_callback *callback = wl_display_sync (display);
    wl_callback_add_listener (callback, &listener, &done);

    const gint64 deadline = g_get_monotonic_time () + timeout_ms * 1000;
    while (!done) {
        while (wl_display_prepare_read (display) != 0) {
            wl_display_dispatch_pending (display);
        }
        if (done) {
            wl_display_cancel_read (display);
            break;
        }
        wl_display_flush (display);

        const gint64 left = deadline - g_get_monotonic_time ();
        struct pollfd pfd = {wl_display_get_fd (display), POLLIN, 0};
        if (left <= 0 || poll (&pfd, 1, left / 1000) <= 0) {
            wl_display_cancel_read (display);
            break;
        }
        if (wl_display_read_events (display) < 0) {
            break;
        }
        wl_display_dispatch_pending (display);
    }

    wl_callback_destroy (callback);
    return done;
}

// Does the Wayland compositor we run under support wlr-layer-shell?
static gboolean wayland_has_layer_shell (void) {
    // Without these, libwayland would try $XDG_RUNTIME_DIR/wayland-0, which
    // may belong to another session of the same user (e.g. an X session
    // started next to a Wayland one).
    if (!getenv ("WAYLAND_DISPLAY") && !getenv ("WAYLAND_SOCKET")) {
        return FALSE;
    }

    struct wl_display *display = wl_display_connect (NULL);
    if (!display) {
        return FALSE;
    }

    static const struct wl_registry_listener listener = {
        registry_global,
        registry_global_remove,
    };
    gboolean found = FALSE;
    struct wl_registry *registry = wl_display_get_registry (display);
    wl_registry_add_listener (registry, &listener, &found);
    // the globals are all announced before the reply to this sync
    if (!roundtrip_with_timeout (display, 1000)) {
        fprintf (stderr, "hime: the Wayland compositor did not answer\n");
        found = FALSE;
    }
    wl_registry_destroy (registry);
    wl_display_disconnect (display);
    return found;
}
#endif

// The GDK backend: X11, whose windows can be placed anywhere and which
// XIM needs, unless the Wayland compositor supports layer-shell, which lets
// the daemon's windows keep their place and stay out of the keyboard focus
// without Xwayland (niri, sway, KDE, ...).  HIME_BACKEND=x11|wayland
// overrides this.
#if GTK_CHECK_VERSION(3, 10, 0)
static const char *choose_backend (void) {
    const char *backend = getenv ("HIME_BACKEND");
    if (backend && (!strcmp (backend, "x11") || !strcmp (backend, "wayland"))) {
        return backend;
    }
    if (backend && backend[0]) {
        fprintf (stderr, "hime: ignoring HIME_BACKEND=%s (use x11 or wayland)\n", backend);
    }

#if HIME_LAYER_SHELL
    if (wayland_has_layer_shell ()) {
        return "wayland";
    }
#endif
    return "x11";
}
#endif

int main (int argc, char **argv) {
    // Daemonize before gtk_init: GTK starts GLib worker threads (GDBus), and
    // a child forked after that only has the main thread, so it hangs on the
    // first D-Bus call (e.g. gvfs, while loading the theme).
    if (getenv ("HIME_DAEMON")) {
        daemon (1, 1);
#if FREEBSD
        setpgid (0, getpid ());
#else
        setpgrp ();
#endif
    }

#if GTK_CHECK_VERSION(3, 10, 0)
    // GDK_BACKEND, inherited from the application that started the daemon,
    // would override the choice
    unsetenv ("GDK_BACKEND");
    const char *backend = choose_backend ();
    fprintf (stderr, "hime: using the %s backend\n", backend);
    gdk_set_allowed_backends (backend);
#endif
    gtk_init (&argc, &argv);

    signal (SIGCHLD, SIG_IGN);
    signal (SIGPIPE, SIG_IGN);

    set_is_chs ();

    char *lc_ctype = getenv ("LC_CTYPE");
    char *lc_all = getenv ("LC_ALL");
    char *lang = getenv ("LANG");
    if (!lc_ctype && lang)
        lc_ctype = lang;

    if (lc_all)
        lc_ctype = lc_all;

    if (!lc_ctype)
        lc_ctype = "zh_TW.Big5";
    dbg ("hime get env LC_CTYPE=%s  LC_ALL=%s  LANG=%s\n", lc_ctype, lc_all, lang);

#if USE_XIM
    char *t = strchr (lc_ctype, '.');
    if (t) {
        int len = t - lc_ctype;
#if FREEBSD
        lc = strdup (lc_ctype);
        lc[len] = 0;
#else
        lc = g_strndup (lc_ctype, len);
#endif
    } else
        lc = lc_ctype;

    dbg ("hime XIM will use %s as the default encoding\n", lc_ctype);
#endif

    if (argc == 2 && (!strcmp (argv[1], "-v") || !strcmp (argv[1], "--version") || !strcmp (argv[1], "-h"))) {
#if GIT_HAVE
        p_err (" version %s (git %s)\n", HIME_VERSION, GIT_HASH);
#else
        p_err (" version %s\n", HIME_VERSION);
#endif
    }

    init_TableDir ();
    load_settings ();
    load_gtab_list (TRUE);

#if HIME_I18N_MESSAGE
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    dbg ("after gtk_init\n");

    // NULL on the Wayland backend: everything needing X is skipped then
    dpy = GDK_DISPLAY ();
    root = dpy ? DefaultRootWindow (dpy) : None;

    get_display_size ();

    // update display_width/display_height on screen size-changed events
    g_signal_connect (
        gdk_screen_get_default (), "size-changed",
        G_CALLBACK (screen_size_changed), NULL);

    dbg ("display width:%d height:%d\n", display_width, display_height);

    start_inmd_window ();

    if (dpy) {
#if USE_XIM
        open_xim ();
#endif

        gdk_window_add_filter (NULL, my_gdk_filter, NULL);

        init_atom_property ();

        // disable the io handler abort
        // void *olderr =
        XSetErrorHandler ((XErrorHandler) xerror_handler);
    }

    signal (SIGINT, sig_do_exit);
    signal (SIGHUP, sig_do_exit);

    init_hime_im_serv (xim_xwin);

    exec_setup_scripts ();

    g_timeout_add (200, delayed_start_cb, NULL);  // Old setting is 5000 here.

    dbg ("before gtk_main\n");

    disp_win_kbm_capslock_init ();

    gtk_main ();

    return 0;
}
