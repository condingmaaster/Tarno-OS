# THOS-Nachtarbeit, 14. Oktober 2026

Wiederaufnahme 21:37 Europe/Berlin nach expliziter Nutzerfreigabe.
OpenAI Docs für Automationsfunktion gelesen; direkte Kontoabfrage bleibt
Quelle für reale Kontingente. Vor Beginn 4 % Verbrauch im 300-Minuten-
Fenster, letzter Zwischenstand 11 %. Stoppschwelle >=95 % Verbrauch.
Einmalige Reset-Fortsetzung in vorhandener Automation für 15.10.01:15:10.
Design/Controlbar/Effekte erhalten; Gesamtprüfung nur vorgemerkt;
keine Subagenten, keine Datenträgeraktualisierung, lokale Änderungen behalten.

## Firefox und Linux-ABI

Der geprüfte morgendliche Memfd-Fix normalisiert unsigned-int-Flags auf
32 Bit und lässt ungültige niedrige Flags weiterhin scheitern. Firefox
passiert nun SharedMap, danach echter IPC-Abbruch wegen SO_SNDBUF=0.
Unix-Queues sind tatsächlich auf 128 KiB begrenzt; Getter zeigt jetzt
diesen Wert und beachtet kurze optlen. Keine vorgetäuschte Resize-Wirkung:
Unix-Send/Recv-Pufferänderung liefert EOPNOTSUPP. seqpacket-test in QEMU
besteht einschließlich gemeldeter Streamkapazität, vollständiger Füllung,
EAGAIN an Grenze, Byteprüfung nach Drain und erneuter Schreibbarkeit.

Firefox danach erreicht WABT/wasm2c und meldet fehlendes ARCH_SET_GS.
ARCH_SET_GS/GET_GS mit echter User-GS-Basis, Boundsprüfung, threadlokalem
Schedulerzustand, Fork/Clone-Vererbung und Exec-Reset implementiert.
Kernel-PerCPU-GS wird beim Syscall/IRQ-Swap getrennt gehalten. Auch ungültige
ARCH_SET_FS-Adressen werden vor VirtAddr/WRMSR abgelehnt. Release-Diagnose-
build erfolgreich. Neuer Gasttest prüft wirkliche GS-Memory-Reads über
Yield/Sleep, acht isolierte Threads, Fork/Exec und ungültige Adressen.
Gastprüfung dieses Fixes noch ausstehend.

Primärquellen:
https://raw.githubusercontent.com/mozilla-firefox/firefox/main/ipc/chromium/src/chrome/common/ipc_channel_posix.cc
https://raw.githubusercontent.com/WebAssembly/wabt/main/wasm2c/wasm-rt-impl.c

## Startmenü bei zusätzlichen Installationen

Bestehender Code schloss Menü sobald Höhe >700; mit 15+ Einträgen wurde
Start unbrauchbar. Begrenztes Viewport innerhalb Bildschirm/700 px,
Scrollrad und PgUp/PgDn/Home/End, korrekt offsetierte Pins/Launcher und
sichtbarer Bereichszähler ergänzen bestehendes Design. Kataloggrenze
128 statt 24; normale neun Einträge bleiben visuell unverändert.
SHM-Segmente werden auch auf abgebrochenem Create aufgeräumt.

Neue 29-Programme-QEMU-Fixture ausschließlich im privaten Abbild.
Erster Harnessversuch scheiterte an /apps.conf-Dateirechten und langer
Tastatureingabe; korrigiert auf Host-Injektion und kurze Script-Ausführung.
Aktueller korrigierter Test läuft. Keine bestandene Prüfung vorwegnehmen.

GS-Gastprüfung BESTANDEN: acht Threads lesen tatsächlich per mov gs:0
jeweils ihre eigene Speicherstelle nach Yield und Sleep. Fork erbt Base,
Exec startet Base0. Ungültige GS/FS-Kerneladressen EPERM, GET_GS NULL EFAULT.
Kein Kernelpanic, reale Registerwirkung. Durable gsbase-night.log.

