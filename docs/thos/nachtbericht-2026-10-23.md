# THOS – Nachtschicht ab 23. Oktober 2026

Zwischenbericht: Die Nachtschicht ist weiterhin vom Nutzer autorisiert. Ein Kontingentstopp beendet sie nicht dauerhaft; dieselbe Automation setzt sie nach dem tatsächlichen Reset fort. Eine manuelle Pause hat Vorrang.

## Umgesetzt und geprüft

- Temporäre QEMU-Images, private Testroots und Seedkopien liegen jetzt in einem eigenen Laufverzeichnis. Erfolgreiche Tests räumen ihre Kopien auf. Vor großen Schreibvorgängen bleiben mindestens 10 GiB Reserve. Fehlgeschlagene Läufe werden nach gesicherter Diagnose gezielt bereinigt.
- `GetCommandLineW` liefert die echte UTF16-Kommandozeile aus dem PEB. Der alte ANSI-Zwischenspeicher und seine Kürzung entfallen. 25 PE-Prüfungen auf KVM und emulierter Westmere-CPU bestehen.
- Windows-Außen- und Clientmaße stimmen nun mit dem bestehenden THOS-Fensterrahmen überein. `AdjustWindowRect/Ex` und `CreateWindowExA/W` ergänzen sich. Die THOS-Dekoration bleibt erhalten.
- Unicode-Fensterklassen erhalten echte prozess- und modulgebundene Atome. Unicode-Titel und Nachrichten durchlaufen echte Fensterprozeduren. 36 PE-Prüfungen sowie bestehende ANSI-, Schrift- und Fensterbedienungstests bestehen.
- Synthetische DLLs überschreiben ihre Exporttabellen nicht mehr mit Funktionscode. Die alte NTDLL-Überlappung wurde mit einer unveränderten Baseline reproduziert. Namenstabellen sind sortiert; Laufzeit-Lookups prüfen Speichergrenzen und suchen binär. 12 native Exportprüfungen, 25 Grenzprüfungen und 19 bestehende Lookup-Prüfungen bestehen.
- Die Windows-Runtime verwaltet Umgebungswerte in UTF16. Lange Werte werden vollständig gespeichert; Snapshots besitzen eigene Lebensdauer und werden freigegeben. Ersetzte Werte geben ihren Speicher wieder frei. 10.000 Ersetzungen und parallele Hosttests mit UBSan zeigen keine Zunahme belegter Blöcke. Der reale PE-Paralleltest deckte zusätzlich die folgende Kernel-Race auf.
- Futex-Warteschlangen bleiben registriert, solange ein wartender Thread bereits eine Referenz hält. Die bisher mögliche Entfernung zwischen Lookup und Einreihung konnte Threads auf unerreichbaren Warteschlangen schlafen lassen. Deterministische Hosttests sowie echte Acht-Thread-PE- und Linux-pthread-Prüfungen bestehen.
- Der PE-Startwrapper wartet auf das Prozessende-Ereignis statt alle zehn Millisekunden zu prüfen. Ein bereits schnell beendetes Kind bleibt über seinen Start-Handle erreichbar und wird eingesammelt. 72 schnelle Starts mit sechs Exitcodes, verzögerte Beendigung und 64 parallele Starts bestehen.
- Linux-Prozess-IDs werden bei `kill` und `wait4` als vorzeichenbehaftete 32-Bit-Werte ausgewertet. Der Fehler bei `kill(-pgid, SIGTERM)` wurde mit libc und einem ausdrücklich vorzeichenerweiterten Vergleichsaufruf reproduziert. Prozessgruppen-Abbruch und rohe ABI-Grenzfälle bestehen auf beiden CPU-Konfigurationen.

ANSI-Umgebungs-Snapshots verwenden laut Microsoft die OEM-Codepage. Die endgültige Variante verwendet hierfür OEM437; Get/SetEnvironmentVariableA bleiben ACP1252. Alle 29 PE-Prüfungen bestehen auf KVM und Westmere; die abschließenden Fenster-, Lookup-, CRT- und Bedienungsregressionen ebenfalls.

