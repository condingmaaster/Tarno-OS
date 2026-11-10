# THOS – Fortschritt am 16. Oktober 2026

Nach der Spielpause wurden gezielte Eingabe-, Datei- und Kompatibilitätsfehler weiterbearbeitet. Layout, Controlbar, Animationen und Effekte bleiben erhalten.

- Mehrere Wayland-Maus- und Tastaturressourcen verdrängen sich nicht mehr. Fokus, Freigabe, Neuanlage und versionsgerechte Ereignisse sind geprüft. Shift, Ctrl und AltGr werden an Wayland korrekt weitergereicht; bestehende Alt-/Windows-Kürzel bleiben erhalten.
- Schreibgeschützt geöffnete reguläre Dateien verweigern nun Schreiben, positioniertes Schreiben und Kürzen. Zuvor waren echte Schreibzugriffe möglich.
- BSD-Dateisperren und POSIX-Bereichssperren funktionieren mit unterschiedlichen Besitz- und Schließregeln. Prozesskonflikte, Bereichsteilung, Zusammenführung, Fork/Exec/Close, Signale und Deadlocks sind geprüft.
- Wartende Dateisperren verwenden jetzt die vorhandene Ereigniswarteschlange anstelle eines festen Zehn-Millisekunden-Polltimers. Aufwecken beim letzten Schließen ist für schreibbare und kleine/große schreibgeschützte Dateien geprüft.

Die Prüfungen liefen ausschließlich nacheinander in QEMU. Dazu gehören ältere Westmere-CPU-Emulation, echte Wayland-/GTK-Eingaben, Fenstergrößen und Vollbild, 19 Windows-PE-Prüfungen, 1.000 Bereichsoperationen gegen ein unabhängiges Modell sowie echte SQLite-Prozesskonflikte mit Rollback/Commit und erneutem Öffnen.

Firefox lädt in der isolierten Diagnose echte Windows-EXE- und Linux-ELF-Dateien; vollständige Bytes stimmen, kein automatisches Ausführen, ausdrückliches Öffnen über Files/Terminal/Controlbar bestanden. Das ist weiterhin eine sandbox-disabled lokale HTTP-Testumgebung und kein Nachweis eines vollständig produktionsreifen Browsers.

Die zusätzliche JDK-Probe besteht: javac kompiliert im Gast, getrennte JVMs beachten FileChannel-Sperren und lesen nach Freigabe die korrekt gespeicherten Daten. JDK-Testoptionen entsprechen der vorhandenen kleinen Diagnosekonfiguration. clone3(435) bleibt nicht implementiert; der vorhandene Lauf fällt erfolgreich zurück. SQLite ist mit DELETE-Journal geprüft; WAL und vollständige geteilte Dateimappings bleiben offen.

Das exakt gepackte lokale Live startet mit Desktop, Controlbar und Terminal; Kernel und komplette RAM-root wurden rückgelesen und verglichen. Screenshots liegen unter [screenshots/2026-10-16](screenshots/2026-10-16), Protokolle unter [logs/2026-10-16](logs/2026-10-16). [Aktueller Desktop](screenshots/2026-10-16/lock-wait-live-desktop.png).

Aktuelles Abbild: target/thos-live.img, SHA256 **0c42ce24e9444e49710e3c1a2813e7d60ec9700a81f54330fa86ce40b71356b8**. Manifest: target/thos-live-2026-10-16-lock-wait-build-manifest.json. Normaler Kernel: target/thos-kernel-lock-wait-2026-10-16.elf. Root: target/live-root-keyboard-2026-10-16.ext2. Vorversionen und lokale Änderungen bleiben erhalten.

Der physische USB-Stick und die echte Tastatur wurden nicht verändert. ASUS/Aura bleibt wegen weiterer Windows-Laufzeitlücken blockiert; keine Installation oder Hardwaresteuerung behauptet. Echte WLAN-/GPU-Firmwareuploads, Hardwarebeschleunigung und allgemeine Windows-Spielkompatibilität bleiben offen. Die allgemeine Gesamtprüfung bleibt vorgemerkt.

Bei der tatsächlich gemeldeten Nutzungsschwelle gestoppt: 95% verbraucht/5% Rest. Stand gesichert, keine Test-VM mehr aktiv. Die zuvor pausierte automatische Fortsetzung wurde nicht reaktiviert.
