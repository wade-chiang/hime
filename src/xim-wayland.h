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

#ifndef HIME_XIM_WAYLAND_H
#define HIME_XIM_WAYLAND_H

// XIM while the daemon runs on GDK's Wayland backend (xim-wayland.c)

#if USE_XIM && HIME_XIM_WAYLAND
// serve XIM on DISPLAY (Xwayland), if set
void xim_wayland_init (void);
// Has an XIM client the focus?  Text is then committed to it directly
// (on the X11 backend, a fake Shift makes it ask for the text instead).
gboolean xim_wayland_ready (void);
// commit the output buffer to it
void xim_wayland_send (void);
#else
static inline void xim_wayland_init (void) {}
static inline gboolean xim_wayland_ready (void) { return FALSE; }
static inline void xim_wayland_send (void) {}
#endif

#endif /* HIME_XIM_WAYLAND_H */
