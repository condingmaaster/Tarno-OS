# Schichtbericht — Nacht auf 2026-10-04 (ab 21:45, Stand ca. 05:00)

## Kurzfassung

Heute stand "viel Fortschritt mit kurzen, effizienten Tests" im Vordergrund. Ergebnis: Aus THOS ist ein **bedienbares System mit Fenstern** geworden — ein
Userspace-Compositor, Terminal-Fenster mit echter BusyBox-Shell auf einem Pseudo-Terminal, Panel, deutsche Tastatur (ä ö ü ß €), echte Win32-Programme als Fenster
neben Linux-Clients — und es läuft **ein Dutzend unveränderter Debian-/Rust-/mingw-Programme** (Python 3.13, Perl, git, gawk, make, tar, jq, bc, vim, Rust-`std`,
Windows-Konsolenprogramme). Zwei alte, hartnäckige Fehler sind **gefunden und behoben** (B17 = Absturz beim `exit`, B15 = AHCI-Hänger). Die komplette Test-Suite
(44 Tests, parallel in ca. 18 min) ist grün.

Zum Ausprobieren: `docs/thos/desktop-howto.md` (`cargo xtask bios-run`, dann `desktop`).

## Gebaut (nach Themen)

**Desktop (Stufe 3 der Desktop-Planung, komplett in Userspace)**
- `thosdesk` (Compositor): Z-Reihenfolge, Schadensrechtecke, Titelleisten mit Text, Anheben/Ziehen/Schließen per Maus, Software-Cursor, Panel-Fenster ohne Rahmen,
  Tastenkürzel Alt+Enter / Alt+Tab / Alt+F4; Protokoll `thoswl.h` (Wayland-förmig, AF_UNIX + SysV-Shm).
- Clients: `thoswin` (Farbfenster), `thostext` (zeigt Getipptes), `thosterm` (VT100-Teilmenge, BusyBox-`ash` auf PTY, UTF-8/Latin-15), `thospanel` (Startleiste).
  `desktop` startet Compositor + Panel + Terminal. Interaktives `vim.tiny` läuft im Terminalfenster.
- Win32-Fenster im Compositor: `/dev/winsys` (Fensterereignisse, abgebildete Puffer, Eingaben zurück als WM_LBUTTONDOWN/WM_CHAR/WM_CLOSE); `winhello.exe` (mingw) wird ein Fenster.
- Kernel dafür: AF_UNIX (`socketpair`, benannte/abstrakte Sockets), SysV-Shm + `memfd_create`/`MAP_SHARED`, Pseudo-Terminals (`/dev/ptmx`, `/dev/pts/N`, Zeilendisziplin,
  `TIOCSCTTY`, `/dev/tty` = Steuerterminal), `/dev/input/kbd` (Roh-Tastenereignisse), die Textkonsole gibt den Bildschirm frei, solange `/dev/fb0` offen ist.

**Echte Software (alles unmodifizierte Binärdateien)**
- Linux: Python 3.13 (`-S -E`, Mini-Stdlib), Perl (inkl. XS-Module per `dlopen`), git (init/add/commit/log), gawk, bc, jq, tar, make, vim.tiny, 22 Coreutils/Findutils-Prüfungen,
  ein Rust-`std`-Programm (Threads, Kanäle, HashMap, Dateien, `rename`, TCP über Loopback, `Command`).
- Windows: mingw-Konsolenprogramme starten aus der Shell (echte `argv`, Exit-Status, Pipes/Umleitung). Dafür zwei neue **DLLs in C**, die der PE-Lader vor den eingebauten
  Modulen befragt: `msvcrtx.dll` (Allocator, Strings, `printf`-Kern, `qsort`, stdio ...) und `kernel32x.dll` (Dateien aller Dispositionen, Verzeichnisse, `FindFirstFile`, Zeit, Heap ...).
- Kernel-Features dafür: `clone(CLONE_VM|CLONE_VFORK)` (`posix_spawn`), `rename`/`link`/Symlinks (ext2, ELOOP-Schutz, `lstat`), verzeichnisrelative `*at`-Aufrufe + `fchdir`,
  `truncate`/`fchmod`/`fchown`, `stat` mit Eigentümer und echten Modus-Bits, `uname`, `creat`/`umask`, 8-MiB-Stack bei Bedarf, `brk`-Verkleinern, Datei-`mmap` ohne Gesamtpuffer.

**Netz**: Loopback (127.0.0.0/8 + eigene Adresse), **e1000-Treiber** (82540EM und 82574L/e1000e, getestet in QEMU), DNS/Ping/wget wie zuvor.

