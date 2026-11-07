# THOS – Abendfortsetzung 15. Oktober 2026

Erneut ausdrücklich autorisierte Nachtarbeit, einschließlich 5-%-Stopp und
Fortsetzung nach tatsächlich gemeldetem Reset. Start primär0 %, Woche0 %,
Reset1792106464 =16.Oktober01:21:04 Berlin. Bestehende Einmalautomation
thos-um-00-10-fortsetzen reaktiviert auf01:22, COUNT1; keine Duplikate.
Nutzerpause hat Vorrang. Keine Subagenten, QEMU nacheinander, UI unverändert.

## Windows-Eingabe

Vorbereiteter echter MinGW-PE-Test auf vier CPUs BESTANDEN: Bewegung mit
gehaltener linker Taste, korrektes Loslassen, rechte und mittlere Taste,
Clientkoordinaten und MK-Flags, grünes Bild nach letzter Aktion sowie
Closebutton -> WM_CLOSE -> Exit0 -> Desktopfenster entfernt. Log/Screenshot
unter2026-10-15/evening-wininput-*. Separater Alt-F4-Retest gestartet.

## Geprüfter Live-Zwischenstand

Alt-F4, winc, Winlife20 und echter GUI-Close bestehen. Beim ersten Winlife-
Versuch fehlte lediglich /winlife-test im vorbereiteten QA-Root; ergänzt,
Dateisystem geprüft und Test bestanden. Quellen-/Gastfehler getrennt protokolliert.
Geprüfter Abendkandidat startet auf Westmere2048M automatisch bis Desktop.
Live-Abbild lokal auf diesen geprüften Stand aktualisiert; SHA256
68cc5e9ea834d364d7f46924b701b8fd16da883de0be064dffdb1bc290c68a85.
Vorheriger Stand target/thos-live-pre-evening-2026-10-15.img erhalten.
Manifest target/thos-live-2026-10-15-evening-build-manifest.json beschreibt
exakt diesen älteren Quell-Snapshot. Kein physischer Datenträger beschrieben.

## Win32-Größenänderung in Arbeit

Styles erlauben jetzt resizable Clientfenster; WM_SIZE und GetClientRect,
fallible Section-Allokation und generationengeprüfte Compositor-Mappings.
Storewechsel werden pro Fenster auf eine laufende Anfrage begrenzt; während-
dessen bleibt die neueste gewünschte Größe erhalten. Kanten, Max/Restore/F11
nutzen bestehende UI, keine Stiländerung. Noch nicht im Live-Zwischenstand.
Releasebuild und Host-Compile bestehen. Erster Gasttest scheiterte an falsch
angenommenem Ursprung100,80: das zusätzliche Panel verschiebt die bestehende
Kaskade auf180,140. Tatsächliche Geometrie wird nun geloggt/gelesen, Retest läuft.
Offizielle Referenz: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size
und https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect.
WM_WINDOWPOSCHANGED/SetWindowPos/MinMax-Callbacks sind noch nicht vollständig
implementiert; keine allgemeine Win32-Konformität behaupten.

## Win32-Resize-Gastprüfung bestanden

Echter MinGW-PE-Client nutzt GetClientRect und prüft WM_SIZE. Ziehen der
Fensterecke auf480x300, Maximieren auf1278x697 oberhalb der Controlbar,
F11 auf1280x800, Zurück/Wiederherstellen und Alt-F4/Exit0 bestehen. Jede
Phase zeichnet bis in die tatsächliche neue Bildpufferecke; Pixelvergleich
und vier PNGs bestehen. Keine bloße Skalierung des alten Inhalts.
Zusätzliche40-Wechsel-Speicherprüfung mit echtem PE/GDI läuft nun einzeln;
prüft alte gültige Ansichten, Zurückweisung veralteter Generationen und
Freigabe ersetzter Frames. Noch kein Ergebnis dafür behaupten.

