// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop protocol v0 (Wayland-shaped, own wire format): fixed 20-byte messages over an
// AF_UNIX stream socket; window pixels live in SysV shared memory the client creates.
#pragma once
#include <stdint.h>
#define WL_SOCK_NAME "\0thos-wl"          /* abstract address */
#define WL_SOCK_LEN  8
enum { WL_CREATE = 1, WL_CREATED = 2, WL_COMMIT = 3, WL_CLOSE = 4, WL_POINTER = 5, WL_KEY = 6, WL_TITLE = 7, WL_POS = 8, WL_HELLO = 9, WL_WINBEGIN = 10, WL_WIN = 11, WL_RAISE = 12, WL_TOAST = 13, WL_CURSOR = 14, WL_THEME = 15, WL_QUIT = 99 };
struct wl_msg { uint32_t op, a, b, c, d; };
/* HELLO   c->s (optional, before CREATE): the server answers WL_CREATED with a=0, b=screen width, c=screen height
   (CREATE d bit 1 = start menu surface, closed on an outside click; bit 2 = task list: the server then sends
    WINBEGIN a=count and WIN a=id|focused<<31 b,c,d=12 title chars for every open window; RAISE c->s a=id focuses one)
   CREATE  c->s: a=width b=height c=shm key (client did shmget(key, w*h*4)), d=reserved
   CREATED s->c: a=surface id, b=screen width, c=screen height
   (CREATE d bit 0 = undecorated: no titlebar/border, e.g. a panel; WL_POS c->s a,b = x,y moves it)
   COMMIT  c->s: a,b,c,d = damaged x,y,w,h in surface coordinates
   CLOSE   s->c: the user closed the window
   TITLE   c->s: a..d = up to 16 title characters
   THEME   c->s then s->c: the theme file (/etc/thos-theme) changed; the server redraws the wallpaper and tells every client to reload it
   CURSOR  c->s: a = pointer shape while over this window's content: 0 arrow, 1 text, 2 hand, 3 busy
   KEY     s->c: a = HID usage, b = 1 press / 0 release, c = ASCII (0 = none), d = modifier byte
   POINTER s->c: a,b = surface-local x,y  c = 1 on a press, 0 = moved over the surface, 2 = left it, 3 = wheel (d = signed clicks, positive = up)  */
