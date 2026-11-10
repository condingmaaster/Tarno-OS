# Umsetzung: sichtbare Flächen und schnelleres Fensterziehen

Die Änderungen setzen die unmittelbar im bestehenden CPU-Compositor nutzbaren
Ansätze aus `compositor-vergleich-2026-10-11.md` um. Designgeometrie, Farben,
Animationen, Schatten und Transparenz werden nicht geändert.

## Zeichenpfade

- `paint_scene` sucht deckende Innenbereiche von Fenstern. Dahinter werden
  Hintergrund und niedrigere Fenster übersprungen. Rundungen und animierte
  oder transparente Fenster bleiben ausgeschlossen. Eine begrenzte Zahl von
  Regionsteilungen verhindert unbeschränkte Fragmentierung; der ursprüngliche
  vollständige Szenenpfad bleibt der Rückfall.
- `move_cached_content` verschiebt den bereits zusammengesetzten deckenden
  Inhalt des obersten Fensters im Backbuffer. Die Kopierrichtung berücksichtigt
  Überlappungen. Alte und neue Ränder, Titelleiste, Schatten und freigelegte
  Flächen werden normal neu zusammengesetzt. Noch ausstehende Client-Commits
  werden am alten Ort gezeichnet, bevor die Pixel kopiert werden.
- Der Bewegungspfad fällt bei Alpha, Fensteranimationen, Bildschirm-Clipping,
  Kontextmenü, Switcher, Toasts und aktiven Ghosts auf den normalen Renderer
  zurück. Es wird keine Animation abgeschaltet oder übersprungen.
- `move_window` verschiebt auch die Position eines Öffnungs-Schnappschusses und
  zeichnet dessen alte/neue Animationsfläche neu. Die Öffnungsanimation läuft
  unverändert weiter, statt während des Ziehens an der alten Position zu bleiben.
- `restore_cursor` stellt die alte Cursorfläche aus dem gültigen Backbuffer
  wieder her. Ausstehende Composition läuft vor der Kopie, anschließend wird
  der neue Cursor darüber gezeichnet.
- `snap_build` meldet seine temporär überschriebenen Backbuffer-Pixel nun selbst
  als Damage. Damit bleiben Cursor- und Bewegungskopien auch dann korrekt,
  wenn später im selben Ereigniszyklus ein Schnappschuss erzeugt wird.
- `frame_wait_ms` wartet nur bis zu den vorhandenen Animations-/Callback-Fristen.
  Zeichenzeit zählt zum Intervall; es wird nicht zusätzlich immer ein vollständiges
  16-ms-Intervall gewartet. Eingaben können das Poll weiterhin sofort wecken.

## Messungen und Regressionen

`thosdamage-test.c` vergleicht gegen den ursprünglichen vollständigen Szenenpfad,
nicht nur gegen ein vollständiges Neuzeichnen mit derselben Optimierung.
320 vollständige Framebuffer stimmen pixelgenau überein. Darunter sind
126 Bewegungen mit Kopie und 114 Rückfälle, gleichzeitige Inhaltsänderungen,
Alpha, Überdeckung, Clipping, Fokus, minimierte Fenster, Kontextmenü, Ghosts,
Cursorwechsel, Bewegung während der Öffnungsanimation und Schnappschuss-Erzeugung nach einer Bewegungskopie.
Die Wartefristen werden ebenfalls geprüft. GCC `-O2` und UndefinedBehaviorSanitizer
bestehen. Der bestehende Formenvergleich besteht weiterhin für 10.000 Fälle.

Ein Host-Vergleich für 100 Neuberechnungen einer Szene mit zwei überlappenden
780 × 500-Fenstern bei 1280 × 800 ergibt: vollständiger Pfad 237.357 µs,
sichtbare Flächen 168.935 µs, etwa 29 % weniger CPU-Zeit. Das ist kein
Framebuffer-/FPS-Test und keine Messung am Acer.

QEMU: `desk-drag-test` und `desk-minimize-test` bestehen. Beim Ziehtest werden
Bilder während festgehaltener Maustaste an drei Positionen geprüft; nach
Loslassen bleibt das Fenster stehen. Die Screenshots wurden zusätzlich angesehen.

Die Performance-Probes unterscheiden `scene`, `present`, `frame` und `move-copy`
vom bisherigen `compose`. `frame` umfasst einen Damage-Flush mit Backbuffer-
Kopien und Cursor, `move-copy` den Kopier-/Damage-Teil der Bewegung außerhalb
dieses Flushs. Sie messen keine physisch sichtbare Präsentation und dürfen
nicht als komplette Eingabelatenz oder FPS ausgegeben werden.

Der TCG-Lasttest mit derselben Mausbahn wie vor dieser Umsetzung besteht:
821 Compose-Aufrufe, durchschnittlich 5.312 µs (vor dieser Umsetzung:
702 Aufrufe, 11.589 µs). Der Kopierpfad wurde in 100 erfassten Bewegungen
benutzt, mit durchschnittlich 1.389 µs für seinen Kopier-/Damage-Teil.
Die erfassten Damage-Flushs benötigen durchschnittlich 4.920 µs; der
Höchstwert beträgt 45.931 µs. Geänderte Aufrufgrößen, zusätzliche Probes,
fehlende Nachlauf-Samples im älteren Image und Host-Schwankungen begrenzen
den Vergleich. Er zeigt noch keine garantierte Bildrate oder Ruckelfreiheit.
Die Ausgabe am Programmende meldet im endgültigen Stand auch die verbleibenden
Samples, die noch kein vollständiges Zwei-Sekunden-Intervall erreicht haben.

## Verbleibende Hardwarearbeit

Echte VBlank-Synchronisierung, Page Flips und ein Hardware-Cursor brauchen eine
neue Ausgabeschnittstelle samt Grafiktreiber. Der bisherige gemappte Boot-
Framebuffer bietet sie nicht. Die neue Fristberechnung ist ein besserer
CPU-Zeitplan, kein emuliertes oder behauptetes Hardware-VSync. Ein Acer-Test
steht aus.
