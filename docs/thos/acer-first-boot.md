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

## Daten sammeln (hwreport) und Programme installieren (thos-pkg)
- Nach dem Anmelden `hwreport` tippen: zeigt CPU (CPUID-Name und Flags), Speicher, alle PCI-Geräte (`/proc/pci`) und das gesamte Kernel-Log (`/proc/dmesg`, auch `dmesg`) und speichert alles in `/tmp/hwreport.txt`.
  Der Stick lässt sich danach an einem anderen Rechner lesen (ext2, Volume `THOSROOT`); die Datei ist die Grundlage, um `hw-target.md` mit echten Acer-Daten zu füllen und Treiberfehler zu finden.
- **Programme aus dem Debian-Archiv** (Netz nötig): `thos-pkg update` (lädt und indiziert die Paketliste, ca. 10 MB, dauert ein paar Minuten),
  danach `thos-pkg search <Wort>` und `thos-pkg install <Paketname>`; Abhängigkeiten werden mitinstalliert, Prüfsummen gegen den Index geprüft, alles landet in `/opt/root`
  (`/opt/root/usr/bin` steht im PATH, Bibliotheken über `LD_LIBRARY_PATH`). Beispiele, die mit dem echten Archiv laufen (`pkg-repo-test`, `pkg-apps-test`): `tree`, `htop`, `jq`, `ripgrep`, `nano`.
  **Sicherheit:** `thos-pkg update` prüft die Signatur der Datei `InRelease` mit `gpgv` und dem Debian-Archivschlüssel (`/usr/share/keyrings/debian-archive-keyring.pgp`, im Image mitgeliefert), danach die Prüfsumme der Paketliste und jedes einzelnen Pakets — die Kette ist also kryptografisch gesichert, auch über unverschlüsseltes http. Fehlt `gpgv`, warnt das Programm laut.
  **Grenzen:** Programme mit sehr vielen Abhängigkeiten (GTK, Qt) sind nicht das Ziel; Maintainer-Skripte (postinst) werden nicht ausgeführt; TLS (`ssl_client`) prüft keine Zertifikate (für Pakete egal, für Webseiten nicht).
- Eigene Dateien: `thos-pkg install <Datei-oder-URL>` entpackt `.deb`, `.rpm`, `.zip`, `.tar.gz/.xz/.bz2` oder eine `.exe` nach `/opt/<Name>` und legt Einträge im Startmenü an (`thos-pkg list`, `thos-pkg remove <Name>`).
  `.msi` wird abgelehnt (noch nicht unterstützt).

## Image-Varianten (mehr Platz, Browser)
- Standard: 128 MiB Root-Dateisystem. Für `thos-pkg install` empfiehlt sich mehr Platz: `THOS_FS_MIB=2048 cargo xtask bios-image --interactive` (Image dann ~2,1 GB; der Stick muss entsprechend groß sein).
- Mit Webbrowser (NetSurf/GTK 3, https, Download-Dialog): `THOS_NETSURF=1 THOS_FS_MIB=512 cargo xtask bios-image --interactive` — im Startmenü erscheint dann „Web browser". Ob und wohin NetSurf Downloads speichert, ist **noch nicht getestet** (HOME ist `/tmp`); heruntergeladene Pakete lassen sich im Dateimanager per Doppelklick installieren, oder gleich mit `thos-pkg install <URL>`.
- Mehr RAM im Test: `THOS_MEM=7800M cargo xtask <test>` (der Acer hat 7,6 GiB; 7,7 GiB nutzbar werden erkannt).

## Desktop automatisch starten
`thos-autostart on` legt `/tmp/autostart` an; danach startet nach jeder Anmeldung automatisch der Desktop (`desktop`). Beendet man ihn (Startmenü → „Quit desktop" oder Strg+Alt+Q), bleibt die Shell offen — der Rückweg, falls etwas schiefgeht.
`thos-autostart off` schaltet es wieder aus. Test: `cargo xtask autostart-test` (eigenes Image, nicht in der Suite).

## Stick schreiben und Root-Dateisystem vergrößern (Skript)
```
cargo xtask bios-image --interactive            # baut target/thos-bios.img (336 MB, Root 128 MB)
lsblk                                           # den STICK finden, z. B. /dev/sdb (nicht /dev/sda!)
sudo tools/make-stick.sh /dev/sdb               # schreibt das Image, erweitert Partition 2 und das ext2 auf den ganzen Stick
```
Das Skript weigert sich bei nicht-wechselbaren Datenträgern und bei eingehängten Partitionen und verlangt, dass man den Gerätenamen noch einmal eintippt.
Benötigt: `dd lsblk sfdisk e2fsck resize2fs partprobe` (Paket `util-linux`, `e2fsprogs`). Das Vorgehen ist mit einer Imagedatei durchgespielt (Partition erweitern, `resize2fs`, `e2fsck` sauber).
Beim Acer im BIOS (F2) „Boot Mode: Legacy" lassen, mit F12 den Stick wählen.


## Auf die interne SSD installieren (`thos-install`)
Läuft THOS vom Stick, kann es sich auf die interne Platte kopieren (in QEMU mit `cargo xtask install-test` geprüft: Stick → leere SATA-Platte → bootet allein).
1. Vom Stick starten und anmelden. Im Startmenü erscheint **Install THOS** (nur wenn von USB gestartet), oder im Terminal: `thos-install`.
2. Das Programm **löscht die ganze interne Platte** (`/dev/sda`): es fragt, ob du das Wort `ERASE` tippst, und danach das Administrator-Passwort (verdeckt eingegeben; nur das Admin-Konto darf das).
3. Kopiert werden MBR, Limines Zwischenraum, die Boot-Partition und von der Root-Partition nur die belegten Blöcke (laut Block-Bitmap), mit Fortschrittsanzeige. Der MBR kommt zuletzt: bricht etwas ab, ist die Platte nicht bootbar, und der Installer lässt sich einfach neu starten.
4. Danach herunterfahren, Stick ziehen, starten: THOS bootet von der Platte, dasselbe Konto gilt weiter.

Technik dahinter: neue Rohgeräte `/dev/sda` (SATA) und `/dev/sdb` (USB) samt `/dev/sda1..4` (nur root; die Platte, von der gebootet wurde, ist schreibgeschützt), `/proc/bootdisk` (`usb` oder `sata`), `pwrite64`, THOS-Aufruf `readpass`, `thoselevate` (Admin-Passwort, dann Programm als root).
Offen: die Root-Partition behält die Größe der Stick-Partition (der Rest der SSD bleibt frei); Größenanpassung folgt.
