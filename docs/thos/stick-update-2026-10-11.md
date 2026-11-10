# Vorbereitetes THOSBOOT-Update

Status: nach ausdrücklicher Freigabe am 11. Oktober 2026 auf den USB-Stick
geschrieben und vollständig zurückgelesen/geprüft.

Ziel bei der Prüfung: `/dev/sdb`, USB, wechselbar, cMobile Line,
Seriennummer `900066D1FAB9B821`, 58,6 GiB. Eine FAT32-Bootpartition
`THOSBOOT`, Startsektor 2048, 1.228.800 Sektoren (600 MiB).
Limine lädt `/boot/thos-root.ext2` als RAM-Root. Der gleiche Live-Bootpfad
wird beim Update verwendet; keine Umstellung auf einen persistenten Disk-Root.

Die vollständigen belegten 601 MiB einschließlich MBR und Bootpartition wurden
vorab nur lesend gesichert: `target/thos-stick-backup-2026-10-11.img`.
SHA-256: `ba12cddf08924aae71db2d28efa4b4983513d9d68d815bfdf18d5477be4f8125`.
Das ist keine Sicherung der restlichen, unpartitionierten Kapazität des Sticks.

Erstes aufgespieltes Update: damals `target/thos-live.img`, jetzt unverändert
gesichert als `target/thos-live-deployed-2026-10-11.img`, gebaut mit
`THOS_FS_MIB=512 cargo xtask release-ram-image`, 601 MiB, gleiche Partitionierung.
SHA-256: `b68e8c5108c90aee21acfe4dc635dd6728d0af0563f628ac6d5530048e24e929`.
Der vorher getestete Disk-Build wurde unter
`target/thos-tested-bios-2026-10-11.img` erhalten.

Prüfungen: ext2 `e2fsck -fn` und FAT `fsck.fat -n` bestehen. Das genaue
Live-Image besteht `autostart-test` mit 7.800 MiB RAM und
`Westmere,-pdpe1gb`: RAM-Root geladen, PAT-WC eingerichtet, beide CPUs online,
Desktop und Controlbar automatisch gestartet. Die vorherigen Fensterzieh-
und Settings-Prüfungen des gleichen Kernel-/UI-Stands bestehen ebenfalls.
Der spätere Acer-Bericht bestätigt Desktopstart, Radeon-VRAM/WC und
WLAN-Erkennung; WLAN bleibt `probe-failed / Empty Address Region`, beide
Firmware-Uploads und Grafikbeschleunigung fehlen.

Vor einem Schreibvorgang Ziel anhand der Seriennummer erneut prüfen.
Das Schreiben ersetzt das bisherige THOS-System im Bootbereich; dessen
Sicherung liegt vor. WLAN-Verbindung und Radeon-3D-Beschleunigung sind noch
nicht einsatzbereit.

## Ausgeführtes Update

Vor dem Schreiben bestanden die SHA-256-Prüfung des Live-Images und die
erneute Seriennummernprüfung. Der Stick war nicht eingehängt. Das vorhandene
`tools/make-stick.sh` schrieb die 630.194.176 Bytes des Live-Images auf
`/dev/sdb` und führte Flush/Sync und Partitionserkennung aus.
Nach Leeren der Linux-Gerätepuffer bestand `cmp -n 630194176` gegen das
Image ohne Abweichung. Die Partition bleibt `THOSBOOT`, FAT32, 600 MiB;
der Stick ist weiterhin nicht eingehängt. Der übrige Datenträger wurde
vom Schreibvorgang nicht überschrieben. Der Acer-Boot wurde anschließend
durch den Nutzer bestätigt; siehe die oben dokumentierten Treiberfehler.

## Neues Abbild nach den Acer-Rückmeldungen (noch nicht aufgespielt)

`target/thos-live.img` enthält jetzt die Netzwerksteuerung, echten DHCP-Status,
BCMA-64-Bit-Größenkorrektur, farbige Terminal-Promptzeile, Windows-Tasten-
Unterstützung, korrigierten Demo-Starter und Shutdown-Bestätigung. Weiterhin
601 MiB mit 512-MiB-RAM-Root und gleicher Bootpartition.

SHA-256: `feafbeac7e6203f8eb2b96fa6b1b9ae4f31ffe39cc45db831dcff31309fb6478`.
Prüfsumme liegt zusätzlich in `target/thos-live.img.sha256`.

Die BIOS-Prüfungen für Netzwerksteuerung, neue Settings-Seiten, Windows-Taste,
Shutdown-Abbrechen, Terminal und Win32-Demo bestehen. Das genaue neue Live-
Abbild besteht ebenfalls `autostart-test` mit `THOS_MEM=7800M` und
`THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'`: Desktop und Panel starten nach
dem Login automatisch. Das ist eine QEMU-Prüfung, keine Acer-Treiberprüfung.

Der Intenso-Stick ist momentan am Entwicklungsrechner nicht angeschlossen.
Deshalb wurde dieses Abbild noch nicht auf den physischen Stick geschrieben.
WLAN-Scan/Verbindung, GPU-Beschleunigung, USB-Maus/Hotplug, native App-
Größenänderungen und persistente Nutzung des übrigen Sticks bleiben offen.

## Neuer Stand am 13. Oktober (noch nicht auf dem Stick)

`target/thos-live.img` wurde erneut gebaut: 601 MiB, SHA-256
`a6a597c9764b2a0ac5597b1f11c92777ef2a49ac589ce99f799ac2111da2e644`. Enthält nun native Resize-/Maximize-/Vollbildfunktionen,
Downloadfenster, curl/CA-Zertifikate, Development-Settings und die aktuellen
Dateisystem-/Synchronisationskorrekturen. OpenJDK und Firefox-Proben liegen
separat im größeren BIOS-Abbild. Der tatsächliche USB-Stick bleibt beim
Deployment vom 11. Oktober; aktuell ist er am Buildhost nicht angeschlossen.

Abschließender Live-Boot: `autostart-test` besteht für das neue
`target/thos-live.img`, mit `THOS_MEM=7800M` und
`THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'`. Desktop, Panel und Terminal
starten automatisch. Der Test lief auf einer privaten Abbildkopie.
