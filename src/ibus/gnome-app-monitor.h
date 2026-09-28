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

#ifndef HIME_GNOME_APP_MONITOR_H
#define HIME_GNOME_APP_MONITOR_H

#include <glib.h>

// Which application GNOME Shell has focused (see gnome-app-monitor.c).
// CHANGED is called when that or the running applications change.
void gnome_app_monitor_start (void (*changed) (void));
// the focused application's id, GNOME_APP_OVERVIEW, or "" if unknown
const char *gnome_app_monitor_focused (void);
// APP still runs (TRUE also while nothing is known, and for "")
gboolean gnome_app_monitor_running (const char *app);

// GNOME Shell's overview (its search field)
#define GNOME_APP_OVERVIEW "gnome-shell-overview"

#endif /* HIME_GNOME_APP_MONITOR_H */
