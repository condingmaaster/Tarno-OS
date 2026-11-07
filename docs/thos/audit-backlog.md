# Vorgemerkt: vollständige THOS-Prüfung

Auftrag vom 11. Oktober 2026: THOS schrittweise vollständig prüfen.
Der Nutzer startet diese Aufgabe später selbst; kein automatischer Start.
Die aktuelle Desktop-/Performance-Arbeit wird zuerst fortgesetzt.

Aktuelle Vorgabe: bestehendes Layout, Design, Taskleiste, Animationen und Effekte
vollständig erhalten. Vorerst ausschließlich Fehler und Performance bearbeiten.
Der alternative Designentwurf wird nicht umgesetzt; Effekte werden nicht reduziert.

## Vorgehen

Je Bereich den aktuellen Code lesen, Aussagen mit vorhandenen Tests abgleichen,
konkrete Fehler reproduzieren und nach Auswirkung priorisieren. Bestätigte Befunde,
Vermutungen und noch ungeprüfte Hardwarepfade getrennt dokumentieren. Bestehende
lokale Änderungen erhalten. Prüfung in überschaubaren Etappen durchführen.

## Etappen

- [ ] Desktop, Eingaben, Ereignisschleifen, Rendering und Performance
      (gezielte Erstprüfung: `review-2026-10-11.md`)
- [ ] Boot, Firmware, ACPI, CPU-Initialisierung und SMP
- [ ] Scheduler, Synchronisation, Timer und Prozesslebenszyklus
- [ ] Speicherverwaltung, Paging, Benutzerzeiger und Fehlerisolation
- [ ] VFS, ext2, Block-I/O, Fehlerbehandlung und Shutdown-Dauerhaftigkeit
- [ ] Treiber: AHCI, USB, Eingabe, Netzwerk, Audio und Framebuffer
- [ ] POSIX-/ELF-Kompatibilität, Threads, Signale und IPC
- [ ] NT-/PE-/Win32-Kompatibilität und gemeinsame Executive-Objekte
- [ ] Rechte, Anmeldung, Elevation, Zufall und Sicherheitsgrenzen
- [ ] Installation, Paketverwaltung, Updates und Wiederherstellung
- [ ] Native Apps, Datenintegrität, Bedienung und Designsystem
- [ ] Build, CI, Testqualität, Dokumentation und Lizenzinformationen

## Konkrete Acer-Rückmeldungen und Produktvorgaben (11. Oktober)

Diese Punkte gehören zur laufenden Fehlerbehebung; die vollständige Prüfung
oben bleibt bis zum manuellen Start vorgemerkt.

- Netzwerk: keine erfundene QEMU-Adresse auf echter Hardware; Ethernetsteuerung
  und WLAN-Einrichtung in Settings/Controlbar. BCM43225 meldet derzeit
  `probe-failed / Empty Address Region`, Firmware geprüft, nicht geladen.
- Grafik: HD 5470, 512 MB erkannt, Framebuffer-WC konfiguriert; Beschleunigung
  und Firmware-Upload sind noch nicht implementiert.
- Terminal: farbige Promptzeile und Abstand zwischen Befehlen/Diagnoseausgaben.
- USB: angeschlossene USB-Maus, Hotplug, Speichergeräte und weitere Klassen
  schrittweise unterstützen. Vorhandene Boot-Tastatur-/Massenspeicherpfade sind
  keine vollständige USB-Unterstützung. Interne Acer-Webcam ebenfalls prüfen.
- Windows-Taste soll Start öffnen/schließen. Win32-Demo startet derzeit den
  falschen Testclient; echte Windows-Programme ersetzen eine Demo erst, wenn
  ihre benötigten APIs tatsächlich funktionieren.
- Herunterfahren: Bestätigung, danach geordnetes Beenden/Speichern und
  Dateisystem-Synchronisation. Eine Bestätigung allein löst noch keine
  ungespeicherten Dokumente oder laufenden Schreibvorgänge.
- Fenster: native Apps nutzen feste Größen im eigenen Desktopprotokoll;
  Größenänderung/Maximieren existieren bisher nur für Wayland-Clients.
  Protokoll und Apps für echtes Neulayout erweitern; keine bloß vergrößerten
  Pixelbilder. Maximieren mit sichtbarer Taskleiste und echtes Vollbild getrennt.
