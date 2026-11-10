// SPDX-License-Identifier: GPL-2.0-or-later
// thosnotify [-k kind] "Title|Body": show a notification on the desktop.
#include <stdio.h>
#include <stdlib.h>
#include "thosnotify.h"
int main(int argc, char **argv) {
    int kind = 0, i = 1;
    if (argc > 2 && !strcmp(argv[1], "-k")) { kind = atoi(argv[2]); i = 3; }
    if (i >= argc) { fputs("usage: thosnotify [-k 0..3] \"Title|Body\"\n", stderr); return 2; }
    thos_notify(kind, argv[i]);
    return 0;
}
