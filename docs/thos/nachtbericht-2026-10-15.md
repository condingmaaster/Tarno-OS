# THOS – Nachtbericht vom 15. Oktober 2026

Layout, Controlbar, Animationen und Effekte sind erhalten.

- Firefox rendert nun echte lokale HTML-Inhalte: fehlende Wayland-Unterflächen
  implementiert und zehn Kompositionsphasen geprüft. Der Firefox-Test lief
  offline mit deaktivierter Sandbox; reguläres sicheres Browsen ist noch offen.
- USB-Maus: mehrere EHCI-Controller, Anstecken/Abziehen während des Ziehens,
  korrekte Tastenfreigabe und Mausrad im Startmenü in QEMU geprüft.
- Lange Kernelarbeit blockiert Zeitmessung weniger: 300-ms-Sleep kam im Lasttest
  nach 310 ms zurück. Große Zufallsdaten werden in begrenzten Teilmengen geliefert.
- Win32-Fenster geben beim Beenden Ressourcen frei. 20 echte Fenster hinterließen
  nur 8 KiB zusätzliche Verwaltung. Der Schließen-Knopf erreicht nun zuverlässig
  das Programm; echter GUI-Test und Konsolenregression bestanden.

Logs und Screenshots liegen unter logs/2026-10-15 und screenshots/2026-10-15.
Der ausführliche Bericht steht in worklog-2026-10-15.md.

Das neue Image liegt als Kandidat bereit. Die abschließende Downloads-Prüfung
wurde wegen des Nutzungskontingents beendet; Java und Live-Autostart müssen
noch folgen. Der bisher geprüfte Live-Stand bleibt erhalten. Kein Datenträger
wurde aktualisiert. Acer-WLAN, Radeon-Beschleunigung und vollständige Programm-
und Spielekompatibilität bleiben offen.

Letzte Kontingentabfrage: 98 % verbraucht, Arbeit gestoppt. Dieselbe einmalige
Fortsetzung ist für 06:16 Uhr Berlin nach dem gemeldeten Reset vorgesehen.

## Ergänzung am Morgen

Die offenen Abschlussprüfungen sind nun bestanden:128MiB-Download mit
Dateivergleich und Fensterverschieben, OpenJDK21 samt javac sowie automatischer
Desktopstart auf Westmere über SATA und emulierten USB-Stick. Beim USB-Test
brauchte das Laden des großen RAM-Abbilds mehr als90Sekunden; längere Frist
bestand. Kandidat und Prüfmanifest gesichert, bisheriger Live-Stand erhalten.

Ein zusätzlicher Win32-Mauseingabe-/Alt-F4-Fix ist gebaut und für QEMU
vorbereitet, aber noch nicht im Gast geprüft oder im Kandidaten enthalten.
Auf deinen Wunsch jetzt Pause; automatische Fortsetzung ausgeschaltet.
