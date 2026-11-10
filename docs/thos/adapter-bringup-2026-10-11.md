# WLAN und Grafik: erster Treiberschritt

Zielgeräte sind die dokumentierten Acer-Komponenten BCM43225 (14e4:4357) und
Radeon HD 5470/Cedar (1002:68e0). Dieser Stand ist eine Diagnosegrundlage,
kein betriebsfähiger WLAN- oder beschleunigter Grafiktreiber.

## Implementierung

- PCI-Konfigurationszugriffe sind über alle CPUs serialisiert; lokale Interrupts
  bleiben dabei ausgeschaltet. 16-Bit-Schreibzugriffe schreiben ausschließlich
  das angeforderte Wort statt benachbarte Write-one-to-clear-Statusbits mitzuschreiben.
- BCM43225: PCI-/D0-/BAR-Prüfung, ChipCommon-ID, BCMA-EROM und D11-Kern samt
  Reset-Wrapper. Der begrenzte Decoder verarbeitet erweiterte Adress-/Größenwörter
  und lehnt unvollständige Daten ab. PCI-Fenster und Command werden wiederhergestellt.
  Es werden weder Funk, Reset noch DMA aktiviert.
- Radeon: PCI-/D0-/BAR-Prüfung, lesender Zugriff auf CONFIG_MEMSIZE zur
  VRAM-Diagnose, sofern die Firmware Registerzugriff bereits aktiviert hat.
  Der Boot-Framebuffer bleibt aktiv; kein Modeset oder GPU-Kommandoprozessor.
- `/proc/wlaninfo` und `/proc/gpuinfo` liefern den erreichten Zustand.
  `hwreport` nimmt beide auf. Einstellungen → Network zeigt den WLAN-Adapter
  und verfügbaren Treiberstand; Display zeigt GPU, VRAM und Beschleunigungsstatus.
  Die vorhandene Controlbar öffnet weiterhin die Netzwerkeinstellungen; ein
  erkannter WLAN-Adapter ohne aktive Kabelverbindung erhält ein graues WLAN-Symbol.
  Erkennung allein wird niemals als Verbindung angezeigt.

## Firmwarebereitstellung (Fortsetzung am 11. Oktober)

Die unveränderten Originaldateien aus Linux Firmware `20251021` liegen unter
`assets/firmware`: BCM43225-Bundle samt Index und Cedar ME/PFP/RLC. Beide
vollständigen Herstellerlizenzen werden mit ausgeliefert. Der Imagebau prüft
`SHA256SUMS` und den tatsächlichen Broadcom-Parser und kopiert die Dateien nach
`/lib/firmware`. Änderungen an Firmware/Lizenzen invalidieren den Image-Cache.

Der Kernel liest nur begrenzte reguläre Dateien. Der Broadcom-Decoder prüft
Indexgröße, Dateigrenzen, doppelte Kennungen und Überlappungen. Das MIMO16-Profil
enthält 39.864 Mikrocode-Bytes; Größen-/Versionsfelder und die drei Init-Tabellen
werden vor einer späteren Upload-Implementierung geprüft. Die Radeon-Dateien
werden gegen die festen SHA-256-Hashes dieses Releases geprüft. Diese Prüfungen
belegen keine passende D11-Core-Revision und keinen erfolgreichen Firmwareupload.

`wlaninfo`/`gpuinfo` unterscheiden `firmware validated/missing/invalid` von
`firmware_loaded 0`. Die Einstellungsseiten zeigen „Available, not loaded“ bzw.
fehlende/fehlerhafte Dateien. Ein fehlender Adapter bleibt auch bei vorhandener
Firmware als fehlend ausgewiesen; Beschleunigung und Verbindung bleiben aus.

Der neue Parser wird vom Kernel und vom Imagebau als dieselbe Quelle verwendet.
Vier Host-Tests bestehen: Originalbundle, beschädigtes Größenfeld, abgeschnittene/
überlaufende Bereiche, überlappende/doppelte/leer angelegte Sektionen und 2.000
zufällige Indizes. `cargo check -p thos-kernel` und der erweiterte QEMU-`hw-test`
bestehen: Beide Firmwarepakete werden im echten THOS-Dateisystem validiert,
MIMO16-Größe stimmt, und beide Upload-Flags bleiben korrekt bei null.
Der anschließende `desk-settings-test` besteht ebenfalls. Die Network- und
Display-Aufnahmen mit den zusätzlichen Firmwarezeilen wurden visuell geprüft:
beide passen vollständig in das vorhandene Fenster. Gestaltung und Übergänge
bleiben erhalten. C-Syntaxprüfung und `git diff --check` bestehen.

