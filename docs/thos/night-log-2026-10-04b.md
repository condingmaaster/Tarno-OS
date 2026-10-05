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

- **GTK 3** (unverändertes Toolkit, Wayland-Backend, Menüs als `xdg_popup`) läuft; der Compositor erkennt Client-seitige Dekoration (GTK) und zeichnet dann keinen zweiten Rahmen.
  Fenster lassen sich an Ecke/Kante ziehen (xdg `configure`) und mit Alt+Up maximieren.
- **Selbst-Hosting**: `gcc` (cc1, as, ld, glibc-Header) übersetzt und linkt ein C-Programm *in* THOS, das danach läuft (`gcc-test`, 448 MiB Dateisystem).

- Wayland-Server komplett für Toolkits: Popups (`xdg_popup`/Positioner), Client-Dekoration, Zwischenablage (`wl_data_device`), Größe ändern, Maximieren.
- Java läuft auch mit dem C1-JIT (`-XX:TieredStopAtLevel=1`), nicht nur interpretiert.
- Gesamtlauf `cargo xtask suite`: **51 bestanden, 1 übersprungen (net-test braucht Host-Loopback), 0 fehlgeschlagen**. Die Sonderbilder
  (`node-test`, `java-test`, `gtk-test`, `gcc-test`) laufen einzeln, weil sie ein größeres Dateisystem brauchen.

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

## Nachtrag 2026-10-05 (Nachtschicht)
- `drive_login` (xtask) tippt Benutzername/Passwort erst nach dem jeweiligen Prompt: behebt die Flake beim ersten Login (Kernel-Logzeilen kamen zwischen die Tasten).
- Desktop: Start-Menü im Panel (Programme mit T = an die Taskleiste, D = auf den Desktop anheften), Desktop-Icons (Doppelklick startet), Menü-Oberflächen schließen bei Klick daneben (CREATE-Flag `d & 2`), `desk-menu-test`.
  Pins liegen in `/tmp/panel.conf` und `/tmp/desktop.conf`.
- Offen: Fensterliste in der Taskleiste, Icons verschieben, Hover im Menü.
