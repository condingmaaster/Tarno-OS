# GParted Live – Vorbereitung 17. Oktober 2026

Host: HP EliteBook840G1, UEFI. SSD SamsungMZ7LN512HMJP-00000,SerialS2MHNX0J200557,476,9GiB. Linuxext4 UUID80a6356e-b707-4446-9ca5-6e64074a978b. EFI UUID95F9-10A8.

Neuer fürGPartedfreigegebenerStick: Intenso cMobileLine,Serial2608260739440441,59GiB,beimPrüfen/dev/sdc,LabelINTENSO/exFAT,unmounted. AlterTHOSBOOTSerial900066D1FAB9B821istabgezogen, NICHTbeschreiben. Gerätebuchstabenkönnensichändern, immerSeriennummerprüfen.

OffiziellesISO gparted-live-1.8.1-6-amd64.iso,Downloadhttps://gparted.org/download.php, SHA256lauthttps://gparted.org/gparted-live/stable/CHECKSUMS.TXT: d789c38779f0d6f7026c12f44c2c52a04f66e28a1aea7d51f3045ad1bbf28411. ISO vollständig heruntergeladen und SHA256 gegen offiziellen Wert geprüft. Neuer Stick hat GPT/2GiB FAT32 EFI-Systempartition GPARTED. Alle336 Dateihashes nach Unmount/Flush/erneutemNurLeseMount identisch zurISOExtraktion; fsck.vfat -n erfolgreich. UEFI-Startdateien vorhanden, kein tatsächlicher Hardwareboot behauptet. Restlicher Stick unzugeordnet. AlterTHOSStick weiterabgezogen. USB ausgehängt. SSD-Partitionstabelle nachher mitVorherdump percmpidentisch. Protokoll:logs/2026-10-17/gparted-new-intenso-write.json.

## Nach fertig geprüftem Stick

Vor dem Resize wichtige persönliche Dateien auf getrenntem Medium sichern. Die vorhandene GPT-Textdatei ist KEINE Datensicherung. Rechner am Netzteil betreiben. Live-Start beendet die lokale Codex-Sitzung; kein automatisches Weiterarbeiten inGPartedbehaupten.

1. Am HP EliteBook beim Neustart Esc drücken, danach F9 (HP Boot Device Options), den neuen Intenso als UEFI-USB wählen. GPartedLive Defaultsettings, Standardtastatur/gewünschteSprache, grafischenStartwählen.
2. InGParted die SamsungSSD mit476,9GiB auswählen, nicht den59GiBUSB. Partitionen neu prüfen: ext4Linuxrund288,5GiB, davor rund179,5GiB unzugeordnet; EFI,Swap undRestpartitionen unverändertlassen. Linuxmussunmountedsein. BeiabweichenderAufteilungSTOPundneuprüfen.
3. Rechtsklick auf ext4Linux,Resize/Move. LinkenRandbisandenAnfangdesdirektangrenzendenfreienBereichsziehen, rechtenRandunverändertlassen. FreierPlatzdavor0MiB, neueGrößerund468,06GiB (=479297MiBbeiunveränderterAufteilung). BestehendeLinuxpartitionbehalten, NICHTformatieren,keineneuePartitionstabelleerstellen.
4. Vorschauprüfen: ausschließlich diese ext4-Partition nachlinksschieben/vergrößern. AnderePartitionendürfennichtgelöschtwerden. Anwenden. Das kann lange dauern; Strom nicht trennen und nicht unterbrechen.
5. NachfehlerfreiemAbschluss herunterfahren, LiveStickabziehen undLinuxbooten. RootUUID/Dateien/df/Bootprüfen. FallsFehlerkeineweiterenPartitionsänderungen,Fehlermeldungsichern.

AktuelleIST-Grenzen(nurReferenz,vorLiveoperationneuprüfen): freierBereichStart4018176End380555263,Sektoren512Byte. LinuxStart380555264End985618431. GeplanteerweitertePartitionStart4018176,End985618431. DieseWertealleinsindKEINsicheresMoveVerfahren; nurPartitionstabellenstartzuändernwürdeDatenunzugänglichmachen.

Referenzhttps://gparted.org/display-doc.php?lang=C&name=help-manual.