Zweiter Startmenü-Lauf zeigte Produktfehler bei Keyboardrouting:
Compositor übersprang jedes undekorierte Fenster, auch offene Startmenüs.
End-Taste erreichte deshalb Menü nicht; sichtbares Program05 wurde
statt Program20 gepinnt. Das Routing akzeptiert jetzt Menüflächen,
bleibt für Dockflächen ausgeschlossen. Dritter gezielter Lauf läuft.

Startmenü-QEMU nun BESTANDEN: 29 Einträge, Ende-Taste zeigt 16–29/29,
Program20 wird korrekt im Dock angeheftet und öffnet tatsächlich Files.
Erster/letzter Ausschnitt und Files-Fenster als PNG gespeichert. Ein
Zwischenlauf wurde mit SIGTERM ohne Endnachweis beendet; daher erst der
vollständig abgeschlossene folgende Lauf als PASS gewertet. Gastlog unter
logs/2026-10-14/start-menu-scroll-night.log. Weiterhin normales Basismenü
und Win32 nach GS-Änderung gezielt regressionsprüfen.

Firefox: echtes xdg/Wayland-Mapping 1256x824 erstmals erreicht, aber keine
fertig gerenderte Testseite. Screenshot firefox-first-window-night.png
zeigt lediglich transparenten/ungefüllten Fensterbereich. Tab-Forkserver
pid72 #PF und Child107 IPC EMSGSIZE bleiben offen. Der frühere Fixture-
Shutdown erfolgte kurz nach erstem Mapping; Aufnahmen warten nun auf
'desk: wayland window', sechs Aufnahmen à10s, Gastwait180/Hosttimeout420.
Kein Firefox-PASS behauptet.

IPC-Ursache EMSGSIZE konkret: gather_iov begrenzte jede sendmsg-Nachricht
auf64KiB und verwarf größere Streamnachrichten statt teilweise zu senden.
Stream/TCP sendmsg sammelt nun begrenztes64KiB-Präfix; Seqpacket bleibt
atomisch bis zur echten128KiB-Queuegrenze. Seqpacket recvmsg erlaubt diese
128KiB ebenfalls; Summenüberlauf der recv-iov-Längen wird abgelehnt.
C-Prüfung erweitert auf atomische128KiB sendmsg/recvmsg und große Stream-
iovecs mit Bytevergleich über Teilübertragungen. Diagnosebuild läuft,
Gastprüfung steht aus.

USB/Acer: konkrete vertauschte TT-Felder in Control UND Bulk entdeckt:
Hubadresse war fälschlich<<23, Port<<16. Linux ehci.h legt HUBADDR
0x007f0000 und HUBPORT0x3f800000 fest. Zentraler queue_caps-Encoder
korrigiert dies; unabhängige Hostlayouttests für Root, unterschiedliche
Hub/Port-Werte und Grenzwerte 2/2 bestehen. Build erfolgreich. Reale
Acer-Hubwirkung bleibt ohne Hardwaretest unbestätigt, USB-Maus/Hotplug
noch nicht implementiert. Primärquelle:
https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/host/ehci.h

Pakete/UI: Repo-Installationen in gemeinsamem /opt/root wurden bislang
nicht in /opt/apps.conf eingetragen. Register berücksichtigt nun die
Dateiliste des angeforderten Pakets, ohne beliebige Programme seiner
Abhängigkeiten mitzuregistrieren. Identische Launcher werden nicht
doppelt eingetragen. Offline-Fixture mit einem bereits entpackten Paket,
einer fremden Desktopdatei und zweitem Install-Aufruf vorbereitet;
Gasttest läuft. Dies prüft UI-Registrierung, NICHT Signatur/Netzwerkdownload.

Paketregistrierungs-Gasttest BESTANDEN: derselbe Repo-Launcher bleibt bei
zweitem Install-Aufruf einmal vorhanden, fremde Desktopdatei ausgeschlossen,
Launcher führt das echte statische pkgdemo aus. Durable pkg-repo-ui-night.log.