Zusätzlich besteht der echte javac/JVM-Dateisperrentest auf dem finalen Kernel. Firefox 153.4.0esr rendert die Offline-Testseite im separaten Diagnose-System mit sichtbaren Referenzflächen; Screenshot `screenshots/2026-10-23/night-firefox-offline.png`. Ein zusätzlicher Tab-Unterprozess stürzt bei einem Nullzugriff ab. Der direkte Vergleich mit demselben Userspace und dem vorherigen geprüften Kernel reproduziert ebenfalls einen solchen Unterprozessabsturz. Dieser Fehler bleibt offen. Firefox lief dabei ausschließlich im bekannten Testmodus mit deaktivierter Sandbox; Browser/JDK wurden dadurch nicht in das Live-Abbild aufgenommen.

## Grenzen und ehrlicher Stand

ASUS Armoury Crate/Aura Creator startet weiterhin nicht: `SetDefaultDllDirectories` fehlt. Snake erreicht weiterhin den fehlenden Import `BeginPaint`. Kein Installer-, Gameplay- oder Hardwaresteuerungserfolg wird behauptet.

Die neue Runtimeumgebung ist noch nicht mit Linux-Env-Vererbung, dem anfänglichen PEB-Environmentblock und der separaten CRT-Umgebung vereinheitlicht. Klassenzusatzspeicher, verschiedene Klassenressourcen, vollständige Unicode-Namensfaltung und gemischte ANSI/Unicode-Textcallbacks fehlen weiterhin. Vorhandene Schriften ersetzen nicht enthaltene Glyphen durch ihren bisherigen Fallback.

WLAN-Firmwareupload, Radeon-Beschleunigung und Verhalten auf dem Acer wurden in dieser Schicht nicht auf echter Hardware bestätigt. Kein USB-Datenträger und keine echte Tastatur wurden verändert. Layout, Controlbar, Animationen und Effekte bleiben erhalten.

## Abbild und Belege

Aktuelles geprüftes Live: `target/thos-live.img`, SHA256 `c75ee952e76c7d44a4b9464a67f3fb052503ba54dd9e749e6e35b5e2371a3e9c`. Der kombinierte Stand wurde nach seinen Regressionen und dem Start des exakt gepackten Live-Abbilds mit Westmere übernommen. Manifest: `target/thos-live-2026-10-23-night-build-manifest.json`. Das vorherige Live `d465fc…` bleibt als gezielter Rückfallstand erhalten.

Logs: `docs/thos/logs/2026-10-23/`. Screenshots: `docs/thos/screenshots/2026-10-23/`. Der detaillierte Verlauf einschließlich fehlgeschlagener Zwischenproben steht in `worklog-2026-10-23.md`. Für die Fortsetzung gilt `resume-2026-10-23.md`.

Abschließend laufen keine VMs mehr; das Test-Scratchverzeichnis ist leer. Entbehrliche Zwischen- und Browser-Testbilder dieser Schicht wurden nach Hashvergleich entfernt. Quellen, eingefrorene Kernel, kleine Nachweise und gezielte Live-Rückfallstände bleiben erhalten. Die SSD hat rund 203 GiB frei.

Die bestehende einmalige Fortsetzung ist für den 24. Oktober um 02:47 Uhr Europe/Berlin aktiv, nach dem tatsächlich gemeldeten Reset um 02:46:09. Vor dem nächsten Arbeitsbeginn wird das Kontingent erneut geprüft. Der nächste konkrete Windows-Arbeitsbereich ist unter `windows-dynamic-loader-next.md` beschrieben; es wurde dafür noch keine Laufzeitfunktion oder ASUS-Kompatibilität behauptet.

Kontingentpause: tatsächlich 5% Rest erreicht. Entwicklung gestoppt, automatische einmalige Fortsetzung um 02:47 Uhr bleibt aktiv.
