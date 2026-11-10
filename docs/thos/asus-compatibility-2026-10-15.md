# ASUS ROG / Aura Creator: Kompatibilitätsprüfung

Die echte Tastatur bleibt unverändert. Der Nutzer untersagt jede Änderung an
Tastatur, Beleuchtung, Belegung, Profilen und Firmware. Alle Softwareproben
laufen in QEMU ohne physisches USB-Passthrough und ohne Netzwerkadapter.
Auf dem Host wird kein ASUS-Programm ausgeführt oder installiert.

## Tatsächliches Ergebnis

Der offizielle Armoury-Crate-/Aura-Creator-Installer 3.3.6.0 ist PE32+ für x64.
Sein ZIP umfasst 2.304.321 Bytes. SHA-256 stimmt mit dem von ASUS veröffentlichten
Wert überein; Herkunft und Hash des entpackten Programms stehen in
[der Provenienz](logs/2026-10-15/evening-asus-provenance.json).

Der erste echte THOS-Start scheitert vor dem Programmeinstieg an
`KERNEL32.dll!FindFirstFileExA`, Rückgabestatus 126.

Diese Dateisuche wurde daraufhin ergänzt. Die vorhandene Unicode-Version
ignorierte bisher Suchoptionen; ANSI und Unicode berücksichtigen jetzt
Groß-/Kleinschreibung und den Verzeichnisfilter auch beim nächsten Treffer.
Ungültige Optionen und nicht unterstützte Gerätesuche liefern Fehler.
Der echte Win32-Gasttest prüft Treffer, Dateigrößen, Basismetadaten,
Fortsetzung des Filters, Fehlercodes und bestehende Programme/Threads:
[QEMU-Test bestanden](logs/2026-10-15/evening-findex-winc-pass-serial.log).
Microsoft-Referenz: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexa

Die erneute ASUS-Probe kommt weiter, scheitert nun an
`KERNEL32.dll!SetDefaultDllDirectories`, erneut Status 126. Das ist weiterhin
**keine erfolgreiche Installation und kein gestarteter Aura Creator**.
[Erster Blocker](logs/2026-10-15/evening-asus-installer-first-blocker-serial.log),
[nächster Blocker](logs/2026-10-15/evening-asus-installer-next-blocker-serial.log).

Die statische Prüfung findet zusätzlich fehlende gewöhnliche Imports in
KERNEL32 und fehlende Module ADVAPI32, SHELL32, OLEAUT32, SHLWAPI.
[Importliste und Abgleich](logs/2026-10-15/evening-asus-import-coverage.json).
Dieser Abgleich bewertet nur Exportnamen; er beweist keine Funktionssemantik,
keine verzögerten Imports und keine vollständigen rekursiven Abhängigkeiten.
Einzelne Erfolgs-Stubs würden diesen Installer nicht funktionsfähig machen.

## Herstellerangaben und Modellabgrenzung

ASUS nennt die ROG Strix Scope II in der
[Aura-Creator-Kompatibilitätsliste](https://www.asus.com/global/support/faq/1042484/).
Der [offizielle Installer](https://www.asus.com/supportonly/armoury%20crate/helpdesk_download/)
setzt Windows 10 ab 1903 oder Windows 11 voraus. Das Paket umfasst UWP-Anwendungen
und mehrere Dienste, darunter Armoury Crate, Aura Lighting und ASUS Framework.
Diese Laufzeitumgebung besitzt THOS noch nicht vollständig.

Die reine USB-Inventur zeigt einen ASUS ROG OMNI RECEIVER (0b05:1ace).
Das beweist die genaue Tastaturvariante nicht. Scope II, II RX und II 96 Wireless
werden deshalb nicht gleichgesetzt. ASUS bietet für die
[II 96 Wireless](https://www.asus.com/uk/supportonly/rog%20strix%20scope%20ii%2096%20wireless/helpdesk_download/)
auch Armoury Crate Gear an; das Paket ist modellspezifisch und bietet laut ASUS
kein Aura Sync. Es wurde wegen seiner Größe von rund 403 MB und ungeklärter
Modellvariante nicht heruntergeladen.

## Nächste Schritte

Zuerst den Windows-Loader und benötigte APIs mit kleinen, gezielten Programmen
weiterentwickeln. Danach den unveränderten ASUS-Installer erneut offline prüfen.
Windows-Dienste, Registry, COM/UWP und Geräteschnittstellen sind eigene größere
Kompatibilitätsarbeiten. Tatsächliche Tastatursteuerung ist derzeit ausdrücklich
ausgeschlossen. Kein Firmware-Update und kein Durchreichen des Receivers.

Leichte Windows-Spiele sind nachrangig. Als winziger echter Windows-Kandidat
liegt SkiFree 1.04 vom [Autor](https://ski.ihoc.net/) lokal vor: 39.409-Byte-ZIP,
118.784-Byte-EXE, PE32/i386. THOS unterstützt aktuell PE32+ x64; die Architektur
ist bereits ein Blocker. Der saubere Gasttest mit benutzereigener Download-Kopie belegt die ausdrückliche
Ablehnung vor dem Start, ohne Änderung des Ausführungsrechts.
[Protokoll](logs/2026-10-15/evening-skifree-final-probe-serial.log).
Es wurde keine Spielbarkeit nachgewiesen.

## Weiterer reproduzierter Windows-Blocker

Ein echtes x64-Programm findet die ergänzte Dateisuche als statischen Import.
`GetProcAddress(kernel32, "FindFirstFileExA")` und die Unicode-Variante liefern
jedoch NULL/ERROR_PROC_NOT_FOUND(127), während die direkte Erweiterungs-DLL den
Export findet. Der dynamische Resolver verwendet bislang nur die synthetische
Exporttabelle und nicht den Erweiterungs-Fallback des PE-Importladers.
[Reproduktionslog](logs/2026-10-15/evening-winlookup-probe-serial.log).
Dieser Fehler wurde am 16. Oktober behoben und mit 19 echten PE-Prüfungen validiert; die ursprüngliche Reproduktion oben dokumentiert den vorherigen Stand.

Aktualisierung16.Oktober: Der beschriebene dynamische Exportfehler ist mit
19echtenPE-Prüfungen behoben und im lokalen Live übernommen. Siehe
[neuesten Bericht](worklog-2026-10-16.md). Der ASUS-Installer selbst bleibt
wegen weiterer fehlender Windows-APIs/DLLs weiterhin blockiert.
