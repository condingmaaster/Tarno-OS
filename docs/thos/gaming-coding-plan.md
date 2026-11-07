# THOS für Gaming und Coding

Nutzerauftrag vom 11. Oktober 2026: Gaming und Coding ergänzen die bereits
beauftragten Desktop-, Treiber-, WLAN- und Softwarearbeiten. THOS bleibt ein
eigenes Betriebssystem mit der bestehenden Oberfläche, Taskleiste, Animationen
und Effekten. Die allgemeine Gesamtprüfung bleibt separat vorgemerkt.

Ausdrückliche Nutzerkorrektur: **Vulkan ist optional**, keine allgemeine
Voraussetzung für THOS oder die erste Gaming-Etappe. Java erhält zunächst einen
OpenGL-Pfad; Bedrock die zur gewählten Windows-Version passende Direct3D-
Anbindung. Vulkan kann später für geeignete Hardware/Spiele ergänzt werden.

## Feste Referenzprogramme

| Ziel | THOS-Pfad | Nachweis |
|---|---|---|
| Minecraft Java Edition | Linux-Personality, echte Linux-JVM und native Bibliotheken | Anmeldung/Start, Welt rendern und spielen, Ton, Speichern, Neustart und Welt erneut laden |
| Minecraft Bedrock Edition für Windows | Windows-Personality, echte Windows-PC-Version | Installation/Laufzeit, Anmeldung, Welt rendern und spielen, Ton, Speichern und erneut laden |

Kein Ersatz durch Android-Bedrock, Minecraft-Server oder einen selbst gebauten
Minecraft-Klon. Ein Server kann einen JVM-Zwischentest liefern, erfüllt aber
nicht den Auftrag für das spielbare Java-Spiel. Launcher und Spiel sind
getrennte Testobjekte. Erfolg wird mit genauer Version und reproduzierbarer
Hardware-/Grafikkonfiguration dokumentiert.

## Reihenfolge mit Abnahmebedingungen

1. **Basis:** bisherige Fenster-/Performancekorrekturen sichern; Radeon-Treiber
   und WLAN weiterbringen. Tests am Acer zusätzlich zu QEMU. Kein Gaming-Modus,
   der die bestehenden Desktop-Effekte dauerhaft abschaltet.
2. **Internet und Coding:** DHCP/DNS/HTTPS/Downloads, Browser, Paketinstallation,
   Rechteverwaltung; Git, Compiler und JDKs anhand echter Projekte prüfen.
   Abnahme: Repository abrufen, ein kleines C-/Rust-/Java-Projekt bauen und
   ausführen. Rechte-/Paketfehler müssen sichtbar und korrigierbar sein.
3. **Grafik-/Spielegrundlage:** GPU-Speicher, Firmware, Command-Ring/Fences,
   beschleunigte Ausgabe und passende Userspace-Grafik-API; Vollbild,
   relative Maus, Tastatur, Audio, Fokus und Wechsel zurück zum Desktop.
   Vorhandenen HDA-/OSS-Code prüfen und mit der vom Spiel erwarteten Audio-
   Schnittstelle verbinden; vorhandener Code ersetzt keinen Spieltest.
4. **Minecraft Java zuerst:** JVM, JNI/LWJGL und deren native Linux-Abhängigkeiten,
   Fenster-/Grafikanbindung und Audio. Erst kleinere Bibliotheks-/Renderproben,
   dann das echte Spiel und gespeicherte Welt. FPS, Frametime-Ausreißer und
   Eingabeverhalten messen, einschließlich Rückkehr zum Desktop.
5. **Minecraft Bedrock danach:** aktuelle Windows-PC-Build prüfen, reale
   DLL-/API-/Grafik-/GDK-Abhängigkeiten protokollieren und gezielt ergänzen.
   Kein pauschales UWP-Konzept aus früheren Bedrock-Versionen übernehmen.
6. **Weitere Programme und Spiele:** die mit beiden Minecraft-Zielen geprüften
   Schnittstellen ausbauen. Keine allgemeine Kompatibilitätsgarantie allein
   aus zwei erfolgreichen Programmen ableiten.

Alle nutzbaren Funktionen gehören in die bestehende THOS-UI: Netzwerk in
Controlbar/Einstellungen, Downloads im Browser und Dateimanager, Programme und
Spiele in Softwareverwaltung/Startmenü, laufende Fenster in der Taskleiste.
Späterer Spieleinstieg und Entwicklungswerkzeuge verwenden die vorhandenen
Komponenten und zeigen installierte Version, Startfehler und tatsächlich
verfügbare Grafikfähigkeiten.

## Versions- und Hardwaregrenzen

Der Acer hat einen Core i3 M 370 und eine Radeon HD 5470/Cedar. Das erste
Java-Client-Ziel ist eine konkret festgelegte Minecraft-Version mit OpenGL-Pfad.
Vulkan-Builds sind eine optionale spätere Erweiterung. Die konkrete Spielversion
wird vor dem Installationstest mit dem Nutzer festgelegt, nicht stillschweigend
gegen eine ältere Version ausgetauscht.

Mojang hat 2026 aktualisierte Java-Anforderungen mit Vulkan-1.3-fähiger Grafik
veröffentlicht und arbeitet am Vulkan-Übergang. Das macht Vulkan nicht zur
Voraussetzung aller Minecraft-Versionen oder zum THOS-Pflichtziel. Anforderungen
und verfügbare OpenGL-Optionen werden pro Spielversion geprüft. Für Builds,
die Vulkan tatsächlich verlangen, kann THOS-Optimierung diese Hardwaregrenze
nicht beseitigen. Der Linux-RADV-Treiber unterstützt GCN/RDNA ab GCN1;
Cedar ist älter und damit kein Ziel dieses Vulkan-Treibers. Daraus folgt:
moderne DXVK-/Vulkan-Kompatibilität nicht als automatisch verfügbaren Acer-Pfad
planen. Geeignete OpenGL-/Direct3D-Alternativen sind versionsbezogen zu prüfen.

Bedrock Windows migrierte laut Microsoft ab 1.21.120 von UWP zu GDK. Deshalb
die gewählte echte Spielversion untersuchen, statt pauschal AppX/UWP oder nur
gewöhnliches EXE-Laden als vollständige Laufzeit anzunehmen. Anmeldung und
Produktberechtigung bleiben die regulären Minecraft-/Microsoft-Verfahren.

## Primärquellen (geprüft am 11. Oktober 2026)

- [Offizielle Java-/Bedrock-Plattformen](https://www.minecraft.net/en-us/store/minecraft-java-bedrock-edition-pc).
- [Java-Systemanforderungen 2026](https://www.minecraft.net/en-us/article/minecraft-java-edition-system-requirements).
- [Java 26.4 Snapshot 1: Vulkan als Standard](https://feedback.minecraft.net/hc/en-us/articles/49076034811533-Minecraft-Java-Edition-26-4-Snapshot-1).
- [Bedrock-GDK-Migration ab 1.21.120](https://learn.microsoft.com/en-us/minecraft/creator/documents/gdkpcprojectfolder?view=minecraft-bedrock-stable).
- [Mesa RADV: Hardware und Kernel-/Userspace-Aufgaben](https://docs.mesa3d.org/drivers/radv.html).
- [DXVK](https://github.com/doitsujin/dxvk) und [Proton](https://github.com/ValveSoftware/Proton)
  als Open-Source-Referenzen, noch keine in THOS nachgewiesene Integration.