Keine Styleänderung, WS_THICKFRAME ist Voraussetzung für manuelles Resize.
Windows SetWindowPos/WM_WINDOWPOSCHANGED/MinMax-Capture weiter nicht vollständig.
Geprüftes Live-Abbild enthält weiterhin nur vorigen Input-/Alt-F4-Zwischenstand,
noch nicht den neuen Resize-Fix. Release-ELF und QA-Abbild sind neuer.

## Titel und Regressionen

Set/GetWindowTextA und GetWindowTextLengthA jetzt mit echten WM_SETTEXT/
WM_GETTEXT/WM_GETTEXTLENGTH-Callbacks im Besitzerprozess; ANSI/Latin1-Roundtrip,
NUL-Abschluss und kurze Puffer im PE geprüft. CreateWindowExA übernimmt den
Titel, Änderungen aktualisieren Titelleiste und Controlbar. Win32-Resize-/
Titeltest und40-Wechsel-Speichertest bestehen; zusätzliche160KiB Metadaten
bleiben pro40Wechsel gemessen. Bildpuffer werden freigegeben; VMM-Seitentabellen
bleiben derzeit prozessgebunden (bump allocation, keine umfassende VA-Reuse-
Behauptung). Native Resize-Regression aller sechs Apps, Winlife20 und
Mauseingabe/Alt-F4 bestehen auch mit neuem Resize-/Titel-Kernel.

## Expliziter Programmstarter und X_OK

Neue /thosrun prüft native64Bit-PE-/ELF-Header, aktiviert bei explizitem Start
das Ausführungsrecht und nutzt execv ohne Shell-Fallback. Files verwendet ihn
fürEXE und nicht ausführbare ELF-Dateien; bestehende Shell-/Text-/Paketpfade
bleiben erhalten. Hostprüfung: echterELF mit wörtlichem Apostroph-/Dollar-
Pfad startet, ungültigeEXE sowie32Bit-PE undDLL werden ohne chmod abgelehnt.

Die ursprüngliche Vermutung, dass THOS jedenPE-Start ohneX-Bit blockiert,
war zu pauschal: der vorhandenePE-Pfad startet solche Dateien bereits.
Konkreter Gastbefund: access(X_OK) ignorierteX-Bits, daher aktivierte der
Starter kein Ausführungsbit, obwohlPE/ELF bereits liefen. sys_access nun
mit echtem Execute/Search-Check, Besitzer-/Gruppenvorrang, Symlinkauflösung,
Geräte-/Fehlend-Unterscheidung und ungültigen Modi. Nicht als umfassende
Exec-DAC-Härtung oder allgemeine POSIX-Konformität verkaufen.

ErsteAccess-Fixture erwartete mkdir0600; Kernel ignoriert noch den mkdir-
Modus/umask. Das ist separat offen. Zugriffsfixture setzt den Verzeichnismodus
nun explizit perchmod und prüft nuraccess. Wiederholung läuft danach mit
realemRipgrep-PE/LinusELF und Files-UI auf neueLaunch-QA. Siehe Resume.

## Expliziter Start, echte CRT-Argumente und Firefox-Downloadversuch

Readonly-Ext2Stream führt jetzt den Dateipfad mit: fchmod/fchown konnten vorher
bei großen lesend geöffneten Dateien Erfolg melden, ohne Metadaten zu ändern.
Access-/Stream-Gasttest besteht. Echte Ripgrep14.1.1-PE und Busybox-ELF aus
Downloads ohneX-Bit starten über /thosrun mit Argumenten; kaputtesMZ bleibt
unverändert, kein Shell-Fallback. Regression nach Argumentparser ebenfalls PASS
(/tmp/evening-thosrun-winargs-reg-2026-10-15.log).

Files-Test mit `Browser's $(touch unintended).EXE` fand einen wirklichen Fehler:
MSVCRT zerlegte Dateinamen an Leerzeichen. Neuer winargs-Parser/-Encoder nach
Microsoft-CRT-Regeln bewahrt Leerargumente, Anführungszeichen, Backslashes,
Tabs und wörtliche Shellzeichen. Bei32Argumenten jetzt33Zeigerslots inkl.NULL,
vorher überschriebenes erstesArgument. Echte MinGW-CRT-Tests und Files-Enter
bestehen; PNG evening-files-opens-downloaded-exe.png. Bestehende Grenze255Bytes
Windows-Kommandozeile wird explizit mitE2BIG abgewiesen, keine stilleKürzung.
Keine Behauptung vollständiger Windows-Argument-/Exec-/DAC-Kompatibilität.

