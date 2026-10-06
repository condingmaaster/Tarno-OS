# THOS auf dem Acer: Plan bis „eigenständiges Betriebssystem“

*Stand 2026-10-06. Ziel (Jona, 2026-10-06): in **einem Monat** läuft THOS auf dem Acer Aspire 5742G so, dass es als eigenes Betriebssystem durchgeht; Reserve bis **1,5 Monate**.*

„Eigenständig“ heißt hier: von der internen SSD booten, ohne Stick und ohne zweites System; anmelden; einen Desktop sehen, der gut aussieht; ins Netz kommen; Programme starten, installieren und entfernen; Dateien verwalten; sauber herunterfahren; und das alles über mehrere Stunden stabil.

## Was schon da ist (QEMU geprüft)
Desktop mit schwebender Leiste, Startmenü, Dateimanager, Software-Zentrale, Bildbetrachter, Terminal; Paketinstallation aus dem Debian-Archiv (signiert geprüft); Linux-Programme (dynamisch) und 64-Bit-Windows-Programme in Grenzen; Netz (virtio/e1000), https; Treiber für AHCI, PS/2, EHCI, HDA, tg3 — **die letzten vier nie auf echter Hardware gelaufen**.

## Die größten Risiken
1. **Hardware, die nur der Acer prüfen kann:** tg3 (Ethernet), EHCI-Hub (USB), HDA (Ton), Touchpad, Helligkeit, Akku. Jeder Fehler dort kostet einen Zyklus „Stick schreiben, starten, Foto vom Bildschirm“. Darum: **früh, oft und mit `hwreport` testen**.
2. **WLAN** (Broadcom BCM43225) braucht Firmware und einen eigenen Treiber. Realistisch nur per Kabel in diesem Zeitraum, es sei denn, ein USB-WLAN-Stick mit einfachem Chip wird gekauft.
3. **Grafik:** nur der VBE-Framebuffer (Software-Rendering). Das ist beim Acer (1366x768) in Ordnung, ein GPU-Treiber ist kein Ziel dieses Monats.
4. **Browser:** ein vollwertiger moderner Browser (Firefox/Chromium) ist auf dieser Hardware und mit diesem System nicht in einem Monat erreichbar; NetSurf ist das Ziel.

## Wochenplan

### Woche 1 (bis ca. 13.10.): Erster echter Start und Hardware-Wahrheit
- Stick-Image bauen und auf den Intenso-Stick schreiben (Anleitung in `acer-first-boot.md`).
- Du startest, machst Fotos / `hwreport`; ich werte aus und behebe, was nicht läuft (Reihenfolge: Bildschirm, Tastatur, Touchpad, SSD, Ethernet, USB, Ton).
- Parallel: Design fertigstellen (Fenster, Leiste, Menü, Programme) und Screenshots für die Doku.

### Woche 2 (bis ca. 20.10.): Installation auf die SSD
- Installer im System: Platte wählen, Partition und ext2 anlegen, Limine in den MBR schreiben, System kopieren. Schutz: THOS fasst nur Platten an, die du ausdrücklich bestätigst; nie `THOSROOT`-fremde Partitionen ohne Rückfrage.
- Erster Start: Konto anlegen, Tastaturlayout, Zeitzone, Rechnername; danach Anmeldebildschirm im neuen Design.
- Einstellungen-Programm (Netz, Anzeige, Konten, Autostart, Aussehen).

### Woche 3 (bis ca. 27.10.): Netz, Browser, Software
- NetSurf ins Standard-Image, Downloads in einen Ordner „Downloads“, Zertifikatsprüfung mit mitgelieferten Wurzelzertifikaten.
- Software-Zentrale mit Kategorien und Installationsfortschritt; `.deb`-Installation per Doppelklick im Dateimanager mit Fortschritt statt Terminal.
- DHCP-Erneuerung, DNS-Zwischenspeicher, Netzwerkanzeige in der Leiste (echter Zustand).

### Woche 4 (bis ca. 3.11.): Notebook-Funktionen
- Akku und Netzteil (ACPI), Anzeige in der Leiste; Deckel zu → Bildschirm aus / Ruhezustand prüfen.
- Helligkeit (VBE/ACPI), Lautstärke-Tasten, Synaptics-Touchpad (Scrollen, Tippen zum Klicken).
- Ton aus dem Lautsprecher mit Lautstärkeregler; Kopfhörer-Erkennung.

### Woche 5 (bis ca. 10.11.): Stabilität und Politur
- Dauerlauf (mehrere Stunden), Speicher- und Plattenfehlerpfade, Absturzberichte in eine Datei, Wiederherstellung nach hartem Ausschalten (ext2-Prüfung beim Start).
- Aktualisierungsweg für das System selbst (neues Image einspielen).
- Feinschliff im Design: Animationen, Symbole, Schriftgrößen, Hochkontrast-Variante.

### Woche 6 (Reserve, bis ca. 17.11.)
Alles, was auf echter Hardware länger gebraucht hat als geplant.

## Was dafür von dir kommt
- Nach jedem Image: starten, `hwreport` ausführen (oder Foto), zurückmelden. Ein Zyklus pro Tag reicht.
- Ein USB-WLAN-Stick und ein Netzwerkkabel im Testaufbau, falls Netz gewünscht ist.
- Entscheidungen zum Aussehen (Farben, Logo, Name der Oberfläche).

## Prüfpunkte („fertig“ heißt)
- [ ] Kaltstart von der SSD in unter 30 s bis zum Anmeldebildschirm.
- [ ] Desktop 60 Bilder/s bei Fensterbewegung auf dem i3-370M.
- [ ] Ethernet, Ton, Touchpad, USB-Stick, Akku funktionieren.
- [ ] Ein Programm per Software-Zentrale installieren, starten, entfernen.
- [ ] Sauber herunterfahren und neu starten, 100 Mal ohne Dateisystemfehler.
- [ ] 4 Stunden Dauerbetrieb ohne Absturz.
