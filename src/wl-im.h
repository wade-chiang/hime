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

#ifndef HIME_WL_IM_H
#define HIME_WL_IM_H

// The daemon as a Wayland input method (wl-im.c)

#if HIME_WAYLAND_IM
// become the input method of the seat, on the Wayland backend
void wl_im_init (void);
// Is a text-input field focused (and can text be committed to it)?
gboolean wl_im_ready (void);
// send the committed text and the preedit to it
void wl_im_send (void);
#else
static inline void wl_im_init (void) {}
static inline gboolean wl_im_ready (void) { return FALSE; }
static inline void wl_im_send (void) {}
#endif

#endif /* HIME_WL_IM_H */
