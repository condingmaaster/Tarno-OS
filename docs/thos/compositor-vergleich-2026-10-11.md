# Fensterziehen: Vergleich mit offenen Grafiksystemen

Recherche am 11. Oktober 2026. Bestehendes THOS-Layout, Taskleiste,
Animationen, Schatten und Transparenz bleiben erhalten. Diese Recherche
ändert keine Rendering-Implementierung.

## Was die anderen Systeme tun

**Linux-Eingabe:** evdev liefert Ereignisse mit Zeitstempeln; `SYN_REPORT`
grenzt zusammengehörige Zustandsänderungen ab. Bewegung und Tastenzustände
sind getrennt beschrieben. Bei Queue-Überlauf gibt es mit `SYN_DROPPED`
ein Verfahren zum erneuten Abfragen des Zustands. Das ist eine Grundlage
für zuverlässige Eingaben, keine automatische Beschleunigung des Zeichnens.
[Kernel-Dokumentation](https://docs.kernel.org/input/input.html),
[Ereignisprotokoll](https://docs.kernel.org/input/event-codes.html).

**Wayland/Weston:** Anwendungen behalten ihre Bildpuffer und melden geänderte
Bereiche. Frame-Callbacks geben Hinweise, wann das nächste Bild sinnvoll ist;
sie sind kein Nachweis einer tatsächlichen Bildschirmausgabe. Deckende Bereiche
helfen, unnötiges Zeichnen dahinter zu vermeiden. Weston verwaltet Damage,
Ausgabetiming und Repaint-Zustand pro Ausgabe.
[Wayland-Spezifikation](https://wayland.freedesktop.org/docs/html/apa.html),
[Weston-Outputs](https://wayland.pages.freedesktop.org/weston/toc/libweston/output.html).

**Haiku:** `Desktop::MoveWindowBy` bestimmt die alte und neue sichtbare Region,
kopiert die weiterverwendbaren Pixel mit `CopyRegion` und entfernt deren neue
Position aus der verbleibenden Dirty-Region. Erst der Rest wird als neu zu
zeichnen markiert. `Window::MoveBy` verschiebt auch ausstehende Dirty- und
Update-Regionen. Das ist ein konkretes Beispiel für effizientes Fensterziehen
im App-Server, nicht bloß ein Verweis auf GPU-Beschleunigung.
[Desktop.cpp](https://github.com/haiku/haiku/blob/master/src/servers/app/Desktop.cpp),
[Window.cpp](https://github.com/haiku/haiku/blob/master/src/servers/app/Window.cpp).

**SerenityOS:** `Compositor::recompute_occlusions` berechnet sichtbare deckende
und transparente Regionen separat und schneidet verdeckte Flächen aus.
Der Zeichenpfad schneidet diese Regionen mit den tatsächlich geänderten
Bereichen. Transparenz und Übergänge sind dabei ausdrücklich berücksichtigt.
[Compositor.cpp](https://github.com/SerenityOS/serenity/blob/master/Userland/Services/WindowServer/Compositor.cpp).

**Linux-Bildausgabe:** DRM/KMS kann fertige Framebuffer mittels Page Flip zur
vertikalen Austastphase umschalten und den Abschluss melden. Damit kann ein
Compositor seine Arbeit zur Anzeige koordinieren und Tearing vermeiden.
Das setzt einen passenden Grafiktreiber voraus.
[KMS-Dokumentation](https://docs.kernel.org/gpu/drm-kms.html).

Die verlinkten Quellcodezweige sind bewegliche Referenzen. Die Aussagen beziehen
sich auf den bei der Recherche gelesenen Stand.

## Abgleich mit THOS

THOS hat bereits wichtige Grundlagen: gespeicherte Fensterpixel (`win::px`),
Damage bei Client-Commits, schnelle Kopierpfade für deckende Inhaltszeilen,
eine Schatten-Lookup-Tabelle und Zusammenfassung wartender Mausbewegungen
bei gleichem Tastenzustand. Die neue Damage-Warteschlange bündelt außerdem
Zeichenanforderungen innerhalb eines Ereigniszyklus. Diese Dinge müssen
nicht nochmals als angeblich fehlende Funktionen eingebaut werden.

Der aktuelle Ziehpfad in `thosdesk.c` ruft `compose2(old, outer_of(w))` auf.
`compose_inner` kopiert im betroffenen Rechteck zuerst den Hintergrund und
zeichnet dann die Fenster von unten nach oben. Deckende obere Fenster
verhindern derzeit nicht, dass die später verdeckten Pixel darunter bereits
bearbeitet wurden. Kleine Bewegungen können weiterhin ein großes Rechteck
betreffen. Die Damage-Bündelung allein beseitigt diese Arbeit nicht.

Der Cursor wird über das Bild gezeichnet. Das Wiederherstellen seiner alten
Position führt im normalen Mauspfad erneut durch `compose`, obwohl der
Backbuffer bereits ein Bild ohne Cursor hält. Hier ist ein kleinerer
Kopierpfad möglich, sofern ausstehende Bildänderungen korrekt berücksichtigt
werden.

Die Ausgabe schreibt Zeilen direkt in den gemappten Boot-Framebuffer und
führt danach `sfence` aus. Das ist keine atomare Präsentation eines vollständigen
Bildes. Die untersuchte `/dev/fb0`-Schnittstelle hat Geometrieabfragen, aber
keine Page-Flip-/VBlank-Schnittstelle. Die 16-ms-Callback-Prüfung in `wls_tick`
ersetzt keine Synchronisierung mit einer tatsächlichen Bildausgabe.

## Priorität für die weitere Performance-Arbeit

1. **Zeit und Arbeit getrennt messen:** Eingabezeit, Verarbeitungsbeginn,
   Fensterposition, Compose-Ende und Framebuffer-Schreibende; außerdem
   Pixelanzahl und Zeit für Hintergrund, Fenster, Effekte und Ausgabe.
   Das Framebuffer-Schreibende ist noch kein gemessener sichtbarer Zeitpunkt.
   Dieselbe Mausbahn wiederholt vergleichen. Die bisherigen durchschnittlichen
   Compose-Zeiten sind keine FPS oder vollständige Eingabelatenz.
2. **Deckende sichtbare Regionen berechnen:** Hintergrund und untere Fenster
   dort überspringen, wo ein deckender Bereich sie vollständig ersetzt.
   Rundungen, transparente Fenster, Schatten, Ghosts und Overlays dürfen
   nicht als deckende Flächen behandelt werden. Das reduziert Arbeit ohne
   die fertigen Pixel zu ändern.
3. **Fensterbewegung als eigenen schnellen Pfad prüfen:** Unveränderte,
   deckende Pixel nach dem Haiku-Prinzip weiterverwenden. Freigelegte Flächen
   und hintergrundabhängige Schatten/Transparenz neu zusammensetzen.
   Bei gleichzeitigem Commit, Fokusanimation, Größenänderung oder ungeklärter
   Überdeckung auf den vollständigen korrekten Pfad zurückfallen.
4. **Cursor unabhängig aktualisieren:** Alte Cursorfläche aus dem gültigen
   Backbuffer wiederherstellen; erst nach notwendiger Composition den neuen
   Cursor zeichnen. Tastendrücke und Loslassen bleiben in Reihenfolge.
5. **Ausgabetiming verbessern:** Zunächst einen gemeinsamen, gemessenen
   Bildtakt statt unkoordinierter relativer Wartezeiten untersuchen.
   Echte Page Flips und ein Hardware-Cursor sind eine spätere Treiberarbeit;
   CPU-Optimierungen sind bereits vorher möglich. GPU-Unterstützung darf
   nicht als Voraussetzung für jede Verbesserung behandelt werden.

Das sind aus den Quellen und dem THOS-Code abgeleitete Kandidaten. Ihr Nutzen
für THOS ist noch zu messen. Die bisherigen TCG-Läufe zeigen insbesondere
noch keinen belastbaren Gesamtgewinn der Damage-Bündelung.

## Prüfung vor Übernahme

Neue Pfade gegen den bisherigen Renderer pixelgenau vergleichen, auch bei
Überdeckung, Bildschirmrändern, laufenden Client-Commits, Fokuswechsel,
Transparenz und allen Animationen. Den Maus-Ziehtest zusätzlich während
kontinuierlicher Bewegung aufnehmen und mit Zeitstempeln auswerten.
Danach auf dem Acer testen; TCG und reale Hardware bleiben getrennte Ergebnisse.