Neuer Firefox-Downloadtest nutzt lokalenHTTP-Server, isoliertesDiagnoseprofil,
Downloadlinks fürPE/ELF, Bytevergleich und Files-UI; Außenverkehr perQEMUrestrict
gesperrt. Erster2GiB-Gast scheitert schon beim Tabprozess: NULL-Schreibzugriff
bei0x70067db43afa, anschließend fork/Mapping-Speichermangel. KEIN bestandener
Browserdownload. Test wegen Fehler nach Log/Screenshot beendet; Exit134 ist
Harness-Assertion nach bewusstemQEMUquit. Screenshot evening-firefox-download-
memory-failure.png, Log /tmp/evening-firefox-downloads-2026-10-15.log. Doppelte
Panelinstanz im neuenHarness entfernt (thosdesk-s startet Panel bereits).
Ursache vorerst offen, größerer Gast als gezielter Gegenversuch vorgesehen.
Firefox bleibt Sandbox-disabled-Diagnose, nicht im normalenLive-Abbild.

## Neue Nutzerpriorität: ASUS Aura/Armoury, Tastatur unverändert

Nutzer priorisiert AuraCreator/ROG-Tools für StrixScopeII als Kompatibilitätstest;
leichte Windows-Spiele sekundär. Explizites Verbot: NICHTS an Tastatur ändern.
Kein Passthrough, keine HID-Ausgabe, keine RGB-/Profil-/Firmware-/Hostinstallation.
Reine lsusb-Inventur zeigte ASUS ROG OMNI RECEIVER0b05:1ace, belegt genaue
Tastaturvariante noch nicht. ScopeII,II RX undII96Wireless separat behandeln.

Offizielle ASUS-Seiten nennen ScopeII in AuraCreator-Kompatibilitätsliste und
Windows10>=1903/11 als Armoury-Voraussetzung. Supportpaket führt UWP sowie
Armoury/Aura/ROGLive/ASUSFramework-Dienste; kein einfacher standaloneWin32-Editor.
Quellen: https://www.asus.com/global/support/faq/1042484/
https://rog.asus.com/us/support/faq/1041654/
https://www.asus.com/supportonly/armoury%20crate/helpdesk_download/
https://www.asus.com/uk/supportonly/rog%20strix%20scope%20ii%2096%20wireless/helpdesk_download/

Offizieller kleinerInstaller3.3.6.0, ZIP2304321Bytes (tatsächliche Größe siehe
Provenienz), SHA256a0c3181b135c0439f12338d6d6b77181b54a62bc44c1fe95c57945f1735d54e1
passt ASUS-Hash. Keine400MBGear-/5GBFull-Downloads überHotspot.
PE32+x64 mitImports KERNEL32/USER32/ADVAPI32/SHELL32/OLEAUT32/SHLWAPI;
u.a.ServiceManager,Registry,ShellExecuteExW. StatischeAnalyse imLog
 evening-asus-installer-pe.txt, Provenienz evening-asus-provenance.json.
GetrenntesQA-Abbild thos-asus-probe-2026-10-15.img. NeuerDiagnosefall
asus-installer-probe läuft offline ohnephysischesUSB; Ergebnis noch abzuwarten.

## ASUS-Probe und erster behobener Kompatibilitätsblocker

Der originale ASUS-Installer scheitert tatsächlich an FindFirstFileExA (Status126).
Die API wurde in kernel32x ergänzt; die bisherige Unicode-Version ignorierte
Suchoptionen. Gemeinsamer Suchzustand hält nun Case-Sensitive- und Verzeichnisfilter
über FindNext hinweg. Ungültige Infolevel/Flags/Filter liefern ERROR_INVALID_PARAMETER,
Gerätesuche ERROR_NOT_SUPPORTED. Pfadlängen werden vor den festen Puffern geprüft.
Large-Fetch bleibt ein optionaler Performancehinweis; Ext2 hat keinen
Virtualisierungsfilter für ON_DISK_ENTRIES_ONLY. Bestehende Unicode-/DOS-/Wildcard-
Grenzen sind damit nicht generell gelöst. Keine erfundenen Erfolgs-Stubs.

