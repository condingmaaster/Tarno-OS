# THOS – Nachtbericht, 17. Oktober 2026

Windows-Grafik und der Unicode-Programmstart sind deutlich weiter. THOS-Layout, Controlbar, Animationen und Effekte bleiben erhalten; die Desktop-Binärdatei wurde nicht verändert. Der physische Boot-Stick und die echte Tastatur wurden nicht verändert. Keine ASUS-Software wurde auf dem Host installiert.

## Erreicht und geprüft

- Echte farbige Bitmaps, Memory-DCs und überlappungssichere BitBlt-Kopien. Pixel liegen in verwalteten Speicherseiten, nicht als große Puffer im 96-MiB-Kernel-Heap. Im Verlauf bestanden 40 aufeinanderfolgende Programme mit je 64 MiB nicht selbst freigegebenem Bitmap-Speicher in einer 2-GiB-VM.
- Zeichenfarben, FillRect, transparente Pinsel, echte Stiftobjekte und korrekte Rechteckränder. Auswahl, Typen, Besitzer, Löschung und Clipping werden geprüft. Wide-/Dashed-Pens bleiben ausdrücklich ununterstützt.
- Echte konstante AlphaBlend-Farbüberblendung über MSIMG32/GDI32; Skalierung und Transparenz pro Pixel werden abgewiesen.
- Kantengeglättete Windows-Schriften aus denselben Lato-Dateien wie die THOS-Oberfläche. 38 echte Prüfungen bestehen auf KVM und Westmere. Jeweils 136.654 tatsächliche Screenshotpixel stimmen vollständig mit einer unabhängigen FreeType-Referenz aus den ursprünglichen TTF-Dateien überein. Die begrenzten Rastergrößen und Glyphen ersetzen keinen allgemeinen Font-Installer.
- Unicode-CRT-Start mit echten __wgetmainargs-, _wcmdln- und __winitenv-Ausgängen. Acht gleichzeitig aufrufende Threads erhalten denselben vollständig aufgebauten Cache. Leere Argumente, Leerzeichen, Anführungszeichen, Backslashes, Umlaute und Emoji bleiben als Daten erhalten. Ein normal mit MinGW -municode gebautes wmain-Programm startet korrekt. Zusätzlich bestehen 1.203 unabhängige Parserfälle und die bestehenden Modul-/Export-/CRT-Tests.
- CRT-Fehler sind für die ausführbare Datei sichtbar; Windows-ENOSYS wurde korrekt von Linux-ENOSYS unterschieden. Codepage-Leadbytes werden anhand validierter Metadaten geprüft; die UTF-/Codepage-Probe besteht 39 Prüfungen.

QEMU lief ausschließlich seriell, regelmäßig auch mit emulierter Westmere-CPU. Geprüft wurden außerdem Fenstergrößen, Maximieren, F11, Pixelecken, Fokus, Controlbar und Schließen. Der exakt gepackte letzte Live-Stand startet unter Westmere mit Desktop, Controlbar und Terminal. Alle Test-VMs sind beendet. Logs und Screenshots liegen unter docs/thos/logs/2026-10-17 und docs/thos/screenshots/2026-10-17; u.a. font-first.png, pen-first.png und wide-live-westmere.png.

## Aktueller lokaler Stand

`target/thos-live.img`: **3a3a468d715a326947c7d2a5049fd15d95ba9923ed4ec20a8504e3c86c617d51**.
Manifest: `target/thos-live-2026-10-17-wide-build-manifest.json`.
Kernel und vollständiger Root wurden aus dem gepackten Abbild zurückgelesen und verglichen; die beiden neuen Laufzeit-DLLs wurden ebenfalls aus ext2 zurückgelesen. Alle Vorgänger und Quellarchive bleiben erhalten. Speicherplatz wurde ausschließlich durch nachweislich byteidentisches Sparsifizieren eigener generierter Testimages gewonnen.

## Offen

Der echte x64-Snake kommt jetzt bis `USER32!AdjustWindowRect`, Status 126 vor Programmeinstieg. **Noch kein Gameplay.** ASUS bleibt an `SetDefaultDllDirectories` und weiteren Windows-Abhängigkeiten blockiert: **keine Installation oder Hardwaresteuerung**. Echte WLAN-/GPU-Firmwareuploads und Hardwarebeschleunigung bleiben offen. Firefox/JDK/GTK liegen weiterhin nur in gesonderten QA-Abbildern; diese Nacht hat sie nicht in das Live integriert.

Normale PE-Starts besitzen weiterhin einen leeren PEB-Umgebungsblock. Der CRT-Test beweist die Verarbeitung eines kontrolliert gesetzten PEB-Blocks, keine Vererbung der Linux-Umgebung. Legacy-Codepages, einige native Fallbacks und weitere Windows-APIs bleiben unvollständig. Es wurde kein pauschaler FPS- oder Gesamt-RAM-Faktor gemessen. Die allgemeine Gesamtprüfung bleibt vorgemerkt.

Bei tatsächlich **95 % Verbrauch / 5 % Rest** wurde der Entwicklungsstopp ausgelöst; danach nur Abschlussdokumentation. Die bestehende Automation ist einmalig für **17. Oktober, 08:05 Uhr Europe/Berlin** aktiviert, nach gemeldetem Reset um 08:04:06. Vor Arbeitsbeginn wird die echte Quote erneut geprüft. Eine neue Pause-/Stoppanweisung hat Vorrang.