## BCMA-Wrapper und Framebuffer-Cache (weitere Fortsetzung)

Der WLAN-Prober liest jetzt zusätzlich den D11-Reset-Wrapper über das zweite
PCI-Fenster. Er prüft Chip-Typ und Wrapper-Adresse und stellt beide Fenster
wieder her. `/proc/wlaninfo` und `hwreport` enthalten Core-Revision, Reset-/Takt-
Zustand und DMA64-Fähigkeit. Die Einstellungsseite kennzeichnet einen im Reset
befindlichen Kern als noch zu initialisieren. Die Reset-Sequenz selbst ist mit
Register-Readbacks, begrenzten Wartezeiten und Abbruch bei nicht antwortendem
Gerät vorbereitet, wird beim Boot aber noch nicht ausgeführt. Sechs Host-Tests
prüfen Reihenfolge, unveränderte Hardware bei Busy-Timeout, fehlende Geräte und
fehlgeschlagenen Reset. Das simuliert Registerverhalten, nicht den Acer-Chip.

Beim Grafikpfad wurde ein Fehler in der Cache-Konfiguration gefunden:
`map_mmio` überspringt vorhandene große HHDM-Seiten, während die bisherige
MTRR-Vorbereitung Framebuffer unterhalb des höchsten RAM-Endes ablehnte.
RAM oberhalb von 4 GiB ist dabei kein Beleg für RAM im darunterliegenden
PCI-Fenster. Der MTRR-Fallback prüft jetzt tatsächliche RAM-Intervalle und die
vollständige Abdeckung des Framebuffers (vier Host-Tests, 30.000 Zufallsfälle).

Für CPUs mit PAT verwendet THOS jetzt einen eigenen Framebuffer-Pfad nach dem
Prinzip von Linux `ioremap_wc`: PAT-Slot 1 wird auf Write-Combining gesetzt,
alle übrigen Slots bleiben erhalten. Große Identity-/HHDM-Seiten werden nur
für die betroffenen Bereiche geteilt; WC wird auf die Framebuffer-Seiten
begrenzt. Kernel-Aliase und `/dev/fb0`-Mappings verwenden denselben Slot.
Änderungen der Seitenschutzrechte erhalten Cache- und Gerätekennungen.
Jede CPU programmiert PAT vor ihrer ersten Framebuffer-Nutzung mit Cache-/TLB-
Flush und Register-Readback. Der Boot prüft beide Kernel-Aliase jeder Seite.
Der Desktop besitzt bereits einen `sfence` nach der Bildausgabe.

`framebuffer_wc_configured` in `/proc/gpuinfo`, `hwreport` und Display zeigt die
Konfiguration. Das ist keine Radeon-3D-Beschleunigung und keine FPS-Messung.
Auf PAT-Systemen werden die Firmware-MTRRs nicht verändert.

Der aktualisierte BIOS-Build und `hw-test` bestehen sowohl mit der normalen
QEMU-Konfiguration (2-MiB-Seiten) als auch mit `THOS_MEM=8G` und `-cpu max`
(1-GiB-Seiten). Beide Boots prüfen 1.000 Framebuffer-Seiten und melden
`framebuffer-wc=1`, beide CPUs online und unverändert keine Ziel-WLAN-/GPU-
Beschleunigung. Das belegt den Mapping-/Bootpfad, keine Geschwindigkeit auf
der Acer-Hardware.

Auch der anschließende `desk-drag-test` besteht auf dem Westmere-Modell ohne
1-GiB-Seiten: Mausknopf halten, Fenster an drei Positionen verschieben,
Loslassen, ursprüngliche Fläche neu zeichnen. Die Pixelvergleiche bestehen;
die Aufnahme wurde zusätzlich visuell geprüft. `desk-settings-test` besteht
ebenfalls; Network mit drei WLAN-Zeilen und Display mit sieben Zeilen passen
vollständig in das unverändert gestaltete Fenster. C-Syntaxprüfung der
Einstellungen (mit FreeType-Includes) und `git diff --check` bestehen.

