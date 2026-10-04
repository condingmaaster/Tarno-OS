# Nachtlog — 2026-10-04 (Abend/Nacht), THOS

Alle Punkte mit Test, außer wo ausdrücklich "ungetestet" steht.

## Neu
- **tg3-Treiber** (`kernel/src/tg3.rs`) für den BCM57780 des Acer. Registerfolge aus Linux `tg3.c` (GPL), alle Offsets maschinell gegen `tg3.h` geprüft,
  zusätzlich von einem zweiten Modell gegengelesen. **Auf echter Hardware ungetestet** (QEMU kennt den Chip nicht). `pci::find_id` sucht jetzt Busse 0–7.
- **AF_UNIX `SCM_RIGHTS`**; `MSG_DONTWAIT` pro Aufruf; `memfd` mit `read/write/seek`, **sparse und bedarfsgepaged** (`MAP_SHARED` über `fault_lazy`), `fallocate`.
- **Wayland-Server** im Compositor (`xtask/testdata/thoswls.h`): Registry, `wl_shm`, `wl_seat` (Zeiger + Tastatur mit deutschem xkb-Keymap), `wl_output`,
  `xdg_toplevel`, Frame-Callbacks (~60/s), Alpha. `wayland-test`: unverändertes `wayland-info` + ein libwayland-Programm (epoll-Schleife).
  `foot-test`: das **unveränderte Debian/Devuan-`foot`** (Fontconfig, FreeType, HarfBuzz, 2 Render-Threads) zeigt BusyBox-`ash`, Eingabe funktioniert.
- **epoll, eventfd, timerfd, signalfd, inotify (Stub)**; `poll/select/epoll_wait` schlafen auf einer globalen Weckschlange statt alle 20 ms zu scannen.
- **Kick-IPI** weckt schlafende CPUs sofort; Takt 1 kHz (Zeitscheibe bleibt 10 ms). Aufwachlatenz poll/epoll: 10–20 ms → 0,5–1 ms (TCG).
- **Desktop-Look**: weiche Schatten, runde Ecken (AA), Verlauf-Titelleiste, Einblenden, Pfeil-Cursor, vorgerenderter Hintergrund, Hover-Schließknopf.
- **Root-Dateisystem 64 → 128 MiB** (war voll). Scratch-LBAs: 300000 (AHCI), 301000 (ESP), 290000 (NCQ), 291000 (Fehlerinjektion).

- **Node.js** (V8 + libuv, Debian-Paket vom Host) und **OpenJDK 21** (Lambdas, Streams, zweiter Thread; `-Xint`) laufen unverändert.
  Beide brauchen ein größeres Root-Dateisystem: `THOS_FS_MIB=384` (`node-test`) bzw. `512` (`java-test`); die Tests setzen es selbst.
  Neue Syscalls dafür: `capget/capset`, `clock_getres`, `getcpu`, `fallocate`.

## Gefunden und behoben
- AHCI: nach einem fehlgeschlagenen `READ LOG EXT` blieb das Task-File-ERR-Bit stehen und ließ den **nächsten gesunden Befehl** scheitern
  (zeigte sich erst, seit beim Beenden `sync_all()` schreibt). Fix: harmloser Befehl nach der Recovery. Relevant für echte Platten.
- Race in `fault_lazy`: zwei Threads auf derselben Seite → `PageAlreadyMapped`-Panic (foot nutzt Render-Threads). Prüfen + Mappen jetzt unter einer Sperre.
- `xkb_context_new` liefert NULL, wenn `/usr/share/X11/xkb` fehlt → foot hatte still keine Tastatur.
- Früher Timer-IRQ vor Scheduler-Start (`this_cpu()` ohne gs) → Boot hing.

## Offen
- tg3 und EHCI-Hub-Pfad auf dem Acer testen (Kabel einstecken, Boot-Log lesen: `docs/thos/acer-hardware.md`).
- Popups/Subsurfaces/Zwischenablage im Wayland-Server, `xdg-decoration`, Fenstergröße ändern.
- Signale weiter pro Prozess (kein `tgkill` pro Thread).
- (erledigt) Kernel-Zugriff (`usercopy`) auf noch nicht eingelagerte Lazy-memfd-Seiten holt sie jetzt ein.
