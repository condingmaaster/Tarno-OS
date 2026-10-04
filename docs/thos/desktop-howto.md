# THOS desktop — how to try it (state 2026-10-04)

```
export PATH=$PATH:/sbin:/usr/sbin        # debugfs, mkfs.fat, sfdisk
cargo xtask bios-run                     # builds the interactive image and boots it in a QEMU window (BIOS/MBR/AHCI, like the Acer)
```

First boot asks for an administrator name and password, later boots for the login. At the `thos$` prompt:

| command | what it starts |
|---|---|
| `desktop` | compositor + panel + terminal (`desktop -v` also logs terminal lines to the serial console) |
| `thosdesk &` then `thoswin R G B W H &` | the bare compositor and a coloured test window |
| `thostext` | a window that shows what you type |
| `winhello.exe &` | a real Win32 program (built with mingw) as a window next to the Linux clients |
| `whello.exe a b`, `wtest.exe` | unmodified mingw console programs (msvcrt + `msvcrtx.dll`) |
| `python3`, `perl`, `git`, `gawk`, `make`, `rstest` ... | unmodified Debian / Rust binaries (see the `real*-test` and `rust-test` commands in `xtask`) |

Keys in the desktop (the compositor owns the keyboard while it runs): **Alt+Enter** new terminal, **Alt+Tab** cycle windows,
**Alt+F4** close the top window; click a titlebar to raise/drag, red square to close. German layout incl. ä ö ü ß € in terminals.
The panel's "Terminal" button starts another terminal. The text console comes back when the compositor exits (`thoswin quit` from a
terminal, or when it crashes — the framebuffer is released when its last file descriptor closes).

Architecture in one paragraph: the compositor (`xtask/testdata/thosdesk.c`) is an ordinary userspace program. Clients speak a small
Wayland-shaped protocol (`thoswl.h`) over an AF_UNIX socket and share window pixels through SysV shared memory. Win32 windows from the
kernel's `window.rs`/`gdi.rs` reach the compositor through `/dev/winsys` (window events, mapped backing stores, input records back).
Keyboard events come from `/dev/input/kbd`, the mouse from `/dev/input/mice`, the screen is `/dev/fb0` (mmap). `thosterm` runs BusyBox
`ash` on a pseudo-terminal (`/dev/ptmx`, `/dev/pts/N`).

Tests: `cargo xtask desk-test | desk-kbd-test | desk-term-test | desk-panel-test | desk-win32-test | desk-keys-test | desktop-test`
(each checks QEMU screendumps), `cargo xtask suite` runs everything (~18 min).
