# THOS – autonome Abendfortsetzung am 16. Oktober 2026

Die automatische Fortsetzung ist wieder ausdrücklich autorisiert. Der Start um 22:03 Uhr erfolgte nach Prüfung des tatsächlichen Kontingent-Resets. Die 5%-Stoppgrenze bleibt bestehen.

Windows-Programme erhalten jetzt den tatsächlichen eigenen Dateipfad und die Pfade geladener DLLs. Zuvor wurde stets `C:\program.exe` gemeldet, und auch die Prozessdaten enthielten einen erfundenen Namen. Die Startdaten werden jetzt pro Ladevorgang übergeben; ein gemeinsamer globaler Zwischenspeicher entfällt. Kleine Puffer, Fehler und Terminierung sind korrigiert. Vier echte Windows-Starts prüfen auch ein abweichendes `argv[0]`, Leerzeichen, Unicode/Emoji und einen Pfad über 260 Zeichen.

Die Windows-APIs zur UTF-8-/UTF-16-Konvertierung behandeln jetzt echte Unicode-Zeichen einschließlich Surrogatpaaren, Größenabfragen, strikten Fehlern und Ersatzzeichen. 28 Gastprüfungen bestehen auf KVM und emulierter Westmere-CPU. Der reine Konvertierungskern besteht zusätzlich einen Rundlauf über sämtliche gültigen Unicode-Codepunkte. Andere Codepages bleiben unvollständig; THOS besitzt damit keine vollständige Windows-Sprachunterstützung.

Bestehende Tests für Windows-Argumente, 19 Exportauflösungen, CRT/Dateien/Threads und das ausdrückliche Öffnen einer heruntergeladen-artigen EXE über Files, Terminal und Controlbar bestehen. Der echte Fensterlauf prüft Mausgrößenänderung, Maximieren, F11, Wiederherstellen, tatsächliche Clientgrößen/Pixelecken und Alt-F4. Ein verschachtelter Testlog wurde durch zusammenhängende Messausgabe im Testprogramm behoben; die Größen- und Pixelprüfungen bleiben erhalten.

Das exakt gepackte lokale Live startet unter Westmere mit 2 GiB, vier virtuellen CPUs und ausgeschaltetem Netzwerk. Kernel und vollständige RAM-root wurden zurückgelesen und verglichen. Layout, Controlbar, Animationen und Effekte bleiben erhalten. Geprüfter Zwischenstand dieses ersten Abschnitts: `target/thos-live.img`, SHA256 `a2cd7b37b76ddca72118b99078bd14080a258b3cac0d3361fef01d4818180328`. Details: `target/thos-live-2026-10-16-unicode-build-manifest.json`.

Der originale ASUS-Installer ist erneut ausschließlich offline in QEMU geprüft. Er scheitert weiterhin am fehlenden `SetDefaultDllDirectories`, Status 126. Keine Installation, kein Aura-Start und keine Hardwaresteuerung sind nachgewiesen. Der physische Stick und die echte Tastatur bleiben unverändert.

Die Arbeit läuft weiter: Eine echte Wayland-Probe bestätigt noch die fehlende Liste bereits gehaltener Tasten beim Neuanlegen einer Tastaturressource. Shift+A wird fälschlich als leere Liste übermittelt. Diese folgende Korrektur ist noch nicht im genannten Live-Stand enthalten.

Screenshots: [Live-Desktop](screenshots/2026-10-16/unicode-live-desktop.png), [Windows-Konvertierung](screenshots/2026-10-16/winunicode-checked.png), [Files öffnet Windows-Programm](screenshots/2026-10-16/unicode-files-windows-opened.png), [Windows-Fenster wiederhergestellt](screenshots/2026-10-16/unicode-windows-restored.png). Vollständige Logs unter `docs/thos/logs/2026-10-16/`.


Inzwischen sind auch die gehaltenen Wayland-Tasten und die Reihenfolge von Key-/Modifier-Ereignissen korrigiert und mit echten Clients geprüft. Der vollständige Firefox-Download-/Files-/Controlbar-Lauf, echte GTK-Eingabe sowie native und Windows-Fenster bleiben funktionsfähig. Die echte Tastatur wird dabei weiterhin nicht angerührt.

Windows-Fenster erhalten nun zusätzlich wirkliche KeyDown-/KeyUp-Meldungen für unter anderem Pfeiltasten, Ctrl und Shift mit korrekten Scancodes/Übergangsflags. Die vorhandene deutsche Zuordnung bleibt erhalten. Enter liefert im Windows-Zeichenpfad CR. Ein reguläres PE prüft sechs Tastenpaare, Zeichen, Flags, tatsächliche Erfolgspixel und sauberes Schließen. Die vollständigen Windows-APIs für TranslateMessage, IME und asynchronen Tastenzustand sind damit noch nicht implementiert.

64 echte PE-Starts aus acht parallelen Arbeitern stimmen mit unabhängig über vererbte Pipes übergebenen Kennungen, CRT-Argumenten und tatsächlichen Modulpfaden überein. Der zusätzlich isoliert geprüfte kleine Windows-x64-Snake-Quellbuild scheitert noch vor dem Start an `GDI32.dll!BitBlt`, Status 126. Das ist kein spielbares Spiel; die [unveränderten MIT-Quellen des Autors](https://github.com/Zcytxcbyz/Snake/tree/b28f387a8f16442bd12078c7ce1ed8a21cc501da) und Buildherkunft sind dokumentiert.

Neuester vollständig geprüfter lokaler Stand: `target/thos-live.img`, SHA256 `12aa81c0be39aa4d2b5094e45a6357e6af6a0777801aef261dfe00f02c0b5822`, Manifest `target/thos-live-2026-10-16-winkey-build-manifest.json`. [Aktueller Desktop](screenshots/2026-10-16/winkey-live-desktop.png), [echte Windows-Tastenprobe](screenshots/2026-10-16/winkey-complete.png). Noch nicht enthalten ist die danach begonnene Korrektur fehlender Windows-Fokusmeldungen; deren Prüfung läuft separat.