## Noch erforderliche Arbeiten

BCM43225 braucht Chip-/Core-Reset, Takt-/Power-Management, Firmwareupload,
D11/DMA/Interrupts und hostseitige 802.11-Funktionen einschließlich Scan,
Association, WPA2 und einer Ethernet-Anbindung an den vorhandenen IP-Stack.
Scan und Connect werden erst mit den funktionierenden Backend-Aktionen eingebaut.

Radeon-Beschleunigung braucht unter anderem Firmwareupload, Speicher-/GART-Verwaltung,
GPU-Initialisierung, Command-Ring, Fences und einen Render-/Present-Pfad. Eine
gelesene VRAM-Größe weist keine funktionierende Beschleunigung nach.

QEMU emuliert keines der beiden Zielgeräte. Für weitere Hardwarearbeit ist ein
Acer-Boot mit diesem Image und ein `hwreport` erforderlich; QEMU-Tests sichern
den vorhandenen Desktop-/Netzwerkpfad und die Abwesenheit der Zielgeräte ab.

## Verifikation

- `cargo check -p thos-kernel`: bestanden (bestehende Warnungen bleiben).
- Host-Test des tatsächlichen BCMA-Decoders: vier Tests bestanden, einschließlich
  abgeschnittener EROMs, erweiterter Deskriptoren und 2.000 Zufallseingaben.
- `cargo xtask hw-test`: bestanden; QEMU meldet keinen BCM43225 und keine
  Radeon-Beschleunigung. Hardwarebericht enthält beide Diagnosen.
- `cargo xtask desk-settings-test`: bestanden; About → Network → Display.
  QEMU-Aufnahmen von Network und Display visuell geprüft: Karten passen in das
  bestehende Fenster, keine abgeschnittenen Zeilen, ursprüngliche Gestaltung.
- `cargo xtask e1000-test`: e1000 und e1000e bestanden, jeweils DHCP und 3/3
  Ping-Antworten nach der PCI-Korrektur.
- `cargo xtask desk-panel-test`: bestanden; echte Maus-Klicks starten Terminal
  und über die Netzwerk-Controlbar die Einstellungen mit `SETTINGS_PAGE=1`.
- C-Syntaxprüfung für Settings/Panel und `git diff --check`: bestanden.

Die QEMU-Folgetests verwenden Kopien des frisch gebauten `target/thos-bios.img`.
WLAN-Verbindung, Radeon-MMIO auf echter Hardware und GPU-Beschleunigung wurden
damit **nicht** geprüft. Ein Acer-Hardwarebericht ist weiterhin erforderlich.

## Nach Acer-Rückmeldung: Netzwerk- und Bedienkorrekturen

Der zuerst aktualisierte Stick war ein Diagnose-Zwischenstand. Er bietet noch
keine benutzbare Broadcom-WLAN-Verbindung oder Radeon-Beschleunigung. Der Acer
meldet BCM43225 `probe-failed / Empty Address Region`, Firmware geprüft und
39864 Microcode-Bytes, aber nichts geladen. Die Radeon meldet 512 MB und
Framebuffer-WC, aber weiterhin Software-Rendering.

- BCMA-EROM: bei expliziten 64-Bit-Größen wurde das hohe Wort gelesen, aber
  ignoriert. Eine gültige 4-GiB-Region mit niedrigem Wort null wurde deshalb als
  leer verworfen. Der Scanner berücksichtigt nun beide Wörter. Fünf Hosttests
  bestehen einschließlich einer solchen Bridge vor einem D11-Kern; der
  konkrete Acer-Fehler ist erst nach erneutem Hardwaretest bestätigt behoben.
- Netzwerk: keine statische QEMU-Adresse als DHCP-Fallback. `/proc/netinfo`
  unterscheidet fehlenden Adapter, ausgeschaltetes Ethernet, fehlenden Link,
  wartendes DHCP und wirklich konfigurierte Verbindung. Physischer Linkstatus
  wird für e1000, tg3 und virtio ausgewertet.