Große IPC-Gastprüfung BESTANDEN: 128KiB-SEQPACKET sendmsg über zwei iovecs,
kleiner Empfänger mit MSG_TRUNC, danach vollständiger128KiB recvmsg ohne
Truncation; großer Stream über mehrere sendmsg-Teilübertragungen vollständig
bytegleich. Dazu alte Credentials/SCM_RIGHTS/Backpressure-Fälle bestehen.
Kernel in beiden SATA-Testquellen enthält nun auch TT-Feldkorrektur.
Firefox mit diesem Kernel und LD_DEBUG-Basen wird gerade erneut geprüft.

EHCI-Gastprüfung mit5120MiB RAM (physische Frames über4GiB) und
PS/2 ausgeschaltet BESTANDEN: erste Einrichtung, Login und Tastatureingabe
über tatsächliche USB-Tastatur. DMA-Arena wird jetzt aus einem hinreichend
großen nutzbaren Bereich unter4GiB gewählt; neue alloc_below entfernt eine
passende EHCI-QH-Seite aus der Freiliste ohne hohe Frames zu konsumieren.
Host-DMA-Auswahltests3/3 bestehen. Reale Acer-Hubtests weiterhin offen.

Firefox nach Streamfix weiterhin keine fertig gerenderte Testseite.
LD_DEBUG des exakten pid73: libxul base0x700596e20000, RIP0x70059ad43afa
=> Offset0x3f23afa; Disassembly verweist auf String 'MOZ_CRASH(failed to
fork)', Zeile270. Zusätzlich echte Meldung fork() failed: Cannot allocate
memory im Parent. Stream-EMSGSIZE nicht mehr beobachtet. Private
Anonymous-mmap-Reservierungen wurden vorher vollständig physisch belegt
und beim Fork vollständig kopiert; Bedarfspaging wird gezielt ergänzt.

Neue private anonyme Reservierungen: kompakte Schutzintervalle und erst
bei Zugriff physische Nullseiten. PROT_NONE behält vorhandene Inhalte;
mincore zeigt echte Resident-Seiten, MADV_DONTNEED gibt berührte Seiten
zurück. Fork kopiert nur vorhandene Backings, weiterhin private Kopien,
kein behauptetes allgemeines Copy-on-write. Anon-Zustand wird durch
IoMutex gegenüber Fault/Unmap/Fork geschützt. MAP_FIXED entfernt die alte
Reservierung einschließlich Backings. mprotect/mincore/munmap überspringen
unberührte virtuelle Bereiche statt millionenfach Seiten zu allocieren.
Virtueller Bump verhindert Überschreiten USER_TOP. Host-Intervalltests3/3
bestehen. Neue Gastprüfung reserviert1GiB in512MiB, berührt wenige Seiten,
prüft Schutz/Inhalt, Forkisolation, Kernelcopy, JIT RX, acht gleichzeitige
Faults und virtuelle Erschöpfung. Build/Gastprüfung noch nicht abgeschlossen;
kein neuer Release ohne erfolgreiche Prüfungen.

Erster Anonymous-Gastlauf: 1GiB Reservierung benötigt286720Byte reale
Frames einschließlich Residency-Ausgabebuffer statt1GiB. Danach Panic
PageAlreadyMapped beim Schutzwechsel: mprotect setzte vorhandenes PTE
nochmals per map statt Schutzflags zu ändern. Explizit auf protect bei
vorhandenem PTE korrigiert, map nur bei erneut sichtbar gemachtem Backing.
Fehlerlog und PNG gespeichert, QEMU beendet. Kein PASS vorwegnehmen.
Harness boot_and_run sichert künftig bei erreichtem Endmarker automatisch
<tag>-finish.ppm vor dem Beenden; kompiliert geprüft.

