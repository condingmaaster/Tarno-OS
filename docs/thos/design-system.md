# THOS Design System

Eine Quelle für Aussehen und Bewegung der ganzen Oberfläche. Wer ein neues Programm oder ein neues Element baut, nimmt Bausteine aus `xtask/testdata/thosui.h` (Aussehen) und `thosmotion.h` (Bewegung) und bekommt damit automatisch das richtige Design.

## Grundsätze
1. **Ein Zustand, eine Antwort.** Jede Eingabe (Hover, Druck, Fokus, Öffnen, Schließen) bekommt sofort eine sichtbare Reaktion, nie mehr als 120 ms Verzögerung.
2. **Bewegung hat Masse.** Dinge, die ihre Größe oder Position ändern, bewegen sich über Federn (leichtes Überschwingen, dann Ruhe). Dinge, die nur erscheinen oder verschwinden, blenden mit einer Kurve.
3. **Glas statt Fläche.** Leiste und Menüs sind leicht durchsichtig, haben oben einen Lichtrand und einen Glanz; Fenster haben einen weichen Schatten, der mit dem Fokus kräftiger wird.
4. **Ruhe im Layout.** Abstände sind Vielfache von 4 px; Rundungen 8 (Chips), 10–12 (Zeilen, Knöpfe), 14–18 (Leiste, Menü), 12 (Fenster).
5. **Eine Bühne pro Ding.** Jedes Element hat seinen eigenen Zustand und seine eigene Bewegung; nichts wird „mitgezogen“, nur weil es daneben liegt.

## Farben (`thosui.h`)
| Token | Wert | Verwendung |
|---|---|---|
| `UI_BG` | `#0e1118` | tiefster Grund, Wallpaper-Basis |
| `UI_SURFACE` | `#171b26` | Fensterinhalt |
| `UI_RAISED` | `#1f2432` | Titelleisten, Karten |
| `UI_HOVER` | `#2a3144` | Hover-Zeile |
| `UI_LINE` | `#2d3448` | Trennlinien, Ränder |
| `UI_TEXT` | `#e8ebf4` | Text |
| `UI_MUTED` | `#8e97ae` | Nebentext |
| `UI_ACCENT` / `UI_ACCENT_HI` | `#5b8def` / `#7ba3ff` | Auswahl, Hauptknöpfe |
| `UI_GOOD` / `UI_WARN` / `UI_BAD` | `#4cc38a` / `#f0b24b` / `#e5534b` | Zustände |
Programm-Kacheln bekommen ihre Farbe aus dem Namen (`ui_app_color`) und ihr Symbol aus `ui_app_glyph`.

## Schrift
Lato (Regular/Bold) für Oberfläche, Hack für Terminal, Font Awesome 4 für Symbole. Größen: 11 (Nebentext), 12–13 (Listen, Knöpfe), 14 (Fließtext), 15–18 (Überschriften).

## Bausteine (`thosui.h`)
`ui_text`, `ui_text_width`, `ui_icon` · `ui_rrect`, `ui_rrect_g` (Verlauf), `ui_rrect_ga` (Verlauf + Transparenz), `ui_card` (Rand), `ui_disc`, `ui_gloss` (Glanz) · `ui_tile` (Programmkachel) · `ui_shade`, `ui_mix` (Farben) · `ui_set_clip` (Beschneiden).
Zusammengesetzte Teile (Zeilen, Chips, Pillen) entstehen aus diesen; wiederkehrende Formen gehören als Funktion in die Bibliothek, nicht in das einzelne Programm.

## Bewegung (`thosmotion.h`)
| Token | Wert | wofür |
|---|---|---|
| `MO_FAST` | 120 ms | Hover, Druck, Fokus |
| `MO_BASE` | 200 ms | Ein- und Ausblenden, Farbwechsel |
| `MO_SLOW` | 320 ms | Menüs, Panels |
| `SP_SNAPPY` | k 520, c 33 (Dämpfung 0,72) | Zustandswechsel mit kaum Überschwingen |
| `SP_SOFT` | k 260, c 27 (0,85) | sanftes Setzen |
| `SP_DOCK` | k 340, c 18,5 (0,50) | Leiste dehnt sich, Knöpfe wachsen: sichtbar federnd |
| `SP_POP` | k 430, c 26 (0,62) | Fenster öffnen |
| `SP_HOVER` | k 700, c 38 (0,72) | Hebung beim Überfahren |
Kurven: `ease_out` (Ankommendes), `ease_in` (Weggehendes), `ease_in_out`, `ease_out_back` (kleiner Überschwinger).
Die Animationsuhr (`mo_now_ms`) lässt sich mit `THOS_SLOMO=n` verlangsamen; so entstehen die Videos (`cargo xtask video-dock`).

