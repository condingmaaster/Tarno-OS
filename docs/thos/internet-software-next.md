# Nächster Arbeitsbereich: Internet und echte Programme

Nutzerauftrag vom 11. Oktober 2026: nach den Fenster-/Performance-Korrekturen
WLAN (Priorität vor Ethernet), Browser und Downloads, JDKs, doas/sudo,
apt/Linux-Pakete und Windows-EXE/MSI angehen. Ziel ist das Herunterladen,
Installieren und tatsächliche Ausführen von Linux- und Windows-Programmen
aus einem Browser, insbesondere Firefox.

Die allgemeine vollständige THOS-Prüfung bleibt separat vorgemerkt und startet
weiterhin nur auf Wunsch. Dieser konkret benannte Arbeitsbereich ist die nächste
Fortsetzung der aktuellen Arbeit.

Ergänzung des Nutzerauftrags: Die Radeon HD 5470 gehört direkt zu diesem
Arbeitsbereich. Alle neuen Funktionen müssen in die bestehende THOS-Oberfläche
und Controlbar integriert werden. Layout, Animationen und Effekte bleiben
erhalten. Statusanzeigen dürfen nur tatsächlich erreichte Backend-Zustände
anzeigen; Geräteerkennung bedeutet weder WLAN-Verbindung noch GPU-Beschleunigung.

Gaming und Coding sind inzwischen zusätzlich beauftragt. Feste Spieleziele:
Minecraft Java über den Linux-Pfad und Minecraft Bedrock für Windows über den
Windows-Pfad. Reihenfolge, Abnahmen und Hardwaregrenzen stehen in
`gaming-coding-plan.md`; die laufende Treiberarbeit wird zuerst abgeschlossen.

## Vorhandene Hardwareangaben

`acer-hardware.md` enthält die am 4. Oktober unter Linux erfassten Daten:

- Acer Aspire 5742G, Intel Core i3 M 370/Westmere, BIOS.
- WLAN: Broadcom BCM43225, PCI-ID **14e4:4357**, Linux bcma/brcmsmac.
- Ethernet: Broadcom BCM57780, PCI-ID **14e4:1692**, Linux tg3.
- Grafik: Radeon HD 5470, **1002:68e0**, THOS bisher Boot-Framebuffer.

Die Angaben müssen nicht erneut vom Nutzer abgefragt werden. Vor einem Hardware-
Test prüfen, ob sie weiterhin zur verwendeten Maschine gehören. Kein Gerätetest
allein aus einem erfolgreichen QEMU-Test ableiten.

## Erste Bestandsaufnahme

Im aktuellen Code vorhanden: `kernel/src/tg3.rs` und die tg3-Anbindung in
`kernel/src/net.rs`, neben virtio-net/e1000; DHCP- und DNS-Konfiguration im
Netzwerkstack. Die älteren Planabschnitte mit unbekannten Acer-IDs oder fehlendem
Ethernettreiber sind überholt. Ein Hardwaretest des tg3-Treibers ist dadurch
noch nicht nachgewiesen. Für BCM43225 wurde inzwischen eine PCI-/BCMA-Diagnose
ergänzt, noch kein betriebsfähiger WLAN-Treiber. Für Radeon gibt es eine Register-/
VRAM-Diagnose, noch keine GPU-Beschleunigung. Details in
`adapter-bringup-2026-10-11.md`.

Die [Linux-Wireless-Dokumentation](https://wireless.docs.kernel.org/en/latest/en/users/drivers/brcm80211.html)
bestätigt BCM43225/4357 als brcmsmac-Ziel. brcmsmac ist ein SoftMAC-Treiber
auf mac80211; Firmwareladen allein reicht daher nicht für eine WLAN-Verbindung.
Der Port braucht auch die passenden hostseitigen 802.11-Funktionen.

`software-install-plan.md` berichtet bereits über Debian-Paketdownloads und
einige echte x64-Windows-Programme. Die älteren Tabellen am Dokumentanfang
beschreiben einen früheren Stand. Jede aktuelle Aussage deshalb gegen Code
und konkrete ausführbare Tests prüfen. Die Devuan-Abschnitte im Haupt-README
sind ebenfalls kein Nachweis für Funktionen im eigenen THOS-Kernel.

## Reihenfolge

1. BCM43225: PCI-Erkennung, BCMA-/Chipzugriff, Reset/DMA/Interrupts,
   Firmwarebereitstellung und Diagnose; danach Scan, Verbindung und WPA2.
   Ethernet als vorhandenen unabhängigen Netzwerkpfad überprüfen, ohne WLAN
   hinter die komplette Software-Kompatibilität zu verschieben.
2. DHCP/DNS sowie HTTP/HTTPS und verifizierte Downloads bis zur Datei testen.
3. Firefox-Laufzeitbedarf mit THOS abgleichen und einen echten Starttest
   definieren; Downloadverzeichnis, Fortschritt, Abbruch und Dateiintegrität.
4. Rechtewechsel/doas/sudo und Debian-Pakete/apt: Abhängigkeiten, Installation,
   Konfigurationsdateien und reale Programmstarts. Vorhandenes `thos-pkg`
   nicht mit vollständiger apt-Kompatibilität gleichsetzen.
5. JDK-Laufzeit prüfen: Loader, Threads/Futex, Speicher-Mappings, Signale,
   Prozesse und Dateisystem; ein konkretes Java-Programm starten.
6. Echte x64-EXE und Installer als Referenzprogramme, danach MSI und nötige
   Win32/COM-/Installer-Funktionen; 32-Bit-Anforderungen gesondert behandeln.

Ein erfolgreicher Download, ein gestarteter Test-Loader und ein vollständig
benutzbares Programm sind getrennte Nachweise. Pro Etappe konkrete Programme
und Versionen festhalten, Fehler reproduzieren und schrittweise beheben.