- Uhr: Acer zeigt 15:51 bei real 13:41. RTC-Konvention, Zeitkalibrierung und
  Zeitzone prüfen; Datum/Uhrzeit-Einstellung im UI ergänzen.
- Tray: Netzwerk, Audio, Bluetooth mit wirklichem Treiberstatus und zugehörigen
  Einstellungen integrieren, bestehendes Dock-Design erhalten.
- Stick: 512-MiB-RAM-Systemabbild belegt nicht den ganzen 58,6-GiB-Stick.
  Persistenten Datenspeicher für Downloads/Programme/Dateien und UI-Anzeige
  planen, vorhandene Bootpartition und Sicherung erhalten.
- Standardprogramme sinnvoll ergänzen; Treiber-/Kompatibilitätsgrundlagen
  bleiben Voraussetzung für Browser, Kamera und größere Anwendungen.
- Ressourcen: wenige notwendige Hintergrunddienste, sichtbare und einzeln
  abschaltbare Autostarts/Dienste. Installierte Programme dürfen sich nicht
  automatisch als dauerhafte Hintergrundprozesse einrichten. Leerlaufverbrauch
  und CPU-Wakeups messen; das Ziel bleibt deutlich unter 1 GB bei 8 GB RAM.

## Erwartetes Ergebnis

Ein nach Schweregrad geordnetes Befundregister mit Codeverweisen,
Reproduktionsschritten, Testabdeckung, klaren Grenzen der Prüfung und einer
umsetzbaren Reihenfolge für Korrekturen.

## Gezielter Fortschritt am 13. Oktober

- Native Größenänderung, Maximieren und F11: sechs Apps in QEMU bestanden.
  Win32-Resize sowie weitere Randrichtungen bleiben offen.
- Farbige Terminalprompt, Windows-Taste und korrigierter Demo-Start: gezielte
  QEMU-Tests bestanden (11. Oktober); keine Designumstellung.
- Downloadfenster mit Files-/Software-Einstieg: HTTP-EXE-Download bytegleich,
  heruntergeladene MSVC-ripgrep-EXE tatsächlich gestartet. Größere Windows-GUI-
  Programme und vollständige MSI-Installation noch nicht nachgewiesen.
- Dateicache-/sendfile-Korrekturen: großer Kopiervorgang und Download bestanden.
  Neue Dateisperren und positionale Reads: paralleler QEMU-Dateitest bestanden.
- OpenJDK 21: Testprogramm samt zweitem Thread läuft nach der Sperrkorrektur
  auf Westmere/1024 MiB. JDK-UI und Compilerprobe noch offen.
- Firefox: zwei Offline-Proben weiterhin Userspace-Pagefault vor dem Fenster.
- WLAN-Verbindung, GPU-Beschleunigung, USB-Maus/Hotplug, weitere USB-Klassen,
  Zeiteinstellung, Audio/Bluetooth-Tray und permanenter Stick-Datenspeicher
  bleiben erforderlich. Der physische Stick ist derzeit nicht am Buildhost.

### Gezielte Dateisystemkorrekturen am 14. Oktober

Triple-Write und Triple-Free sind ergänzt. Ein begrenzter Write-through-
Dateipfad hält große Dateien nicht mehr vollständig im Kernelheap. Im
1-KiB-Dateisystem besteht die 128-MiB-Schreib-/Leserunde einschließlich
hohem pwrite, Shrink/Grow, Sparse-Datei und exakter Blockfreigabe; externe
e2fsck-Prüfung besteht. ENOSPC-Erhaltung und Wiederaufnahme wurden vor der
jüngsten Metadatenoptimierung geprüft; deren Retest steht noch aus.
Unveränderte indirekte Metadaten werden nun wiederverwendet, mit gezielten
Hosttests für Wachstum/Löcher/Schrumpfen und Rückabwicklung. Keine vollständige
Crash-/Stromausfallprüfung oder POSIX-Unlink-Lebensdauer behauptet.

Firefox erreicht nach echten SEQPACKET-, FIONREAD- und Memfd-/Seal-Fixes
weitere Hilfsprozesse. Letzte Browserprobe scheitert beim Fontconfig-Thread,
noch keine Browser-GUI/Sandbox bestätigt. JDK-UI und frische Compilerprobe
bestanden bereits am 13. Oktober. Details und aktueller Teststand stehen
in worklog-2026-10-14.md und resume-2026-10-14.md.
