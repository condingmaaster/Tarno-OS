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

## Nachtschicht 2026-10-05 (zweite Hälfte)
- `thos-pkg`: `.deb/.rpm/.zip/.tar.*/.exe` aus Datei oder URL; **Debian-Archiv mit Abhängigkeiten** (`update`, `search`, `install <name>`, Prüfsumme gegen den Index, `/opt/root`).
  Getestet mit dem echten Archiv in QEMU: Index laden (68825 Pakete), `tree` installieren und ausführen (`pkg-repo-test`, braucht Internet, nicht in der Suite).
- Kernel-Fehler, die dabei auffielen und behoben wurden: `O_APPEND` und `O_TRUNC` wurden beim `open` ignoriert (`>>` überschrieb, `>` kürzte nicht), `TCGETS` & Co. gelang auf jeder Datei (gunzip dachte, die Eingabe sei ein Terminal),
  `vfork` fehlte, `execve("/proc/self/exe")` fehlte, `#!`-Skripte ließen sich nicht direkt starten (jetzt: Interpreter-Zeile, bis 4 Ebenen), `syslog(2)` für `dmesg`.
- Image: `/bin` bekam beim Verlinken der BusyBox-Applets keinen Platz mehr (debugfs `ln` vergrößert das Verzeichnis nicht) — die letzten Applets (`top` u. a.) fehlten still; jetzt `expand_dir`. Neue Applets: `ssl_client`, `dmesg`, `dpkg-deb`, `ar`, `cpio`, `xzcat`, `sha256sum` …
  `openssl`-Shim (C): BusyBox `wget` ruft `openssl s_client` auf, der Shim verbindet und übergibt an `ssl_client` — https funktioniert (`https-test`).
- `/proc/cpuinfo` mit echten CPUID-Daten, `/proc/pci`, `/proc/dmesg` (32-KiB-Ringpuffer), `hwreport`-Skript: für den ersten Acer-Start.
- Windows-Programme: `msvcrtx.dll`/`kernel32x.dll` stark erweitert — libm (x87), richtiges `%e/%g/%f`, Wide-Char-CRT inkl. `wprintf`, Zeit/`strftime`, Deskriptor-I/O und `_stat64`, Umgebung, Unicode-Datei-/Pfad-APIs (`CreateFileW`, `FindFirstFileW` …), TLS, Interlocked-Funktionen, Temp-Pfade, Locale-/String-Typ-Stubs.
  `wbig-test` (44 Prüfungen mit einem breiten mingw-Programm).
- `THOS_RES=WxH` für xtask-Tests bei anderer Auflösung.
- Noch offen: 32-Bit-Windows (WOW64), `.msi`, GUI-Teile von user32/gdi32 (nur 14+6 Aufrufe), Fensterliste in der Taskleiste, Paket-Signaturen.

## Nachtschicht 2026-10-06 (kurz nach Mitternacht bis ~02:00)
- **Desktop:** Aufgabenleiste mit allen offenen Fenstern (`WINBEGIN/WIN/RAISE`), Dateimanager `thosfiles`, Software-Zentrale `thossoft` (durchsucht den Debian-Index, installiert/entfernt per Terminalfenster),
  Neustart/Herunterfahren im Startmenü, Druck-Taste = PNG-Bildschirmfoto nach `/tmp/shot-N.png`, `thosterm -c <Befehl>`.
- **Pakete:** `thos-pkg` kann Archivpakete entfernen; echte Programme aus dem Debian-Archiv laufen: tree, htop, jq, ripgrep, nano, w3m (`pkg-apps-test`, `pkg-repo-test`, Netz nötig, nicht in der Suite).
  `/proc/<pid>/maps` (glibc `pthread_getattr_np`, libgc), `#!`-Skripte, `/proc/self/exe`, `vfork`, `sched_yield`.
- **Windows:** ein echtes Rust-Programm für `x86_64-pc-windows-gnu` läuft (std: Heap, Dateien, Umgebung, Threads, Mutex, Kanäle, `catch_unwind`, `Command::status()`/`output()` mit Pipes),
  Winsock über Loopback, `CreateProcess` (PE- und ELF-Kinder), x64-SEH-Unwinder (`unwind.h`), Heap mit Blockkopf, anonyme Pipes (`NtCreateNamedPipeFile`), Dateiabbildungen, Init-Once, Umgebungsblock.
  Neue DLLs: `ntdllx`, `ws2_32`, `api-ms-win-core-synch-l1-2-0`, `bcryptprimitives`, `userenv` (alle freistehendes C, `xtask/msvcrtx/`). Test: `winrt-test`.
- **Kernel-Fehler gefunden/behoben:** `NtWriteFile/NtReadFile` lasen die Länge als 64 Bit (ist `ULONG`), NT-Handles konnten Slot 0 (= NULL) bekommen, `HeapAlloc` ohne Kopf (Rust ruft `HeapSize`/`HeapReAlloc`).
- Haiku-Fehlersuche in tg3/ehci/hda/ps2: 8 Meldungen, 7 davon falsch (Speicherbarrieren/DMA-Größen waren in Ordnung); eine echte Kleinigkeit (HDA wartete ohne Zeitgrenze) behoben.
- Offen: 32-Bit-PE (WOW64), `.msi`, GUI-Teile von user32/gdi32, Paketsignaturen, Hover im Startmenü, Icons verschieben.
- `thos-pkg update` verifiziert jetzt die Signatur von `InRelease` (gpgv + Debian-Archivschlüssel im Image) und die Prüfsummenkette — im Test mit dem echten Archiv bestätigt.
