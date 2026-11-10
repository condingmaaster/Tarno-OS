# Windows – nächste echte Laufzeitlücken

Maßgeblich sind resume-2026-10-25.md und der neueste Arbeitsbericht samt Manifesten. Die alten GetTimeFormatW-/WTS-/Thread-/Ressourcen-/INI-Blocker sind bereits bearbeitet; keine alten Anweisungen wiederholen. Geprüftes Live bislang57b00eeb… (Profile), neueste private CPU-Zeit-QA kernel4c83508d… qualifiziert; exaktes Live-Paket623d3c1d… noch im Offline-Starttest. Nach erfolgreicher Übernahme aktuellen Resume beachten.

## ASUS

Die tatsächliche isolierte Installerprobe kommt über GetThreadTimes und scheitert an „PE: DLL not found in System32“, Status126. Die statische Importreihenfolge und die tatsächliche System32-Inventur identifizieren ADVAPI32 als erste fehlende Bibliothek; SHELL32, OLEAUT32 und SHLWAPI fehlen ebenfalls. Der Lader benennt in dieser Meldung noch nicht selbst die konkrete DLL. Keine Installation und keine Hardwaresteuerung behaupten.

ADVAPI32-Imports umfassen echte Registryzugriffe, Serviceverwaltung, SIDs/ACLs und CreateProcessAsUserW. Keine Erfolgssicherheits-/Service-Stubs ergänzen. Vor einer öffentlichen Registrybridge muss der vorhandene NT-Unterbau gehärtet werden: NtSetValueKey liest ULONG DataSize noch als volle64Bit, Namen werden aufASCII/260Zeichen verkürzt, mehrere Ausgabezeiger/Handleoberbits/OBJECT_ATTRIBUTES-Details sind ungeprüft. Echte Unicode-/Puffer-/Fehler-PE-Tests einschließlich alter Gegenprobe verwenden. Registryhives existieren bereits, aber persist() verschluckt Schreibfehler und mutiert zuerstRAM; daher derzeit keine zuverlässige dauerhafte Windows-Registry versprechen. Rechte entsprechen nur begrenztem UID-Modell, keinen vollständigen WindowsACLs. NtDeleteValueKey fehlt. Native Registrykorrektur allein stellt noch kein ADVAPI32 und keine ASUS-Kompatibilität bereit.

Verträge: [ZwCreateKey](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatekey), [Registry-Objektattribute](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/opening-a-handle-to-a-registry-key-object), [RegQueryValueExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw). ULONG-Argumente verwenden ausdrücklich32Bit, Pufferlängen sind Bytezahlen. Fehlende Fähigkeiten ehrlich als Fehler behandeln.

## Zeitmessung und übrige Grenzen

GetThreadTimes verwendet echte instrumentierte laufende User-/Kernelintervalle, keine Wallclock alsCPU-Ersatz und keinen Samplingdaemon. Warten und nicht eingeplante Zeit sind ausgeschlossen; Erzeugung/Ende stammen separat von echter UTC. Kleine Records überleben nur solange echte Handles sie halten; Kernelstacks bleiben nicht daran hängen. Nur PE-Threads werden gemessen, normalerLinuxdesktop bekommt keinen Record. Kein zyklusgenauer Profiler oder Anspruch perfekter Abdeckung sämtlicher Interruptsonderpfade. Finale Messwerte und deren erhebliche Hoststreuung im Worklog.

Snake scheitert weiterhin an USER32!BeginPaint. Erst echte Update-Region, Paint-DC, Clipping und Lebensdauer umsetzen; keine Spielbarkeit behaupten. Begrenzter DLL-Lader ist geprüft, dynamischesDllMain/TLS/neueAbhängigkeiten und vollständigeWindows-Such-/Ressourcenmodi fehlen. MengenbegrenzterExportparser hat weiter infallible Nebenallokationen; nicht sämtlicheKernel-OOMs alsgelöst ausgeben.

WindowsLocalTime bleibt bisherUTC, Controlbar hat eigeneCET/CEST-Umgebung. Gemeinsame konfigurierbareTZ mit korrekter Vererbung/DST fehlt; keine Stundenoffsets erfinden. LocaleINSTALLED bleibtleer/falsch, solange vollständigeWindows-Sprachdienste fehlen.

ASUS ausschließlich isoliert/offline ohne physische Tastatur/Receiver, keinPassthrough und keineHostinstallation. Layout/Controlbar/Effekte erhalten. Allgemeine Gesamtprüfung nurvorgemerkt.
