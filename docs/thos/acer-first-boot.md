# THOS auf dem Acer Aspire 5742G — erster Start (Checkliste)

Stand 2026-10-04. Nichts davon ist auf der echten Hardware getestet; QEMU bildet BIOS/MBR/AHCI nach, aber nicht den Broadcom-Chip, den EHCI-Hub und die Radeon.

## Image bauen und auf einen Stick schreiben
```
export PATH=$PATH:/sbin:/usr/sbin
cargo xtask bios-image --interactive      # ergibt target/thos-bios.img (MBR, FAT32 /boot, ext2-Root, Limine BIOS)
sudo dd if=target/thos-bios.img of=/dev/sdX bs=4M conv=fsync status=progress     # sdX = der STICK, vorher mit lsblk prüfen!
```
Das Image ist rund 330 MiB groß. Beim Acer im BIOS (F2) "Boot Mode: Legacy" lassen und mit F12 vom Stick starten.
Neu: **THOS bootet komplett vom USB-Stick** (EHCI-Massenspeichertreiber, `usb-boot-test` in QEMU ohne SATA-Platte). Limine lädt den Kernel über das BIOS vom Stick,
danach liest und schreibt THOS sein Root-Dateisystem über USB. THOS benutzt **nur** ein ext2-Dateisystem mit dem Volume-Namen `THOSROOT`; die
Linux-Partitionen der internen Platte werden nie gemountet oder beschrieben. Findet THOS auf der SATA-Platte kein `THOSROOT`, sucht es am USB-Anschluss.
Den Stick an eine USB-2.0-Buchse des Acer stecken (der 5742G hat kein USB 3.0).

## Was beim Start auf dem Schirm / im Boot-Log zu sehen sein sollte
| Zeile | Bedeutung |
|---|---|
| `THOS: ahci ok ...` / `ext2 part  root FS in MBR partition` | SATA + Root gefunden |
| `THOS: boot  no THOSROOT filesystem on the SATA disk; trying USB storage` | normal beim Stick-Start |
| `THOS: usb msc ok  NNN MiB mass-storage device` | der Stick wurde gefunden (EHCI-Hub-Pfad ungetestet) |
| `THOS: ehci ok  USB keyboard attached` oder `ehci  no ...` | USB-Tastatur über EHCI |
| `THOS: net nic          tg3 xx:xx:...` | Ethernet-Treiber hat den BCM57780 initialisiert |
| `THOS: tg3 link        up 1000 Mb/s full duplex` | Kabel steckt, Gegenstelle antwortet (erst nach Autonegotiation, ~2 s) |
| `THOS: net dhcp         lease ...` und `net ok  ping ...` | Netz läuft |
| `THOS: net              tg3: ...` | Treiber meldet, woran es hakt (Text nennt den Grund) |

Danach: Administratorkonto anlegen, anmelden, `desktop` tippen (Compositor + Terminal). Weitere Programme: `wayland-info`, `foot`, `gtkhello` usw. sind nur in den Testbildern enthalten.

## Wenn etwas hängt
- Die serielle Konsole gibt es am Acer nicht; wichtige Zeilen stehen auf dem Bildschirm (Foto machen).
- Hängt der Start nach `random ... seeded`: Timer/APIC — bitte Foto.
- Tastatur reagiert nicht: PS/2 am Notebook ist der Normalfall (`ps2 ok`), USB-Tastatur nur mit EHCI.
- Ethernet ohne Link: erst Kabel einstecken, dann neu starten (der Treiber prüft den Link alle 250 ms und meldet `up`/`down`).
