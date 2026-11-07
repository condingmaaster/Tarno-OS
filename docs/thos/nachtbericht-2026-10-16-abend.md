# THOS – Abendfortsetzung am 16. Oktober 2026

Mehrere konkrete Windows-, Eingabe- und Grafikfehler sind behoben und im lokalen Live-Abbild übernommen. Layout, Controlbar, Animationen und Effekte bleiben erhalten.

- Programme erhalten echte Exe-/DLL-Pfade. Gleichzeitige Starts teilen keine globale Kommandozeile mehr;64PE-Starts aus8Arbeitern stimmen mit unabhängigen Pipe-Kennungen überein.
- UTF8/UTF16 funktionieren mit Umlauten/Emoji, Größenabfragen und Fehlern. GetCPInfo meldet die richtige UTF8-Puffergröße.33echte PE-Prüfungen bestehen auch auf emulierter Westmere-CPU.
- Wayland übermittelt gehaltene Tasten und Modifier in richtiger Reihenfolge. Windows-Fenster bekommen echte Tasten-Press-/Release- und Fokusmeldungen; Pfeiltasten, Ctrl/Shift und Enter sind geprüft.
- Ungültige GDI-Kontexte können nicht mehr versehentlich Desktop oder andere Fenster verändern. Der Zeichenfarbenzustand gehört zur Fensterlebensdauer; eine nie bereinigte globale Tabelle entfällt.29echte Prüfungen bestehen auf KVM und Westmere.

Die QEMU-Läufe erfolgten ausschließlich nacheinander. Geprüft wurden außerdem echte Windows-Fenstergrößen/WM_SIZE/Pixelecken, Maximieren/F11/Wiederherstellen/AltF4, Fokuswechsel mit Controlbar/Minimieren/nativem Terminal, Taskleistenklicks und19Exportauflösungen. Im Verlauf bestanden auch GTK, native Apps und isolierte Firefox-Downloads mit vollständigem Bytevergleich und ausdrücklichem Öffnen über Files/Terminal/Controlbar. Firefox läuft dabei weiterhin nur im lokalen sandbox-disabledHTTP-Testprofil und ist nicht imLive enthalten.

Das endgültige gepackte Live wurde unter Westmere mit2GiB/SMP4/offline gestartet. Kernel und vollständige RAM-root wurden rückgelesen und verglichen; [Desktop](screenshots/2026-10-16/gdi-live-desktop.png) und [echte GDI-Prüfung](screenshots/2026-10-16/gdi-handles-checked.png) sind gesichert und visuell geprüft. Aktueller Stand: target/thos-live.img, SHA256 **86279c94974681ba9a8ffeb5284b5a739b4afe63fd0af5330ce4a84566c3972f**, Manifest target/thos-live-2026-10-16-gdi-handles-build-manifest.json. Vorgänger und sämtliche lokalen Änderungen bleiben erhalten.

ASUS/Aura ist weiterhin an fehlenden Windows-Laufzeitfunktionen blockiert, zuerst SetDefaultDllDirectories. Ein kleiner echter Windows-x64-Snake-Quellbuild wurde offline geprüft und scheitert an BitBlt; keine Spielbarkeit behauptet. Die [MIT-Quellen des Autors](https://github.com/Zcytxcbyz/Snake/tree/b28f387a8f16442bd12078c7ce1ed8a21cc501da), Buildherkunft und Lizenz sind dokumentiert. Eine Kopier-/Clippinggrundlage für das weitere GDI-Backend besteht4Hosttests einschließlich10.000Modellvergleichen, ist aber noch kein eingebundenes BitBlt.

Hardwarebeschleunigung und echte WLAN-/GPU-Firmwareuploads bleiben offen. Der physische USB-Stick und die echte Tastatur wurden nicht verändert. Die allgemeine Gesamtprüfung bleibt vorgemerkt. Es wurde kein pauschaler FPS- oder Gesamt-RAM-Faktor gemessen.

Alle Test-VMs sind beendet. Bei tatsächlich95% Verbrauch/5%Rest gestoppt; Abschlussstand und Live-Hash gesichert. Automatische einmalige Fortsetzung:17.Oktober2026 um03:04 Europe/Berlin, nach gemeldetem Reset03:03:29; vor Beginn erneut echte Quote prüfen, bei höchstens5%Rest stoppen. Details und Fehler-/Zwischenläufe in worklog-2026-10-16.md sowie logs/2026-10-16.
