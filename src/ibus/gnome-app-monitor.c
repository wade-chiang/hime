/*
 * Copyright (C) 2026 The HIME team, Taiwan
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

// The application GNOME Shell has focused, so that each application has an
// input state of its own (GNOME Shell passes the keys of all text-input
// applications through one IBus input context).  GNOME tells no ordinary
// client which one it is: GNOME Shell's Introspect interface does, but only
// to xdg-desktop-portal-gnome, which asks for the running applications
// (GetRunningApplications) each time the focused one changes.  As fcitx5
// does (5.1.23), a second session bus connection becomes a monitor and
// reads those replies.  Without the portal, nothing is known and all
// applications share one state.

#include <string.h>

#include <gio/gio.h>

#include "gnome-app-monitor.h"

#define SHELL_NAME "org.gnome.Shell"
#define INTROSPECT_NAME "org.gnome.Shell.Introspect"
#define PORTAL_NAME "org.freedesktop.impl.portal.desktop.gnome"

static void (*changed_cb) (void);
static GDBusConnection *bus;

// the unique names of GNOME Shell and the portal, once both run
static char *shell_owner, *portal_owner;
static GDBusConnection *monitor;
static GCancellable *cancellable;
// counts the monitors started: replies of an earlier one are stale
static guint generation;

// what is known
static GHashTable *running;
static char *focused_app;
static gboolean overview;

// In the monitor connection's thread: the portal's last
// GetRunningApplications call, whose reply is wanted
typedef struct {
    char *shell_owner, *portal_owner;
    guint32 serial;
    guint generation;
} Filter;

// the applications GNOME Shell replied with, for the main thread
typedef struct {
    GVariant *apps;
    guint generation;
} Reply;

static void filter_free (gpointer data) {
    Filter *filter = data;
    g_free (filter->shell_owner);
    g_free (filter->portal_owner);
    g_free (filter);
}

static void notify (void) {
    if (changed_cb) {
        changed_cb ();
    }
}

// In the main thread: the applications GNOME Shell replied with
static gboolean apps_cb (gpointer data) {
    Reply *reply = data;
    GVariant *apps = reply->apps;
    const gboolean stale = reply->generation != generation;
    g_free (reply);
    if (stale) {
        g_variant_unref (apps);
        return G_SOURCE_REMOVE;
    }
    g_autoptr (GHashTable) new_running = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    char *new_focused = NULL;
    GVariantIter iter;
    const char *app;
    GVariant *props;
    g_variant_iter_init (&iter, apps);
    while (g_variant_iter_next (&iter, "{&s@a{sv}}", &app, &props)) {
        g_hash_table_add (new_running, g_strdup (app));
        GVariant *seats = g_variant_lookup_value (props, "active-on-seats", NULL);
        if (seats) {
            g_free (new_focused);
            new_focused = g_strdup (app);
            g_variant_unref (seats);
        }
        g_variant_unref (props);
    }
    g_variant_unref (apps);

    g_clear_pointer (&running, g_hash_table_unref);
    running = g_steal_pointer (&new_running);
    g_free (focused_app);
    focused_app = new_focused;
    notify ();
    return G_SOURCE_REMOVE;
}

// In the monitor connection's thread: the messages it gets are the bus's
// reply to BecomeMonitor, then the portal's calls and GNOME Shell's replies
// to it.  Those are not for us: drop them (else GDBus would answer the
// calls, and a monitor must send nothing).
static GDBusMessage *filter_cb (GDBusConnection *connection, GDBusMessage *message, gboolean incoming,
                                gpointer data) {
    Filter *filter = data;
    const char *sender = g_dbus_message_get_sender (message);
    if (!incoming || !g_strcmp0 (sender, "org.freedesktop.DBus")) {
        return message;
    }
    switch (g_dbus_message_get_message_type (message)) {
    case G_DBUS_MESSAGE_TYPE_METHOD_CALL:
        if (!g_strcmp0 (sender, filter->portal_owner) &&
            !g_strcmp0 (g_dbus_message_get_member (message), "GetRunningApplications")) {
            filter->serial = g_dbus_message_get_serial (message);
        }
        break;
    case G_DBUS_MESSAGE_TYPE_METHOD_RETURN:
        if (filter->serial && !g_strcmp0 (sender, filter->shell_owner) &&
            g_dbus_message_get_reply_serial (message) == filter->serial) {
            filter->serial = 0;
            GVariant *body = g_dbus_message_get_body (message);
            if (body && g_variant_is_of_type (body, G_VARIANT_TYPE ("(a{sa{sv}})"))) {
                Reply *reply = g_new (Reply, 1);
                reply->apps = g_variant_get_child_value (body, 0);
                reply->generation = filter->generation;
                g_idle_add (apps_cb, reply);
            }
        }
        break;
    default:
        break;
    }
    g_object_unref (message);
    return NULL;
}

static void stop_monitor (void) {
    generation++;
    if (cancellable) {
        g_cancellable_cancel (cancellable);
        g_clear_object (&cancellable);
    }
    if (monitor) {
        g_dbus_connection_close (monitor, NULL, NULL, NULL);
        g_clear_object (&monitor);
    }
    g_clear_pointer (&running, g_hash_table_unref);
    g_clear_pointer (&focused_app, g_free);
}

static void become_monitor_cb (GObject *source, GAsyncResult *result, gpointer data) {
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
    if (!reply && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_message ("hime-ibus: cannot follow the focused application: %s", error->message);
    }
}

static void monitor_connected_cb (GObject *source, GAsyncResult *result, gpointer data) {
    g_autoptr (GError) error = NULL;
    GDBusConnection *connection = g_dbus_connection_new_for_address_finish (result, &error);
    if (!connection) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_message ("hime-ibus: cannot follow the focused application: %s", error->message);
        }
        return;
    }
    monitor = connection;

    Filter *filter = g_new0 (Filter, 1);
    filter->shell_owner = g_strdup (shell_owner);
    filter->portal_owner = g_strdup (portal_owner);
    filter->generation = generation;
    g_dbus_connection_add_filter (monitor, filter_cb, filter, filter_free);

    g_autofree char *call_rule = g_strdup_printf (
        "type='method_call',sender='%s',interface='%s',member='GetRunningApplications'",
        portal_owner, INTROSPECT_NAME);
    g_autofree char *reply_rule = g_strdup_printf ("type='method_return',sender='%s',destination='%s'",
                                                   shell_owner, portal_owner);
    const char *rules[] = {call_rule, reply_rule, NULL};
    g_dbus_connection_call (monitor, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus.Monitoring", "BecomeMonitor",
                            g_variant_new ("(^asu)", rules, 0), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
                            cancellable, become_monitor_cb, NULL);
}

static void read_overview (void);

static void start_monitor (void) {
    stop_monitor ();
    notify ();
    if (shell_owner) {
        // GNOME Shell may have started again
        read_overview ();
    }
    if (!shell_owner || !portal_owner) {
        return;
    }
    g_autofree char *address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    if (!address) {
        return;
    }
    cancellable = g_cancellable_new ();
    g_dbus_connection_new_for_address (
        address, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, cancellable, monitor_connected_cb, NULL);
}

static void name_appeared (GDBusConnection *connection, const char *name, const char *owner, gpointer data) {
    char **owner_of = data;
    if (!g_strcmp0 (*owner_of, owner)) {
        return;
    }
    g_free (*owner_of);
    *owner_of = g_strdup (owner);
    start_monitor ();
}

static void name_vanished (GDBusConnection *connection, const char *name, gpointer data) {
    char **owner_of = data;
    if (!*owner_of) {
        return;
    }
    g_clear_pointer (owner_of, g_free);
    start_monitor ();
}

// GNOME Shell's overview has a text field of its own

static void set_overview (GVariant *value) {
    if (value && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean (value) != overview) {
        overview = g_variant_get_boolean (value);
        notify ();
    }
}

static void overview_changed (GDBusConnection *connection, const char *sender, const char *path,
                              const char *interface, const char *signal, GVariant *parameters, gpointer data) {
    g_autoptr (GVariant) changed = NULL;
    g_variant_get (parameters, "(&s@a{sv}@as)", NULL, &changed, NULL);
    g_autoptr (GVariant) value = g_variant_lookup_value (changed, "OverviewActive", G_VARIANT_TYPE_BOOLEAN);
    set_overview (value);
}

static void overview_cb (GObject *source, GAsyncResult *result, gpointer data) {
    g_autoptr (GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, NULL);
    if (reply) {
        g_autoptr (GVariant) value = NULL;
        g_variant_get (reply, "(v)", &value);
        set_overview (value);
    }
}

static void read_overview (void) {
    g_dbus_connection_call (bus, SHELL_NAME, "/org/gnome/Shell", "org.freedesktop.DBus.Properties", "Get",
                            g_variant_new ("(ss)", SHELL_NAME, "OverviewActive"), G_VARIANT_TYPE ("(v)"),
                            G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, overview_cb, NULL);
}

void gnome_app_monitor_start (void (*changed) (void)) {
    changed_cb = changed;
    bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) {
        return;
    }
    g_bus_watch_name_on_connection (bus, INTROSPECT_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE, name_appeared,
                                    name_vanished, &shell_owner, NULL);
    g_bus_watch_name_on_connection (bus, PORTAL_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE, name_appeared,
                                    name_vanished, &portal_owner, NULL);
    g_dbus_connection_signal_subscribe (
        bus, SHELL_NAME, "org.freedesktop.DBus.Properties", "PropertiesChanged", "/org/gnome/Shell",
        SHELL_NAME, G_DBUS_SIGNAL_FLAGS_NONE, overview_changed, NULL, NULL);
}

const char *gnome_app_monitor_focused (void) {
    if (overview) {
        return GNOME_APP_OVERVIEW;
    }
    return focused_app ? focused_app : "";
}

gboolean gnome_app_monitor_running (const char *app) {
    return !running || !*app || !strcmp (app, GNOME_APP_OVERVIEW) || g_hash_table_contains (running, app);
}
