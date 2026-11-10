# THOS – Fortsetzung am Abend des 16. Oktober 2026

Die automatische Fortsetzung wurde vom Nutzer ausdrücklich wieder aktiviert. Der Lauf um 22:03 Europe/Berlin hat das tatsächlich zurückgesetzte 300-Minuten-Kontingent geprüft: 0% verbraucht, Wochenkontingent 46% verbraucht; gemeldeter nächster Reset 1792199009. Bei höchstens 5% Rest stoppen und die bestehende Automation einmalig kurz nach dem dann tatsächlich gemeldeten Reset neu terminieren. Bei erreichtem Wochenlimit oder unbekanntem Reset keine Fortsetzung erfinden. Neue Pause-/Stoppanweisungen haben Vorrang.

Aktuell geprüftes Live: target/thos-live.img SHA256 3dbd0cfd70255f60ca476d7e5e491f6171f9f77b2b184ab1e8b128d1520997c6. Manifest target/thos-live-2026-10-16-focus-build-manifest.json; normaler Kernel thos-kernel-unicode-2026-10-16.elf, Root live-root-focus-2026-10-16.ext2, DLL kernel32x-module-2026-10-16.dll und CUI thosdesk-focus-2026-10-16. Alle Vorgänger behalten, ältere Hashangaben unten historisch.

Win32-KeyDown/KeyUp und nun auch Compositor-Fokusmeldungen tatsächlich geprüft und lokal übernommen, inklusive Controlbar/Minimieren/nativerTerminalwechsel auf Westmere. Keine vollständigen SetFocus/GetFocus/IME/TranslateMessage/AsyncKeyState-APIs behaupten. Kernel/DLL gegenüber Vorgänger unverändert.

Neue gezielte Windows-UTF-Fortsetzung: GetCPInfo meldet für CP_UTF8 bisher MaxCharSize1 statt4 und akzeptiert unbekannte Codepages. Echte PE-Probe mit tatsächlich aus Metadaten begrenztem UTF8-Puffer läuft gegen eingefrorene Focus-QA (winunicode-probe, /tmp/thos-codepage-before-2026-10-16.log). Noch keine neue DLL im Live. Kernel/CUI für diese Arbeit unverändert lassen; neue DLL separat einfrieren und vollständig testen. ASUS/Spiel/HW-Grenzen siehe aktueller Bericht/Worklog.

## Grenzen

Layout/Controlbar/Animationen/Effekteerhalten. KeineSubagenten. AllelokalenÄnderungenundBackupsbehalten,.gitreadonly. NICHTSanechterTastaturändern(keineHID/RGB/Profile/Belegung/Firmware,keinphysischesUSB-/ReceiverPassthrough,keineASUSHostinstallation). KeineautomatischephysischeDatenträgeraktualisierung. GroßeHotspotDownloadsvermeiden. AllgemeineGesamtprüfungnurbemerkt.

QEMUausschließlichseriell,vorStartInventurimERHÖHTENHostkontext;normaleSandboxPSsiehtHostVMsnicht. /tmp/thos-one-case-2026-10-16.sh (dauerhaftauchdocs/thos/logs/2026-10-16)hältflock target/thos-qemu-tests.lockundprüftaufQEMU. AlleSerienbeendet,keinenTesthintergrundprozessfortsetzen. NormaleTestsundJDKProbe2GiB/SMP4,Firefox4GiB/KVM;Host7.5GiB,keine8GiB-Gäste.

## GeprüftesaktuellesLive

target/thos-live.img SHA256 **0c42ce24e9444e49710e3c1a2813e7d60ec9700a81f54330fa86ce40b71356b8**. Manifest target/thos-live-2026-10-16-lock-wait-build-manifest.json. Kandidat thos-live-lock-wait-2026-10-16.img. NORMALinteractiveKernel thos-kernel-lock-wait-2026-10-16.elf (keineVMdiagnose/Heapcache),Root live-root-keyboard-2026-10-16.ext2. AktuellesreleaseKernelELFistdieserWaitKernel;vortrotzdemHashvergleichen,nichtblindneuereExperimenteverpacken. Quellensnapshot thos-lock-wait-source-2026-10-16.json. CUI thosdesk-keyboard-modifiers-2026-10-16 SHA256a9442c92b9b17804715d307d8cf0fa3e1ca253e4010c57b6633c1a208ac31006.