Anonymous-Gastprüfung nun BESTANDEN (512MiB Gast): 1GiB Reservierung
286720Byte commitment einschließlich256KiB Messvektor; unberührte Seiten
resident0, berührte1. PROT_NONE hält Inhalt und Residency; Fork kopiert
verborgene/berührte Daten privat; schreibgeschützter Kindprozess endet
korrektSIGSEGV. MADV_DONTNEED gibtBacking frei, nächster Zugriff null.
Teil-Unmap+MAP_FIXED, Kernel-read in unberührten Buffer, JIT RX=42 und
acht gleichzeitige Faults bestehen. Virtueller Adressraum erschöpft sich
mit ENOMEM statt nichtkanonischen Adressen. PNG und Gastlog gespeichert.
Firefox wird mit demselben Kernel in2GiB erneut geprüft.

Nach Einführung blockierbarer Anon-Faults meldete Firefox Kernel-#PF mit
User-RIP/Adresse0x7005145d8cf0. Architekturfehler: #PF lag auf gemeinsamer
per-CPU-IST, obwohl IoMutex-Contention reschedulen kann. Anderer Task auf
selber CPU überschreibt dann die schlafende Faultframe. #PF verwendet nun
regulären Thread-TSS-RSP0; #DF und NMI behalten ihre Notfall-ISTs. Fault-
Callstub richtet RSP dynamisch nach SysV aus und hält ExcFrame in savedRBX.
Primärquelle Linux arch/x86/kernel/idt.c INTG(X86_TRAP_PF).
Vier-CPU-Gasttest mit acht konkurrierenden Threads und128 frischen Seiten
BESTANDEN, ebenso sämtlicher Anon-Daten-/Schutz-/Fork-/Adressgrenzen-Test.
Firefox-Retest mit diesem Kernel läuft. Noch keine fertige Renderseite.

Sudo/doas THOS-Frontends implementiert, an reale Kernel-Authentifizierung
gebunden: -n verweigert ohne Passwort, -u root/0, --, -s, sudo-S;
kein Credentialcache/Daemon, keine vollständige sudoers/doas.conf-Policy
behauptet. Root-Zielprogramm läuft wirklichUID/GID0, Parent bleibtUID1000.
spawn_elevated übernimmt nur Standard-FDs0–2 sowie cwd/ctty/jobgroup, nicht
beliebige zusätzliche Handles; Umgebung weiterhin sauber. Dadurch landet
Ausgabe im aufrufenden GUI-Terminal und Pipes funktionieren. Passwort
byteweise gelesen (keine Nutzdaten nach der Passwortzeile vorablesen),
Wipe per volatile, Echo bei Abbruchsignal wiederhergestellt.
CLI-Gastprüfung BESTANDEN: falschesPasswort, -n, unzulässiger Zielaccount,
RootUID0, Exit7, Literalargument, Root-doas-n und stdin-Payload erhalten.
Settings/Development bietet 'Admin terminal' im vorhandenen zweiten
Buttonslot. UI-Gastprüfung BESTANDEN: echter Mausklick, maskiertes Passwort,
Rootshell zeigt0 fürid-u im GUI-Terminal, Passwort erscheint nicht imLog,
ShellExit funktioniert. Screenshots/Gastlogs dauerhaft gespeichert.

Firefox nach Faultstackfix kein Kernel-#PF mehr beobachtet, weiterhin
echte ENOMEM-Fork-Fehler bei2GiB und später User-#PF. Nur gemapptes,
ungefülltes Fenster, keine fertig gerenderte Seite. Neues Bedarfspaging
ist dennoch durch isolierte Speicher-/Konkurrenztests belegt. Allgemeines
Copy-on-write ist weiter offen; nächste Diagnose mit größerem Gast-RAM
auf realistischem Acer-Niveau, keine2GiB-Tauglichkeit behaupten.

Firefox4GiB: im beobachteten Verlauf keineForkENOMEM-Meldung, dennoch
wiederholte User-PF mitRIP0x70014600a960/cr2=0, keineTestseitenfarben.
GerendertesBrowserbild weiterNICHTbestätigt. Dauerhafter4GiB-Log/PNG.
Nächster Schritt exakteASLR-/Assertdiagnose dieser neuenKindprozessstelle;
kein pauschales Abschalten weiterer Schutzfunktionen. Für die anstehenden
nativen Regressionen wird ein normaler Releasekernel (interactive, ohne
ipc-diagnostics) gebaut. Die pauschalen Diagnoselogs gehören nicht insLive.