- `/dev/netctl`: Administrator kann Ethernet trennen, DHCP erneut anfordern
  und validierte IPv4-/Gateway-/DNS-Einstellungen setzen. Ungültige Requests
  verändern die laufende Konfiguration nicht. Einstellungen gelten aktuell
  für die Sitzung, sind noch nicht persistent.
- Settings: getrennte Ethernet-/WLAN-Seiten im bestehenden Layout. Ethernet
  hat die genannten echten Steuerungen. WLAN zeigt genaue Diagnose und kann
  die Hardwareprüfung erneut ausführen; Scan/Verbinden fehlen weiterhin.
- Terminal: hellblaue Promptzeile mit Abstand zum vorherigen Befehlsoutput.
- Windows-Tasten: PS/2-Decoder ergänzt linke/rechte Super-Modifikatoren;
  Desktop/Panel öffnen oder schließen Start bei alleiniger Betätigung.
- Demo-Starter: `/thoswin` war ein parametrierter Testclient und beendete sich
  ohne Argumente. Der Menüeintrag startet jetzt die vorhandene `winhello.exe`.
- Shutdown: erster Klick zeigt Bestätigung mit Abbrechen. Bestätigen synchronisiert
  das Dateisystem vor dem Poweroff. Offene Apps erhalten noch keine geordnete
  Speichern-/Beenden-Anfrage; vollständiger Sitzungs-Shutdown bleibt offen.

Validierung: `cargo xtask net-settings-test` mit frisch gebautem BIOS-Abbild
bestanden: Disconnect entfernt IP/DNS; manuelle IPv4 aktiv; ungültige Eingabe
lässt die Konfiguration unverändert; echte DHCP-Erneuerung; isolierter virtio
ohne DHCP-Server bleibt mit `ip 0.0.0.0` unkonfiguriert. Reiner IPv4-Parser und
Zustandsmodell: drei Hosttests bestanden.

Weitere QEMU-Prüfungen auf dem anschließenden BIOS-Build bestanden:

- `desk-settings-test`: About/Network/Display und per Maus geöffnete Ethernet-
  und WLAN-Seiten; beide neuen Seiten zusätzlich visuell geprüft.
- `desk-menu-test`: Windows-Taste öffnet/schließt Start, Shutdown-Bestätigung
  erscheint, Abbrechen erhält die Sitzung, Pinning und Programmstart funktionieren.
- `desk-term-test`: Shellbefehle und deutsche Tastaturbelegung funktionieren,
  die hellblaue Promptzeile ist in der Aufnahme nachgewiesen und visuell geprüft.
- `desk-win32-test`: vorhandene MinGW-EXE öffnet ein Fenster, zeichnet und
  verarbeitet den Mausklick als WM_LBUTTONDOWN. Das belegt die Demo-APIs,
  keine allgemeine Kompatibilität mit Windows-Programmen.

Der anschließend neu gebaute Live-Stick muss separat geprüft werden. Der
physische Acer hat diese Nachkorrekturen bisher nicht getestet.

## Primärquellen

- [Linux BCMA-Registerformat](https://github.com/torvalds/linux/blob/master/drivers/bcma/scan.h)
  und [Scanner](https://github.com/torvalds/linux/blob/master/drivers/bcma/scan.c).
- [BCMA-PCI-Host](https://github.com/torvalds/linux/blob/master/drivers/bcma/host_pci.c)
  und [ChipCommon-Register](https://github.com/torvalds/linux/blob/master/include/linux/bcma/bcma_driver_chipcommon.h).
- [brcmsmac-Geräte und SoftMAC-Einordnung](https://wireless.docs.kernel.org/en/latest/en/users/drivers/brcm80211.html).
- [Evergreen-Register](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/evergreend.h)
  und [VRAM-Einheiten](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/radeon/evergreen.c).
- [Linux PAT und Speicher-Aliase](https://docs.kernel.org/arch/x86/pat.html)
  und [MTRR-Fallback statt direkter MTRR-Treiberkonfiguration](https://docs.kernel.org/arch/x86/mtrr.html).
- [Intel SDM, Band 3A, Abschnitt 11.12 und Tabelle 11-7](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).
