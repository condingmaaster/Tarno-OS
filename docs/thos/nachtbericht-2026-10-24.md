# THOS – Nachtbericht 24. Oktober 2026

Das neue lokale Live-Abbild ist geprüft und übernommen: 9c4ed07d1350586d7e013c011c96c718ec6259c7b976a6b9135e44866c82adcf. Die bisherige Oberfläche, Controlbar, Animationen, Schatten und Effekte bleiben erhalten. Das exakte Paket startet offline auch mit Westmere-CPU; Screenshot: screenshots/2026-10-24/live-nls-westmere.png. Der vorherige geprüfte Stand bleibt als Rückfall erhalten. Kein USB-Stick und keine echte Tastatur wurden verändert.

Windows-DLLs können jetzt tatsächlich zur Laufzeit geladen, mit bereits gepinnten Systemfunktionen verbunden, aufgerufen und wieder entladen werden. Vollständige Pfade, Unicode/ACP1252, stabile Modulmetadaten, Referenzen und erste Lade-Reihenfolge sind geprüft. 62 lange importierte DLLs, 200 Lade-/Entladezyklen, parallele Referenzen und gezielt gestörte Ladeversuche bestehen. Suchrichtlinien wählen nachweisbar die richtige Dateiversion aus Programmverzeichnis/System32; Einstellungen bleiben prozessgebunden.

Fehlerhafte PE-Dateien werden nicht mehr als Shellskripte interpretiert. Zu große materialisierte Images scheitern kontrolliert an der Allokation. Deutsche/englische Locale-Metadaten und echte, auch verschachtelte/parallele Callbacks ersetzen leere Erfolgsausgaben. Vollständig installierte Windows-Sprachdienste werden ausdrücklich nicht vorgetäuscht.

ASUS kommt über die bisherigen Importlücken hinaus und scheitert jetzt an GetTimeFormatW. Der nächste Formatkern ist nur hostgeprüft vorbereitet, noch nicht eingebunden. Snake scheitert weiter an BeginPaint. Keine erfolgreiche ASUS-Installation, Hardwaresteuerung oder Spielbarkeit behauptet. Dynamisches DllMain/TLS, neue DLL-Abhängigkeitsgraphen, Ressourcen-Lademodi und vollständige NLS-/Zeitdienste fehlen weiter.

Neun entbehrliche eigene Zwischenabbilder entfernt: 2.24 GiB freigegeben, etwa 201.94 GiB frei. Quellen, kleine Beweise und gezielte Rückfallstände bleiben erhalten. Alle Details/Hashes im Arbeitsbericht und target/thos-live-2026-10-24-nls-build-manifest.json.

Die Nachtschicht bleibt bis zu deiner manuellen Pause autorisiert. Quotenstopp und nächster tatsächlicher Reset werden im Resume abschließend festgehalten.

Bei tatsächlich5 % Rest geordnet gestoppt. Dieselbe automatische Fortsetzung ist einmalig für07:48 Uhr geplant, nach tatsächlichem Reset07:47:19. Wochenverbrauch30 %. Die Nachtschicht bleibt bis zu deiner manuellen Pause autorisiert.