Normaler Releasebuild anfangs gescheitert: spawn_elevated-Exe-Pfad hatte
noch einenalten SAK-Testcaller inconsole.rs; korrigiert. Der vorher
laufendeMemfd-Test war deshalb nochDiagnosekernel, wurde nichtals neuer
Release-PASS etikettiert; mitneuenMM/Faultpfaden bestehtMemfd trotzdem.
Normalerinteractive-Build danach erfolgreich. BeideSATA-Testquellen haben
jetzt diesenReleasekernel, ohneIPC-Diagnosefeature. Win32-Konsolenprüfung
besteht: echteMinGW-Programme, argv, DateiIO, printf inkl.Float, Threads,
Events/Semaphore/Mutex undPipes. NormalerStartmenü-Retest läuft.

Release-Normalstartmenü mit9Einträgen BESTANDEN (Windows-Taste, Pins,
Shutdown-Cancel undTermlaunch). Release-Java21 samtCollections/Streams/
Thread undfrischemJavac BESTANDEN. Ein separatesLive-Kandidatenabbild
wurde ausdemnochunverändertenLive abgeleitet. EingebetteterRoot aktualisiert
mit /thosdesk UND /desktop, Panel, Settings, Pakethelper und sudo/doas.
Hardlink /bin/thos-pkg ersetzt, sodassSoftwareUI denselben neuenHelper
nutzt (nurRootpfad austauschen wäre beiHardlink veraltet geblieben).
Kandidaten-ext2 e2fsck-fn fünfPässeExit0. Nochkein Kandidatenboot-PASS,
kein physischerDatenträger beschrieben. GroßerDownload mitRelease läuft.

GroßerDownload mitnormalemRelease BESTANDEN:128MiB HTTP, Fenster während
Transfer real+96/+72 verschoben, vollständigerBytevergleich undFiles-Button
im verschobenenFenster. Logs/PNGs gespeichert. Live-Kandidatenboot mit
Westmere,-pdpe1gb und2048MiB läuft. OriginalLive nochunverändert.

## Abschließender Admin-UI-Nachweis

Der finale normale Release besteht `privilege-ui-test`: Klick auf Admin terminal in Settings, verdeckte Passwortzeichen, echte UID 0 im GUI-Terminal und rot gekennzeichneter Root-Prompt. Screenshot visuell geprüft und als `screenshots/2026-10-14/admin-terminal-root-night.png` gespeichert; Testlog `logs/2026-10-14/release-privilege-ui-night.log`. QEMU beendet, keine parallelen oder verbleibenden Gäste. Letzte Kontingentabfrage 94 % Verbrauch; nächste Reset-Fortsetzung bleibt einmalig 15.10.01:15:10 Berlin.

### Präzisierung des Firefox-4-GiB-Logs

Die vier Benutzer-Seitenfehler bei RIP `0x70014600a960` folgen im gespeicherten Log sämtlich auf `desk: quit requested` und `desk: loop end`. Sie können mit dem Testabbau zusammenhängen und sind kein gesicherter Grund für die zuvor fehlende HTML-Darstellung. Nach dem Reset zuerst den lebenden Browser vor Compositor-Abbau mit exakten Prozess-/ELF-Basen beobachten; Abbruch beim Abbau getrennt untersuchen. `target/thos-verification-2026-10-14.json` aktualisiert, ältere Prüfliste als Historie erhalten.

## Kontingent-Stopp

Die abschließende direkte Kontingentabfrage meldet 95 % Verbrauch / 5 % Rest im 300-Minuten-Fenster. Arbeit gestoppt, keine weiteren Builds oder Tests. Alle Gäste beendet und Fortsetzungsstand gesichert. Gemeldeter Reset bleibt 15. Oktober 01:14:48 Berlin; vorhandene einmalige Fortsetzung für 01:15:10 bleibt aktiviert und muss vor weiterer Arbeit erneut das Kontingent prüfen.
