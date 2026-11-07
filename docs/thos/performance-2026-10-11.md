# Performance-Korrekturen bei unverändertem THOS-Design

Nutzervorgabe: Layout, Taskleiste, Farben, Rundungen, Schatten, Transparenz,
Animationen und Effekte bleiben erhalten. Vorerst ausschließlich Fehler und
Performance bearbeiten. Der frühere alternative Designentwurf wird nicht umgesetzt.

## Änderungen

### Zeichenbereiche innerhalb eines Ereigniszyklus bündeln

`thosdesk.c` sammelte bisher keine Zeichenanforderungen aus demselben Zyklus:
Hover, Fokus, Client-Commit, Mausbewegung und Animation konnten dieselben Pixel
mehrmals zusammensetzen und in den Framebuffer schreiben. Jetzt sammelt eine
begrenzte Warteschlange die betroffenen Rechtecke. Überlappende Bereiche werden
zusammengelegt, wenn ihre gemeinsame Bounding Box nicht mehr Pixel kostet als
beide Einzelbereiche. Bei sehr vielen Bereichen bleibt ein begrenzter Fallback.

Das Zeichnen erfolgt vor dem nächsten `poll`, auch nach einem frühen `continue`,
und abschließend beim Verlassen der Schleife. Es wird keine zusätzliche Wartezeit
eingeführt. Der Zeiger wird nach den zusammengefassten Bereichen einmal darüber
gezeichnet. Federn, Kurven, Zeiten, Effekte und deren Parameter bleiben erhalten.
Fensterschnappschüsse verwenden weiterhin den vorhandenen Renderer.

### Gleiche Rundungen und Verläufe günstiger zeichnen

`thosui.h` berechnet das horizontale Clipping einmal pro Form. Nur Eckpixel
brauchen die bisherige Antialiasing-Berechnung. Voll deckende Innenbereiche werden
direkt gefüllt; transparente Bereiche behalten die exakte bisherige Mischung.
Es werden keine optischen Eigenschaften vereinfacht.

## Nachweise

- `thosui-raster-test.c`: 10.000 deterministisch erzeugte Formen gegen den
  ursprünglichen skalaren Renderer verglichen. Unterschiedliche Positionen,
  Clipping, Radien, Farben, Verläufe und Transparenzen: jeder Pixel identisch.
- `thosdamage-test.c`: Begrenzung und vollständige Abdeckung der Warteschlange
  geprüft, außerdem 80 Vergleiche des gesamten Framebuffers gegen vollständiges
  Neuzeichnen. Enthalten: Alpha, Schatten, Fokus, Fensterschnappschüsse, Cursor,
  Bewegung und minimierte Fenster. Auch die Verwendung des Backbuffers als
  temporärer Schnappschuss-Puffer wird geprüft.
- Beide Host-Tests bestehen auch mit UndefinedBehaviorSanitizer. AddressSanitizer
  konnte wegen der vorinstallierten Laufzeit-/Preload-Reihenfolge in der Sandbox
  nicht ausgeführt werden.
- Form-Mikrobenchmark auf dem Host, 500 deckende 780 × 500-Verlaufsrechtecke:
  ursprünglich 0,718 s, optimiert 0,145 s (GCC `-O2`). Das ist ein Test der
  Zeichenfunktion, keine Desktop-FPS-Messung oder Aussage über Acer-Hardware.
- `cargo xtask desk-minimize-test`: bestanden. Minimieren, fortbestehender Dock-
  Knopf und Wiederherstellen durch den Dock-Knopf; Screenshots geprüft.

Host-Tests vom Repository-Stamm aus:

```sh
gcc -O2 xtask/testdata/thosui-raster-test.c $(pkg-config --cflags --libs freetype2) -lm -o /tmp/thosui-raster-test
/tmp/thosui-raster-test
gcc -O2 xtask/testdata/thosdamage-test.c $(pkg-config --cflags --libs freetype2) -lm -o /tmp/thosdamage-test
/tmp/thosdamage-test
```

## Grenzen

Die Pixelvergleiche prüfen definierte Szenarien; sie ersetzen keine vollständige
Desktop-Suite oder den Test am Acer. Die Optimierungen erhalten die bestehenden
Animationsabläufe. Der Gesamtgewinn hängt vom Anteil an Formen, überlappenden
Zeichenanforderungen und Framebuffer-I/O ab.

## Messung am gesamten Desktop

Der bisherige TCG-Lauf nach den Änderungen liefert noch keinen belastbaren
Gesamtgewinn: 702 Compose-Aufrufe mit durchschnittlich 11.589 µs, gegenüber
905 Aufrufen mit 5.134 µs im vorherigen Lauf. Die Bündelung verändert Umfang
und Anzahl pro Aufruf; die Mittelwerte sind keine FPS und messen nicht die
Eingabelatenz. Auch die Dateimanager-Zeiten schwanken zwischen den Läufen.
Die Host-Mikrobenchmark darf daher nicht als Beschleunigung des gesamten
Desktops ausgegeben werden. Weitere vergleichbare Messungen sind nötig.

`cargo xtask desk-drag-test` verwendet echte emulierte PS/2-Mauspakete:
Titelleiste anfahren, linke Taste halten, in Schritten bewegen, Bilder bei
+40/+30, +80/+60 und +120/+90 aufnehmen, loslassen und den Zeiger weiterbewegen.
Der Inhalt wird pixelgenau an jeder erwarteten Position geprüft. Nach dem
Loslassen bleibt das Fenster stehen; die vorher belegte Fläche wird geprüft.
Die Bilder während des Haltens prüfen den Zustand, keine kontinuierliche
Bewegungsaufnahme und keine garantierte Reaktionszeit. Hover-Änderungen nach
dem Loslassen werden durch Bewegen außerhalb des Inhalts vermieden.