**Härtung / Zuverlässigkeit**: ASLR (PIE, Interpreter, mmap, Heap, Stack), Speichererschöpfung endet in `ENOMEM` statt Kernel-Panic (und der Adressraum wird beim Exit sofort frei),
Dateideskriptoren schließen sofort beim Prozessende, Register werden vor dem Start eines Programms genullt (kein Kernel-Leck), Kernel-Heap 64 MiB.

## Gefundene und behobene Fehler

| Fehler | Ursache | Fix |
|---|---|---|
| **B17** BusyBox-Applets stürzen beim `exit` ab (`rip=0x1`), "nur unter Last" | Der iretq-Pfad neuer Threads ließ `rdx` mit Kernelresten; statisches glibc reicht `rdx` als `rtld_fini` weiter und ruft es beim Exit auf | alle Register nullen (`sched.rs`) |
| **B15** AHCI-Fehlerbehandlung hängt in ~50 % der Läufe | Wartezeit nach Iterationen (`yield`) statt Zeit, `PxIS` schon vom IRQ gelöscht, `READ LOG` überschrieb den Bounce-Puffer von Tag 0 | zeitbegrenzt, IRQ-Fehler-Latch, Puffer sichern |
| `wait4` ignorierte `WNOHANG` | ash blockierte nach `cmd &` bis das Hintergrundkind endete (`sleep 1` dauerte 100 s) | Option ausgewertet |
| `brk` verkleinern + vergrößern = Kernel-Panic | Seiten blieben gemappt | Seiten freigeben / vorhandene überspringen |
| Hauptstack wuchs nie (64 KiB fest) | — | 8 MiB per Seitenfehler |
| `read_file` verdoppelte den Puffer bei 7-MiB-Datei | Kapazität = Dateigröße, letzter Block darüber hinaus | volle Blöcke reservieren |
| `git`: "dubious ownership" | `stat` meldete uid 0 | Eigentümer/Modus aus dem Inode |
| `shortcuts-test`/`desk-test` rannten gegen Laufzeit-Races | Test-Annahmen (Login-Prompt sofort, Fensterreihenfolge) | Tests gewartet / sequenziert |
| glibc-`ioctl()` schneidet Adressen auf 32 Bit | `int`-Rückgabe | `syscall()` (Compositor) |

## Offen / Risiken

- Kein `lspci -nn` der echten Acer-Hardware → kein passender NIC-Treiber (Verdacht: Atheros AR8152/AR8151), keine echte Messung der Geschwindigkeit auf dem i3-370M.
- Windows: Unicode-APIs (`W`), 32-Bit-Programme, GUI-Steuerelemente fehlen (Threads: `CreateThread` + Events/Mutex/Semaphoren laufen jetzt, max. 15 Arbeitsthreads); `msvcrtx.dll` kennt noch kein `scanf`, `%e`-Format ist vereinfacht.
- Signale sind weiter pro Prozess (nicht pro Thread); kein SIGSTOP/Job-Control (Ctrl+Z); Hardlink-/Symlink-Sonderfälle (relative Link-Ziele über `..` bei gelöschtem Zwischenverzeichnis) ungetestet.
- Exec liest die ganze ELF-Datei in den Kernel-Heap (BusyBox 2 MiB, Python 7 MiB) — bei sehr großen Programmen (Node/Java > 50 MiB Bibliotheken) sprengt das den 64-MiB-Heap und das 64-MiB-Dateisystem.
- ext2: Verzeichnisse > 12 Blöcke und Dateien > 64 MiB (Triple-Indirect) fehlen.
- Der `dns-test` braucht Internet; `net-test` Loopback am Host (`sudo ip link set lo up`) — beides lief heute.

## Was du tun musst

1. Pushen (ich schiebe nicht auf fremde Konten): `git push` für `main`; die PR #1 (main → devel) ist obsolet, wenn `main` Default ist — ggf. schließen.
2. Auf dem Acer: `lspci -nn` und `lsusb` ausgeben (für Netzwerk-/USB-Treiber) und `cargo xtask bios-image --interactive` auf einen Stick schreiben, falls du die Hardware testen willst.
3. Desktop ausprobieren: `cargo xtask bios-run` → Konto anlegen → `desktop`.

## Vorschläge für als Nächstes

1. Echte Hardware-Treiber nach `lspci` (AR8152, EHCI/USB-Tastatur, Touchpad-Gesten).
2. Thread-gerichtete Signale + SIGSTOP/SIGCONT (Job-Control), dann Go/Java/Chromium-Klasse.
3. Windows: `CreateThread`, `W`-APIs, Dialoge/Steuerelemente über den Compositor; WOW64.
4. Streaming-`exec` und ein größeres/lazy Dateisystem-Layout (Node, Java, Compiler im System: Selbst-Hosting als Ziel).
5. Fenstermanager-Feinschliff (Größe ändern, Minimieren, Hintergrund, Uhr im Panel, Zwischenablage).
