# THOS-Arbeitsbericht für die Nacht vom 14. auf den 15. Oktober 2026

Das bestehende Layout, die Controlbar, Animationen und Effekte bleiben
bestehen. Die Arbeit konzentriert sich auf konkrete Fehler und die bereits
beauftragten Funktionen. Die allgemeine Gesamtprüfung bleibt vorgemerkt.

## Geprüfte Verbesserungen

- Private RAM-Reservierungen belegen erst beim Zugriff physische Seiten.
  Der Gasttest reserviert 1 GiB in einem System mit 512 MiB RAM und benötigt
  zunächst rund 280 KiB einschließlich des Messpuffers. Schutzrechte,
  Inhaltserhalt, Fork und konkurrierende Zugriffe wurden geprüft.
- Ein gemeinsamer Ausnahmestack konnte bei wartenden Seitenfehlern von
  anderen Threads überschrieben werden. Seitenfehler verwenden jetzt den
  Threadstack; der Stresstest mit vier CPUs und acht Threads besteht.
- Das Startmenü funktioniert auch mit vielen installierten Programmen.
  29 Einträge, Scrollen zum letzten Eintrag, Anheften und Starten bestehen.
- Repo-Pakete erhalten einen passenden Startmenü-Eintrag. Fremde Programme
  und doppelte Einträge werden bei der Registrierung vermieden.
- THOS-Frontends für sudo und doas starten nach echter Passwortprüfung einen
  Root-Befehl. Der aufrufende Benutzer bleibt unverändert. In Settings unter
  Development führt „Admin terminal“ zur Root-Shell im vorhandenen Terminal.
  Passwortmaskierung, Standard-Ein-/Ausgabe und Rückgabewerte sind geprüft.
- USB-DMA bleibt unterhalb von 4 GiB. Vertauschte Hub-/Portfelder wurden
  korrigiert. Die USB-Tastatur funktioniert im 5-GiB-QEMU-Gast ohne PS/2.
- Echte Win32-Konsolenprogramme samt Threads und Synchronisierung sowie
  Java 21 mit frisch kompiliertem Java-Programm bestehen weiterhin.

## Noch offen

Firefox erreicht ein Fenster, aber keine fertig dargestellte Testseite.
Mehrere konkrete ABI-Fehler wurden behoben; Kindprozessabbrüche bleiben
zu untersuchen. Der Browser-Test läuft offline mit deaktivierter Sandbox
und ist kein Nachweis für einen fertigen Alltagsbrowser.

WLAN-Verbindung und Firmwareladen, Radeon-Beschleunigung, USB-Maus und
Hotplug, vollständige Windows-/Linux-Kompatibilität und die Spieleziele
sind weiterhin offen. Die sudo/doas-Frontends ersetzen noch keine vollständige
sudoers- oder doas.conf-Implementierung. Allgemeines Copy-on-write bleibt offen.

## Abbilder und Nachweise

Das neue Live-Abbild wurde auf Westmere in QEMU erfolgreich gebootet und
liegt unter target/thos-live.img (601 MiB). Der vorherige Stand ist als
thos-live-pre-night-2026-10-14.img gesichert. Kein physischer USB-Stick
wurde beschrieben.

Auch der 128-MiB-GUI-Download besteht mit dem Releasekernel: vollständiger
Bytevergleich, Fensterverschiebung während des Downloads und anschließendes
Öffnen von Files. Der eingebettete Kernel wurde bytegenau geprüft; ein
Buildmanifest hält 248 Quellenfingerprints und die Abbild-Prüfsumme fest.

Ausführliche Messungen und der aktuelle Übergabestand stehen in
[worklog-2026-10-14-night.md](worklog-2026-10-14-night.md) und
[resume-2026-10-14.md](resume-2026-10-14.md). Screenshots und Gastlogs liegen
unter screenshots/2026-10-14 und logs/2026-10-14.

Die Arbeit stoppt spätestens bei 5 Prozent Rest im Fünf-Stunden-Kontingent.
Die automatische Fortsetzung ist für den gemeldeten nächsten Reset
kurz nach 01:14 Uhr deutscher Zeit eingerichtet und prüft vor weiterer
Arbeit erneut das tatsächliche Kontingent.