Freestanding-DLL und echter MinGW-Test gebaut. Winc inklusive neuer ANSI/Unicode-
Suchprüfungen, Filterfortsetzung und Fehlercodes besteht vollständig. Erneuter
offline ASUS-Start erreicht den nächsten fehlenden Import SetDefaultDllDirectories.
Statischer gewöhnlicher Importabgleich listet weitere KERNEL32-Exporte sowie
fehlende Module ADVAPI32, SHELL32, OLEAUT32 und SHLWAPI. Vollständige ASUS-Laufzeit
und Gerätesteuerung weiter offen. Details in asus-compatibility-2026-10-15.md.

SkiFree 1.04 vom Autor (39.409-Byte-ZIP) als nachrangiger leichter Windows-Test:
echte i386/PE32-Datei wird durch thosrun vor Ausführung abgelehnt. Kein Spiel läuft
nachgewiesen. Erste Fixture versuchte unnötig chmod auf root-eigener Datei;
Formatprüfung selbst korrekt. Fixture wurde auf benutzereigene Download-Kopie
umgestellt; saubere Wiederholung noch ausstehend.

Bisheriger neuer Nachtkandidat (vor FindEx-DLL) startet auf Westmere/2048M sauber
mit Desktop, Panel, Terminal. Neuer Kandidat thos-live-candidate-asus-2026-10-15.img
mit identischem neuesten Kernel, Resize-/Titel-CUI, Files/thosrun und geprüfter
FindEx-DLL gepackt. Root e2fsck besteht. SHA256
5f6096fdac54081bcbea65a8e5f4c310f4ec265ddcc88ed41d4d37ebef75cd44.
Noch kein Austausch von thos-live.img: abschließende Prüfung des konkreten
neuen Pakets steht aus. Kein physischer Datenträger beschrieben.

Firefox-Gegenversuch mit4096M/SMP4 läuft allein, isoliertesHTTP und aktualisiertes
Script ohne doppelte Panelinstanz. Log /tmp/evening-firefox-downloads-4g-2026-10-15.log,
PID-Datei gleichen Namens mit.pid, Abschluss.exit. Keine weitere QEMU parallel!
Automationsprompt übernimmt inzwischen ausdrücklich ASUS-Priorität und Verbot
jeder Änderung an echter Tastatur/Receiver einschließlich USB-Passthrough.

## Firefox-HTTP-Seite bewiesen, Download weiterhin gescheitert

Der4GiB-Gegenversuch zeigte schließlich die echte lokaleHTTP-Testseite mit
beiden Downloadlinks; PNG evening-firefox-local-download-links.png wurde
visuell geprüft. Das ist weiter als der erste2GiB-Versuch. Nach dem automatischen
Klick auf Windows download treten erneut Benutzer-Pagefaults auf (PID547:
0x700513c37ec3, Ziel0x700539a4b620; PID150:NULL bei0x70052a943afa).
Kein Bytevergleich/Files-Start erreicht. Nach dokumentiertem Fehler gezielt
QEMUquit, HarnessASSERT/Exit134 erwartbar; nicht als Kernelpanic darstellen.
Offline aus dem beendeten Gast extrahierte Root zeigt /tmp/Downloads leer.
Logs evening-firefox-download-4g-failure*, Dateiliste und Screenshots gesichert.
Mehr RAM allein löst das Problem nicht. Es gibt noch keinen bestandenen
Firefox-Download-Ende-zu-Ende-Test.

