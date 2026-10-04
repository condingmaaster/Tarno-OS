# Acer Aspire 5742G — Hardware (gelesen am 2026-10-04 per SSH, nur Lese-Befehle)

Die Maschine lief mit Linux (KDE Plasma); Angaben aus `lspci -nnk`, `lsusb`, `lscpu`, `free`, `lsblk`, `/proc/bus/input/devices`.

| Baustein | Gerät | ID | Linux-Treiber | THOS heute |
|---|---|---|---|---|
| CPU | Intel Core i3 M 370 (Westmere, 2 Kerne / 4 Threads, VT-x, SSE4.2, kein AVX/AES-NI) | — | — | läuft (SMP) |
| RAM | 7,6 GiB | — | — | — |
| Chipsatz | Intel HM55 (5 Series / 3400) | 8086:3b09 | — | — |
| Firmware | **BIOS**, kein UEFI | — | — | BIOS/MBR-Image vorhanden |
| Platte | SSD/HDD 223,6 GiB an AHCI | 8086:3b29 | ahci | AHCI-Treiber (NCQ) |
| **Ethernet** | **Broadcom NetLink BCM57780** | **14e4:1692** | tg3 | **kein Treiber** (nicht Atheros!) |
| WLAN | Broadcom BCM43225 (b/g/n) | 14e4:4357 | bcma/brcmsmac | kein Treiber (braucht Firmware) |
| Grafik | AMD Mobility Radeon HD 5470 "Park" (Evergreen/Cedar) | 1002:68e0 | radeon | nur VBE-Framebuffer |
| USB | 2× Intel EHCI (USB 2.0), **kein xHCI** | 8086:3b3c, 8086:3b34 | ehci-pci | nur xHCI-Treiber → **EHCI fehlt** |
| Eingabe | PS/2-Tastatur, Synaptics-Touchpad (PS/2) | — | atkbd, psmouse | läuft (Tastatur, Maus als Standard-PS/2) |
| Sonstiges | Webcam (ALi 0402:9665, USB), HD-Audio (Intel 8086:3b56 + HDMI), SMBus, MEI | — | — | — |

Folgen für die Planung:
- Der Netzwerktreiber muss ein **Broadcom tg3 (BCM57780)** werden, nicht Atheros. Das ist ein Gigabit-Chip mit eigener Ring-/Statusblock-Logik (Familie BCM5717/57780); größer als der e1000-Treiber.
- USB-Tastatur/-Maus/-Stick am Acer laufen über **EHCI** (zwei Controller mit je einem Root-Hub und internem Rate-Matching-Hub). Ohne EHCI keine externe USB-Hardware.
- Grafik: Evergreen-Chip (Radeon HD 5000er), Beschleunigung wäre ein eigenes Projekt; bis dahin Software-Rendering auf dem Framebuffer.
- WLAN (Broadcom, Firmware nötig) ist die letzte Stufe; Ethernet zuerst.

## EHCI-Treiber (kernel/src/ehci.rs, 2026-10-04)
BIOS-Übergabe, Reset, Port-Reset, Steuerübertragungen über den asynchronen Zeitplan, Enumeration, HID-Boot-Tastatur per GET_REPORT-Abfrage.
`ehci-test` (QEMU, PS/2-Controller abgeschaltet): Anmeldung und Tippen laufen allein über die USB-Tastatur — **bestanden**.
Der Weg über einen **Hub** (so hängt am Acer alles: interner Rate-Matching-Hub 8087:0020) ist implementiert, aber **ungetestet**, weil
QEMUs `usb-hub` nur Fullspeed kann und nicht an einen EHCI-Port passt. Erster echter Test: am Acer.
