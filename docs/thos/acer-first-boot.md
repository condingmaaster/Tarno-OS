# THOS auf dem Acer Aspire 5742G — erster Start (Checkliste)

Stand 2026-10-04. Nichts davon ist auf der echten Hardware getestet; QEMU bildet BIOS/MBR/AHCI nach, aber nicht den Broadcom-Chip, den EHCI-Hub und die Radeon.

## Image bauen und auf einen Stick schreiben
```
export PATH=$PATH:/sbin:/usr/sbin
cargo xtask bios-image --interactive      # ergibt target/thos-bios.img (MBR, FAT32 /boot, ext2-Root, Limine BIOS)
sudo dd if=target/thos-bios.img of=/dev/sdX bs=4M conv=fsync status=progress     # sdX = der STICK, vorher mit lsblk prüfen!
```
Das Image ist rund 330 MiB groß. Beim Acer im BIOS (F2) "Boot Mode: Legacy" lassen und mit F12 vom Stick starten.
Das Root-Dateisystem darf auf einem Stick schreiben; **auf die interne Platte schreibt THOS in diesem Stand nichts**, solange du das Image nicht selbst dorthin kopierst.

## Was beim Start auf dem Schirm / im Boot-Log zu sehen sein sollte
| Zeile | Bedeutung |
|---|---|
| `THOS: ahci ok ...` / `ext2 part  root FS in MBR partition` | SATA + Root gefunden |
| `THOS: ehci ok  USB keyboard attached` oder `ehci  no ...` | USB-Tastatur über EHCI (Hub-Pfad ungetestet) |
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