Drei lesende CPU-Registerproben während der Verzögerung zeigen mehrfach
Kernel-LockedHeap::alloc/dealloc auf fast allenCPUs; ein Sample zeigt auch
Benutzerfortschritt. Das ist ein Hinweis auf Allocator-Konkurrenz, kein Beweis
für Deadlock oder alleinige Fehlerursache. Proben und Zuordnung mit aktuellem
Kernel-ELF gesichert. Crashreporter scheitert zusätzlich an nicht implementiertem
flock(73), Glean ENOSYS; das erklärt nicht automatisch den ursprünglichenCrash.
Keine vorschnelle Kerneländerung oder bloßesDeaktivieren von Sicherheitschecks.

## Abschließend geprüftes Live-Paket lokal übernommen

Finales Paket5f6096fd... startet auf Westmere/2048M offline automatisch mit
Compositor, Controlbar und Terminal. Screenshot evening-final-live-desktop.png
visuell geprüft. Erster zusätzlicher Harness wartete unpassend auf serielle
Shellausgabe, während Desktop die virtuelle Tastatur besitzt; Abschlusskriterium
auf tatsächlichen Panel-Task und vorhandenen Abschluss-Screenshot korrigiert.
Saubere Wiederholung PASS. Kernel/RAMRoot aus FAT sowie Desktop/Files/thosrun/
FindEx-DLL/w32test ausRoot stimmen beim SHA256-Rücklesen mit Build-Artefakten
überein. Verifikation evening-final-live-pack-verification.json.

Lokal target/thos-live.img atomar auf geprüften Kandidaten übernommen.
SHA2565f6096fdac54081bcbea65a8e5f4c310f4ec265ddcc88ed41d4d37ebef75cd44,
Manifest target/thos-live-2026-10-15-night-build-manifest.json.
Backup target/thos-live-pre-night-2026-10-15.img behält68cc5e9e...;
weitere Backups bleiben erhalten. Keine physische Datenträgeraktualisierung.

SkiFree-Probe mit eigener Download-Kopie sauber wiederholt: explizite32Bit-
Ablehnung, Status1, keine Execute-Rechteänderung, keine Setupfehlermeldung.
Logs dauerhaft gesichert. Kein Spielstart behaupten.

Zusätzliche kleine Diagnose winlookup-probe vorbereitet: statischer Import
FindFirstFileExA versus GetProcAddress auf kernel32.dll und Erweiterungs-DLL.
Quellprüfung zeigt, dass der dynamische Exportpfad die synthetische Exporttabelle
liest und nicht denselben Erweiterungs-Fallback wie der statische Loader hat.
Echter Gastvergleich läuft noch. Keine ungeprüfte Kerneländerung dafür begonnen.


## Konkrete nächste Windows-Korrektur reproduziert

winlookup-probe ist durchgelaufen: statischer FindEx-Import funktioniert,
GetLastError über GetProcAddress funktioniert, FindFirstFileExA/W über kernel32
liefern aberNULL, Fehler127. Direkte Abfrage der kernel32x-DLL findet den Export.
Das bestätigt den fehlenden dynamischen Erweiterungs-Fallback; keine bloße
Vermutung und kein API-Erfolg. Logs dauerhaft gespeichert. Nächste Arbeit:
GetProcAddress und LdrGetProcedureAddress konsistent mit dem statischen Loader
machen, rekursive/Ordinal-Fälle begrenzen und im echten PE prüfen. Aktuell dafür
noch keine Kerneländerung begonnen; geprüfter Live-Kernel bleibt unverändert.

git diff --check besteht. Alle QEMU-Proben nacheinander durchgeführt, keine
Subagenten, kein Tastatur-/Receiver-Passthrough und keine physische Aktualisierung.

Abschlussreserve: letzte Abfrage94%/Woche15%, tatsächlicherReset1792106464.
Keine neue Implementierung nahe95%-Grenze begonnen. Alle QEMU beendet.
Bestehende einmalige Automation nach tatsächlichemReset erneut bestätigt:
16.Oktober01:22 Berlin, ACTIVE/COUNT1, keine Duplikate. NächsterFix und
Tastaturverbot im Prompt und bereinigten Resume gesichert.

Letzte Abschlussabfrage:95% verbraucht,5% Rest, Woche15%. STOPP eingehalten.
Quota-JSON gesichert; nur diese abschließende Statusaktualisierung durchgeführt.
