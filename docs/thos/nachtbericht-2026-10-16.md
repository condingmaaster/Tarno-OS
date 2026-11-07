> **Nutzerstopp:** Automatische Fortsetzung um 06:23 Uhr ausdrücklich abgesagt und Automation pausiert. Keine weitere automatische Arbeit; erst nach neuer ausdrücklicher Freigabe fortsetzen.

# THOS – Nachtbericht vom 16. Oktober 2026

Die vorhandene Oberfläche samt Controlbar, Animationen und Effekten bleibt erhalten. Die echte Tastatur wurde nicht verändert oder an QEMU durchgereicht; kein ASUS-Programm wurde auf dem Host installiert. Kein physischer Datenträger wurde heute aktualisiert.

## Im lokalen Live-Abbild angekommen

- Dynamische Windows-Exportauflösung findet jetzt die ergänzten System-DLL-Funktionen. 19 echte PE-Prüfungen einschließlich Weiterleitungen, Fehlerfällen und gezählten Ldr-Namen bestehen; synthetische CRT-/GUI-Module sind korrekt im PEB sichtbar.
- Anonyme Speicherbereiche werden per binärer Suche und mit weniger wiederholten Vektorallokationen verwaltet. Zwei kontrollierte Gastvergleiche in entgegengesetzter Reihenfolge: VMA-Arbeitslast 739/760 statt2.220/2.227ms. Das ist ungefähr dreifach schneller **für diese Arbeitslast**, kein pauschaler OS-/Fenster-Faktor. Datei-Mapping wurde dabei nicht klar schneller.
- Wayland-Eingabe berücksichtigt Unterflächen, deren Reihenfolge und Eingaberegionen sowie das Halten einer gezogenen Mausaktion. 13 echte Clientphasen und zehn bestehende Pixelphasen bestehen.

Auch sechs native Apps und echte Windows-Fenster bestehen Resize, Maximieren, Vollbild und Wiederherstellen. Speicher-/Schutz-/Fork-/Konkurrenztests bestehen auf vier virtuellen CPUs. Das exakte Live-Paket startet offline mit emulierter Westmere-CPU, Desktop, Controlbar und Terminal; Screenshot visuell geprüft.

Aktuelles [lokales Live-Abbild](../../target/thos-live.img): SHA256 `09370f44c373446504f6ed15d0b78c00df1d0de91f1fdefe2a1438bfcaf9a3b1`. [Manifest](../../target/thos-live-2026-10-16-regions-input-build-manifest.json). Vorgänger und frühere Backups bleiben erhalten. Der optionale kleine Heapcache und VM-Diagnoseausgaben sind nicht im Live aktiviert.

## Noch offen

Firefox rendert die lokale Downloadseite, der Download-/Eingabetest ist jedoch noch nicht erfolgreich. Ein Lauf belegte tatsächlichen Speichermangel durch vollständiges Kopieren großer Prozessspeicher bei Fork. Ein kleinerer Testprozesspool verhindert diesen Seitenfehler im Folgelauf, löst den verbleibenden Fehler nicht. Echte GTK3-Mausereignisse mit normaler Clientdekoration bestehen separat. Die Firefox-Crash-Helfer-Anmeldung zeigt Zeitüberschreitungen; die letzte Probe mit gesetzter Abschaltvariable blieb ebenfalls erfolglos und der Crash-Reporter war dabei weiterhin aktiv. Keine vollständige Firefox-/Download-Kompatibilität behauptet.

ASUS Aura Creator/Armoury Crate bleiben am Installer und weiteren Windows-Laufzeitlücken blockiert. SkiFree ist ein nicht unterstütztes 32Bit-Programm. Echte Hardware-WLAN-/GPU-Unterstützung, vollständiges DLL-Nachladen, Dienste/COM/UWP und weitere Linux-Funktionen bleiben Arbeit für die folgenden Schritte.

Nachweise und Screenshots: [Arbeitsbericht](worklog-2026-10-16.md), [Fortsetzungsstand](resume-2026-10-16.md), [Live-Desktop](screenshots/2026-10-16/regions-input-live-desktop.png), [GTK-Eingabetest](screenshots/2026-10-16/gtk-input-button.png).

## Stopp und Fortsetzung

Bei95% Verbrauch (5% Rest) gestoppt. Alle QEMU-Gäste sind beendet. Nächster tatsächlich gemeldeter Reset:16.Oktober06:22:11 Europe/Berlin; bestehende einmalige Fortsetzung auf06:23 aktualisiert. Vor Arbeitsbeginn wird das tatsächliche Kontingent erneut geprüft. Eine neue Pause-/Stoppanweisung hat Vorrang.