## Wie die Leiste federt
Die mittlere Pille ist eine Reihe von Elementen (Starter, Trenner, Fensterknöpfe), jedes mit eigenen Federn für Breite, Anwesenheit, Hover und Fokus. Ein neues Fenster lässt seinen Knopf aus Breite 0 wachsen; die Pille ist die Summe der Breiten plus Rand und überschwingt deshalb mit. Ein geschlossenes Fenster schrumpft auf 0 und wird dann entfernt. Die Nachbarn rutschen mit, weil ihre Position die Summe der Breiten davor ist.

## Neues Programm anlegen (Checkliste)
1. Fenstergröße wählen (Vielfache von 4), Inhalt mit `UI_SURFACE` füllen.
2. Titel setzen (`WL_TITLE`): er erscheint in der Leiste; ein passendes Symbol steht in `ui_app_glyph`.
3. Text und Symbole nur mit `ui_text`/`ui_icon`; keine eigenen Bitmap-Schriften.
4. Hover- und Fokuszustände über `WL_POINTER` mit `c = 0` (Bewegung) und `c = 2` (verlassen) behandeln; Übergänge mit Federn oder Tweens statt Sprüngen.
5. Eintrag im Startmenü: `add_app` in `thospanel.c`.

## Abdeckung: jedes Element, seine Zustände, seine Bewegung
Diese Tabelle ist die Arbeitsliste. „✔“ = gebaut und im Video/Screenshot geprüft, „◐“ = gebaut, Feinschliff offen, „–“ = fehlt noch.

| Element | Zustände | Bewegung | Stand |
|---|---|---|---|
| Leiste (3 Glas-Pillen) | normal, Hover, Fokus, Druck | Pille federt mit der Summe der Breiten; Knöpfe wachsen und schrumpfen | ✔ (Druck-Zustand –) |
| Starter / Fensterknopf | Hover, Fokus, vorhanden/weg | Hebung + Vergrößerung (Feder), Fokus blendet, Einblenden beim Öffnen | ✔ |
| Startmenü | offen/zu, Zeile Hover, Chips an/aus, Fußknöpfe | wächst aus der Start-Pille (Skalierung + Einblenden), Hover-Platte gleitet zwischen Zeilen | ◐ (Chip-Wechsel springt noch) |
| Fenster | öffnen, Fokus, schließen, Ziehen | Pop-in (Feder + Einblenden), Fokuswechsel weich (Schatten, Titel), Geisterbild beim Schließen | ✔ (Minimieren: Fenster schrumpft zur Leiste und blendet aus, Knopf in der Leiste bleibt markiert, Klick holt es federnd zurück; der Knopf des fokussierten Fensters minimiert es; Maximieren für Wayland-Fenster) |
| Schließen-Knopf | Hover | Scheibe blendet per Feder ein | ✔ |
| Desktop-Symbole | Hover, ausgewählt, Start | Hebung + Lichtplatte, Hüpfer beim Starten | ✔ |
| Dateimanager | Zeile Hover/Auswahl, Orte, Leer | Auswahl- und Hover-Platte gleiten, weiches Scrollen mit Mausrad, Scrollbalken blendet | ✔ |
| Software-Zentrale | Suche, Ergebnis Hover/Auswahl, Installieren/Entfernen | wie Dateimanager | ◐ (Installation läuft in der App: Knopf wird „Installing“ mit gleitendem Balken, letzte Ausgabezeile in der Fußleiste; Knopf-Druck –) |
| Bildbetrachter | Bild, Fehler | – (Überblenden beim Blättern fehlt) | ◐ |
| Terminal | Cursor, Farben | Cursor atmet im Leerlauf, ist bei Eingabe voll | ✔ |
| Zeiger | Pfeil, Text (I-Balken), Hand, Warten (Ring dreht sich); das Fenster fordert die Form mit `WL_CURSOR` / `ui_cursor()` an | Formwechsel sofort; Ring animiert | ✔ (Größenänderungs-Pfeile –) |
| Hintergrund | – | – | ✔ |
| Benachrichtigungen (Toasts) | Info, Fertig, Warnung, Fehler; bis zu 4 gestapelt | gleiten per Feder (SP_POP) von rechts ein, stehen 5 s, blenden aus; `thosnotify.h` oder Befehl `thosnotify 'Titel|Text'` | ✔ (Klick zum Schließen –) |
| Start-Bildschirm / Anmeldung | Splash mit Wortmarke + Fortschrittsbalken, danach Anmeldekonsole im THOS-Farbschema (Logo, zentrierte Spalte, keine Log-Zeilen) | Balken füllt sich an Startmeilensteinen | ◐ (grafische Anmeldung im Desktop –) |
| Einstellungen (About, Network, Storage, Display) | Seitenleiste Hover/Auswahl, Karten mit Werten, Nutzungsbalken (`ui_bar`) | Platten gleiten, Seite blendet und rutscht 14 px ein (MO_BASE), Werte live alle 2 s | ◐ (Aussehen/Akzent, Ton, Konten fehlen) |

Neue Elemente kommen zuerst hier als Zeile hinein; erst wenn Zustände und Bewegung beschrieben sind, wird gebaut.
