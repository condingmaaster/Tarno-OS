# THOS desktop — how to try it (state 2026-10-04, evening)

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
**Alt+F4** close the top window, **Ctrl+Alt+Q** leave the desktop (back to the text console); click a titlebar to raise/drag, red disc to close. German layout incl. ä ö ü ß € in terminals.
The panel's "Terminal" button starts another terminal. The text console comes back when the compositor exits (`thoswin quit` from a
terminal, or when it crashes — the framebuffer is released when its last file descriptor closes).

## Programs that run (all unmodified, from the host distribution)

| program | test | notes |
|---|---|---|
| `foot` terminal | `foot-test` | needs fontconfig + a TTF; `LC_ALL=C.UTF-8`; `xkb` directory must exist |
| GTK 3 apps | `gtk-test`, `soak-test` | menus (popups), client-side decoration, clipboard |
| NetSurf browser | `netsurf-test` | local pages and `https://` (CA bundle in `/etc/ssl/certs`) |
| Node.js, OpenJDK 21 | `node-test`, `java-test` | need a 384 / 512 MiB root filesystem (`THOS_FS_MIB`) |
| `gcc` + binutils | `gcc-test` | compiles C inside THOS, including the compositor itself |
| `curl` | `curl-test` | HTTPS through the real internet (QEMU user networking) |
| audio | `audio-test` | `/dev/dsp`, 48 kHz S16 stereo through Intel HD Audio |

The panel (`thospanel`) has a green **Start** button: it opens a menu of the installed programs (Terminal, text editor, foot, browser, GTK demo, Win32 demo, plus entries from `/apps.conf`, one `Label|command` per line). Each row has two toggles: **T** pins the program to the taskbar, **D** puts an icon on the desktop (double-click it to start). A click outside the menu closes it; "Quit desktop" ends the session.
Pins are stored in `/tmp/panel.conf` and `/tmp/desktop.conf` (same `Label|command` format; `/panel.conf` and `/desktop.conf` are the system defaults). The panel also shows the local time (CET/CEST).
A scroll wheel works when the PS/2 mouse speaks the IntelliMouse protocol (`/dev/input/wheel`).

## Wayland

The compositor also speaks the **real Wayland protocol** on `/run/wayland-0` (set `XDG_RUNTIME_DIR=/run` for a client). Implemented:
`wl_compositor`, `wl_shm` (ARGB8888/XRGB8888 through a memfd passed with `SCM_RIGHTS`), `wl_seat` (pointer + keyboard, with a German xkb keymap),
`wl_output`, `xdg_wm_base`/`xdg_surface`/`xdg_toplevel`, frame callbacks (about 60 per second); stubs: `wl_subcompositor`, `wl_data_device_manager`,
popups. Run `/usr/bin/wayland-info` to see what a client sees. `wayland-test` runs the stock `wayland-info` and a plain libwayland-client program
against it; `foot-test` runs the distribution's `foot` terminal (downloaded once with `apt-get download`, no root needed).

Windows get soft shadows, rounded corners, a gradient titlebar, a fade-in and an arrow cursor; ARGB surfaces are blended. Everything is integer
software rendering (tables for corners and shadows, the wallpaper rendered once), sized for the 2010 Acer.

Architecture in one paragraph: the compositor (`xtask/testdata/thosdesk.c`) is an ordinary userspace program. Clients speak a small
Wayland-shaped protocol (`thoswl.h`) over an AF_UNIX socket and share window pixels through SysV shared memory. Win32 windows from the
kernel's `window.rs`/`gdi.rs` reach the compositor through `/dev/winsys` (window events, mapped backing stores, input records back).
Keyboard events come from `/dev/input/kbd`, the mouse from `/dev/input/mice`, the screen is `/dev/fb0` (mmap). `thosterm` runs BusyBox
`ash` on a pseudo-terminal (`/dev/ptmx`, `/dev/pts/N`).

Tests: `cargo xtask desk-test | desk-kbd-test | desk-term-test | desk-panel-test | desk-win32-test | desk-keys-test | desktop-test`
`wayland-test | foot-test` (Wayland clients), `event-test` (epoll/eventfd/timerfd/signalfd), each checks QEMU screendumps or serial output;
`cargo xtask suite` runs everything (~18 min; add `--jobs 1` and `taskset -c 2,3` to keep other CPUs free).

## Dateimanager, Aufgabenleiste, Programme installieren (2026-10-05)
- **Dateimanager** (`thosfiles`, Startmenü „Files“): Ordner durchsuchen (Klick, Pfeiltasten, Bild auf/ab, Pos1/Ende), Doppelklick oder Enter öffnet, Rücktaste/„Up“ geht nach oben, Entf (zweimal) löscht, R lädt neu.
  Doppelklick auf `.deb`, `.rpm`, `.zip`, `.tar.*` startet `thos-pkg install` in einem Terminalfenster, `.exe`/ausführbare Dateien laufen im Terminal, Textdateien öffnen sich in `less`.
- **Aufgabenleiste:** die Leiste unten zeigt neben den angehefteten Programmen jedes offene Fenster als Knopf (in der Reihenfolge des Öffnens, das aktive hell); ein Klick holt das Fenster nach vorn.
- Neue Programme: `thos-pkg update` und `thos-pkg install <Paket>` (Debian-Archiv, Abhängigkeiten werden mitinstalliert); danach erscheinen sie im Startmenü, wenn sie einen Desktop-Eintrag mitbringen.
- Tests: `desk-tasklist-test`, `desk-files-test`, `desk-menu-test`, `pkg-test` (alle in der Suite), `pkg-repo-test` (braucht Internet).
