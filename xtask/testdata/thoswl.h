// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop protocol v0 (Wayland-shaped, own wire format): fixed 20-byte messages over an
// AF_UNIX stream socket; window pixels live in SysV shared memory the client creates.
#include <stdint.h>
#define WL_SOCK_NAME "\0thos-wl"          /* abstract address */
#define WL_SOCK_LEN  8
enum { WL_CREATE = 1, WL_CREATED = 2, WL_COMMIT = 3, WL_CLOSE = 4, WL_POINTER = 5, WL_KEY = 6, WL_QUIT = 99 };
struct wl_msg { uint32_t op, a, b, c, d; };
/* CREATE  c->s: a=width b=height c=shm key (client did shmget(key, w*h*4)), d=reserved
   CREATED s->c: a=surface id
   COMMIT  c->s: a,b,c,d = damaged x,y,w,h in surface coordinates
   CLOSE   s->c: the user closed the window
   POINTER s->c: a,b = surface-local x,y  c = button mask  */