LiveFATab1048576: ::boot/thos-kernel und ::boot/thos-root.ext2,KEINEPartition2! QAhatMBRP2(Start/SektorenanOffset470),GrenzenundGrößeprüfen. QA-Patchhelferextrahiertsparse,schreibtnurveränderteChunksmitRücklesevergleich,fsckbleibt. EinigeeigeneTestartefaktebyteidentischsparsifiziert(jedesvollständigcmp),keineBackupsverworfen. Vorversiond0428c…als thos-live-record-locks-2026-10-16.img,326c…als thos-live-keyboard-2026-10-16.imgundalleälterenbehalten. KeinphysischerStickaktualisiert.

## Erreichtundgeprüft

WaylandmehrerePointer/KeyboardressourcenunabhängigmitFokus/Release/RebindundVersionierung;Shift/Ctrl/AltGranWayland,LeftAlt/SuperCompositorkürzelbehalten. LeeresHeldKeySnapshotbeiEnterbleibtGrenze. ReadOnlyreguläreDateienundFilemmapZugriffschecksbehoben. BSDflockOFD/WeakArc-BesitzundPOSIXfcntl5/6/7PID-/BereichsbesitzmitSplit/Merge,Konflikten,Ownerabfrage,AnyClose/Dup2/CLOEXEC/Exit,Blockieren/EINTR/Deadlock. OFDfcntl36/37/38explizitEINVAL;andereBackendslocksggEOPNOTSUPP.

BeideLockartenwartenGeneration+WaitQueue.wait_if_intrstatt10msTimerpolling. QueuepredicatefasstkeineFD-/Locktabellenmutexan. BSDflockWakeauchamletztenArclockfähigerFiletypen,inklROWrapper/Stream. KeineHintergrundworker.

Alle9WaitKernelRegressionenPASS:POSIXundechteSQLite3.46.1DELETEJournalTCG/Westmere;1000gemischteBereichevsunabhängigesByteModellmitzweitemProzess;readonly-Dateien;BSDflockinklletztemCloseRW/ROklein/RO512KiB;memfdFreeze/Seals/Fork/Unmap;PE19;FirefoxEndezuEnde;exaktLiveTCG/Westmere/2GiB/SMP4/offline. Kernel/volleRAMrootrückgelesenundverglichen,lock-wait-live-desktop.pngvisuellgeprüft. VorherigeCUIRegressionen13Pointer/10Pixel/5Keyboardphasen/6nativeAppsResize/Pty/Max/F11/echterPEWindowsResize/Alt-F4/GTK3ebenfallsPASS. Layoutunverändert. Logs/Screenshotsunterdocs/thos/{logs,screenshots}/2026-10-16.

Firefox153.4ESRprüftisolierteLoopbackHTTPDownloads:PE243939B/ELF2024544B,vollständigeBytes,keinAutoexec,FilesöffnetbeideausdrücklichimTerminal/Controlbar. Sandbox-disabledDiagnosemitkleinemProzesspool,keinProduktionsbrowser-/allgemeinerHTTPSNachweis,FirefoxNICHTLive. locking-protocolTextimReadonlyFall1,RecordKernelFall0;nurbeobachtetesErgebnis,nichtWALSupportbehaupten. MOZ_CRASHREPORTER_DISABLEwarunwirksam.

ZusätzlicheJDK21ProbePASS:javackompiliertimGast,JVMOwner/konkurrierendesFileChannel.tryLock/null/nachFreigabeReopenundByteskorrekt. KleinevorhandeneTestoptionen(SerialGC/96m/etc),JDKnurQA,nichtzusätzlichLive. clone3(435)fehltweiter,erfolgreicherFallbackimLauf. Quelle JdkFileLockProbe.java/jdk-lock.sh;Casejdk-lock-testbenötigtBrowserQAmitJDK. NeuestebrowserQA thos-lock-wait-browser-qa-2026-10-16.img.

## OffenePunkte

ASUSAura/ArmouryInstallerweiterblockiertanSetDefaultDllDirectories/anderenWindowsAPIs/DLLs/Diensten/UWP. KeinAuraStart/Installation/Hardwarekontrolle. SkiFree32Bitabgelehnt,keinSpielerfolg. OnDiskLoadLibrarylädtnichtallgemeinDLLszurLaufzeit;32BitPEfehlt. EchteAcerWLAN/GPUFirmwareUploads/Accelerationbleibenoffen. VollesDAC,SharedFilemaps/WAL/mprotectSemantik,COW/Forkkopierenoffen. KeineallgemeineGesamtprüfungbeginnen. WeiterehistorischeDetailsinBericht/Worklog/Resume-Historien.
