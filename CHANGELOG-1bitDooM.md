# 1bitDooM – Änderungsliste (Stand ce736db, 2026-10-07)

Basis: UZDoom trunk 907448a. Alles unten ist der aktuelle Endzustand.

## Fork / Menü
- Version "0.1 alpha" statt Git-Beschreibung (Konsole, Fenstertitel); Start-Logo (bootlogo.png) entfernt.
- Eigenständiger Fork "1bitDooM": Windows-Build-Workflow, eigenes README, Umbenennung (Fenstertitel, Config-/Savordner, exe-Metadaten).
- Optionsmenü: Auswahlpfeil in der Menü-Auswahlfarbe statt festem Rot; GAMEINFO `MenuFont` und `MenuSelector`; ohne `MenuSelector` wird Zeichen 13 der MenuFont genutzt.
- MODELDEF-Keyword `Outline` (Hull-Outline pro Modell).

## Map-Geometrie-Outlines (Screen-Space, `gl_outline`)
- Post-Process-Pass `outline.fp` auf Tiefen- und Normalpuffer (Silhouetten, Kanten, Wand/Boden/Decke-Creases).
- Nur Map-Geometrie: Modelle, Sprites, maskierte/transparente Wände (Zäune) bekommen keine Linien.
- Reichweite mit Ausblendung (`gl_outline_range`, 0 = unbegrenzt), Alpha, Farbe, Tiefen- und Winkelschwelle.
- Linien immer genau 1 Pixel dick.
- Creases nur dort, wo Wände auf Boden/Decke treffen (auf der Wandseite); Wand-Wand-Ecken nur als Tiefen-Silhouette.
- Kein Sky: keine Linien zum Sky hin, Sky-Wände (Sky-Hack) und Portal-Tiefenpass als Sky markiert; Oberkante unter Sky-Decke bleibt erhalten.
- Keine Outlines an Wänden von Linien mit "Not shown on map" (ML_DONTDRAW).
- Keine Outlines an Wänden mit Sky-Textur, ohne gültige Textur sowie Farb-, Nebel- und Spiegelwänden.
- (entfernt) Zusammenfassen naher paralleler Linien (`gl_outline_merge`) wurde wieder entfernt.
- Weiße Linien auf schwarzen Flächen (`gl_outline_dark`, Helligkeitsgrenze `gl_outline_dark_level`): Helligkeit der Fläche steckt im Alpha des Fog-Puffers.
- `gl_outline` = 0 Aus / 1 Normal / 2 Erweitert. Erweitert zeichnet zusätzlich die Wand-Sky-Grenze und konkave Wand-Innenecken (1 px). Menü-Auswahl "OutlineMode".

## 3D-Modelle
- Model-Outline (Hull) über Menü (`gl_model_outline`, Breite `gl_model_outline_width` in Karteneinheiten); Hull wird bei Skins mit transparenten Pixeln übersprungen.
- Modell-Schatten: flache schwarze Silhouette am Boden, wählbar für Gegner/Dekoration, Spieler-Schalter, Filter (Shootable/nicht-solide überspringen).
- Hüllen-Outline mit geglätteten Normalen (an Vertices gleicher Position gemittelt, im Lightmap-Attribut abgelegt) und Gehrung: keine Lücken mehr an harten Kanten (Würfel).
- Modelle über TEXF_Model/NoOutline-Flags im Normalpuffer markiert (OpenGL wie Vulkan).
- Innere Outline für Modelle (`gl_model_inner_outline`, Abstand `gl_model_inner_outline_distance`, Standard 512): 1 px Linie innen an der Silhouette. Modell-Markierung im 2-Bit-Alpha: Alpha 0 + Normale (1,1,1).
- Ferne Monster komplett schwarz ab Abstand (`gl_model_black_distance`).

## Spielerwaffe
- Waffen-Outline als Hülle (Inverted Hull) nur für das HUD-Modell, Dicke in Bildschirmpixeln (`gl_weapon_outline`, `gl_weapon_outline_width` 1-8, `gl_weapon_outline_color`, Hüllenflächen immer umgedreht); zeichnet auch Linien zwischen Waffenteilen. HUD-Modelle ignorieren `gl_model_outline`.
- Waffen: weder Hülle noch Außenkontur bei MDL_FORCECULLBACKFACES; zusätzlich Kontur nach außen (`gl_weapon_outer_outline`, Pixel, auch für flache Modelle wie den Mündungsblitz) als Post-Process.
- Waffen-Pixelung nur für das HUD-Modell (`gl_weapon_pixel`: Aus, 2, 3, 4, 6, 8), unabhängig von jupiter3d_pixelate.

## Ausprobiert und wieder entfernt
- Outline-Varianten: dünner werdende Linien, Local-Maximum-Suppression, 3-Pass-Dilation, Kontrastkurve.
- Frühe Waffenpixelung (Texturen-Snapping, Alpha-Maske).
- Distanz-Unschärfe (Disc, Gauss), Fern-Texturen schwarz/weiß, halbe Auflösung, ganzzahliger Faktor.
- Modell-Outline in Pixeldicke, Stufen nach Auflösung, fließender Übergang.
- Crash-Fix (Zugriff auf veraltetes Material) vor der Entfernung der Fern-Textur-Features.


- Map outline width is adjustable again (gl_outline_width, Int 1..4 render pixels, menu slider "Outline width (pixels)").

- Map outline: no distance fade any more (hard cut at gl_outline_range); weak line responses are boosted so distant lines stay full strength.

- Map outline: lines flagged "Not shown on map" are fully ignored by the outline pass: no line on their walls and no depth lines on the neighbouring pixels (new surface marker TEXF_HiddenLine).

- Inner outline for far models: only drawn where the neighbouring non-model pixel is at least 24 map units farther away (real silhouette); no more lines along the alpha edge of decal / cut-out models lying on a wall.

- Far-model inner outline is switched off only for non-solid actors (decals, cut-out graffiti, blood, corpses, items); all solid actors keep it.

- White lines on black surfaces: the colour is decided from a 9x9 neighbourhood (25 taps): white only if nearly everything around is dark, so mostly bright graphics keep black lines even with dark pixels at the edge.

- Model drop shadows shrink with the height of the actor above the floor (about the actor's position) and disappear at gl_model_shadow_fade_height (default 128 map units, 0 = always full size; menu slider "Shadow gone at height").

- Soft model drop shadow (gl_model_shadow_style 1, default): dithered black blob (texture SHDWBLOB) on the floor below the actor, always the same strength, shrinking with the height above the floor (gl_model_shadow_fade_height). Style 0 is the old flat silhouette. Which actors get a shadow is still chosen with gl_model_shadow_monsters / _decor / _player.

- Soft shadow is now a smooth black-to-transparent gradient (no dither pattern), drawn with alpha blending and linear filtering.

- Shadow style 2 "Sun": in sectors with a sky ceiling the actor casts a projected (sheared, flat black) shadow on the floor, as if the sun stood above the middle of the map (bounding box of all vertices; height gl_model_shadow_sun_height, 0 = automatic = half the map size, at least 1024). Actors in sectors without sky get the soft blob. A flying actor's shadow is displaced by its height.

- Fix: the soft shadow blob left the render style on "translucent" (and changed depth mask / alpha threshold): following models blended, did not write the normal buffer and map outlines showed through them. The blob now restores the render style, texture mode and alpha threshold and no longer touches the depth mask.

- Soft blob and sun shadow removed. New shadow style 1 "Light source" (gl_model_shadow_style, default 1): the flat black shadow is projected onto the floor away from the dynamic light that shines on the actor most (GLDEFS / actor lights; lights attached to the actor itself and lights below the floor are ignored). Without a light, or with style 0, the shadow lies straight below the actor and shrinks with the height above the floor (gl_model_shadow_fade_height).

- Model drop shadow: the player now also casts his shadow in first person (gl_model_shadow_player). The player model itself is still not drawn, only the shadow.

- Scene shadows (gl_scene_shadow): screen space shadows of a sun (gl_scene_shadow_sun_angle / _elevation) and up to 3 dynamic lights (gl_scene_shadow_lights) on the map geometry and models. New post pass shaders/pp/sceneshadow.fp, runs before the outlines. Hard black or 1 bit dither (gl_scene_shadow_style); walls facing away from the light are black (gl_scene_shadow_facing). Only what is on screen casts shadows, ceilings never do.

- Scene shadows are now independent of the view: the level is rasterized into a height map (floor / ceiling height per cell, built from the sectors on level load, updated for moving doors and lifts, hw_shadowheightmap.cpp). The shadow pass rebuilds the world position of each pixel from the depth buffer and marches a ray through the height map towards the sun / the dynamic lights. The sun only shines under an open sky, solid ceilings and walls block it. gl_scene_shadow_lights adds one pass per light.

- Scene shadows: lines with a transparent middle texture (chain link fence, grates) now cast the silhouette of their texture. The alpha masks of those textures are packed into an atlas (64x64 per texture) when the level loads, and the shadow rays test against them.

- Scene shadows: sharp edges. The shadow ray is now walked cell by cell through the height map (exact grid traversal, no fixed steps and no jitter, so nothing frays or bands), and the grid is finer (2 map units for levels up to about 2000 units, otherwise 4 / 8 / ...). gl_scene_shadow_steps is gone.

- Scene shadows: exact edges. The shadow rays are tested against the exact line segments of the map (every line that can block: one sided walls, height steps, fences, lines of tagged sectors), cell by cell, so shadows are straight lines that start exactly at the corners of the geometry. The grid cells only store floor / ceiling heights for cells without lines. Moving sectors refresh the cells and lines of the sector.

- Model drop shadows follow the sun of the scene shadows (gl_scene_shadow + gl_scene_shadow_sun, actor under an open sky, shadow style "Light source"): the shadow is projected away from the sun, not straight down.
- Scene shadows: pixels right at a wall base are now classified like the open ground next to them (solid cells near an open sky carry a marker). This removed the notch in the shadow edge at the foot of walls.
- Scene shadows: the sun is now a point light above the centre of the level (bounding box of all vertices). Its height is set in map units (gl_scene_shadow_sun_height, default 2048); gl_scene_shadow_sun_angle / _elevation / _length are gone. Every pixel looks at the sun from its own position; model drop shadows use the same point.
- Scene shadows: transparent middle textures (fences) use the same scale / offset rules as the wall renderer (texture scale, world panning, row offset, pegging), the back side texture is used when the front side has none, and the alpha atlas is now 128x128 per texture.
- Scene shadows: sectors with a textured ceiling more than 512 units above the floor count as open for the sun; the ground / walls next to them are no longer skipped. Rays stop above the highest floor / ceiling of the level (faster, long shadows no longer cut off at 400 units).
- Scene shadows: surfaces within 64 units of a dynamic light (the light source and its fixture) are never shadowed by that light, and the light ray stops 24 units before the light, so lamps no longer turn black.
- Scene shadows: walls are only as high as the ceiling of their sector when that ceiling is open (sky): the sun passes above them, outdoor areas far from the sun are no longer black. Solid space (outside all sectors) is as high as the nearest sector, not infinite.
- Scene shadows: sun and dynamic lights are evaluated together in one pass. A pixel is black only if every source that reaches it is blocked, so lights no longer paint black over sunlit areas (and a light no longer blackens what another light lights).
- Scene shadows: the sun hangs above the map origin (0, 0), the cross of the map editor, instead of the bounding box centre. Default height 1024 (shadow length = object height * distance to the origin / sun height, so a very high sun gives very short shadows).
- Scene shadows: floors and ceilings are snapped to their exact height from the height map before the shadow rays start (removes the moire stripes along shadow edges that came from the imprecise depth buffer at grazing angles). Model shadows use the sun in the same sectors as the map shadows (sky or a ceiling 512+ units above the floor).
- Scene shadows: gl_scene_shadow_fences switches the fence (transparent texture) shadows off; floors and ceilings use an exact up / down normal for the facing test.
- Scene shadows: the lattice shadow of fences only counts within 128 units of the receiver (farther away the fine mesh aliased into moire rings), and models / sprites no longer receive the lattice of fences.
- Scene shadows: 3D models get a normal derived from the neighbouring depth values, so shadows on them work like on the map (surface facing away from the light = black, cast shadows incl. fence lattice). The fence alpha atlas keeps thin wires in the silhouette (any opaque pixel of a block counts) so the lattice of the shadow matches the texture.
- Scene shadows: the floor / ceiling position snap now works up to 16 units of depth error and is repeated 3 times, so far away floors at grazing angles get an exact position (speckle noise along shadow edges and in fence lattice shadows).
- Scene shadows: wall pixels are snapped onto the plane of their map line along the exact view ray (and take the exact line normal), like floors, so the edges of shadows on walls no longer fray.
- Scene shadows (OpenGL): the Doom 3 algorithm. Walls and ceilings are extruded away from every light (the sun and up to 3 dynamic lights) into stencil shadow volumes (depth fail / "Carmack's reverse"), drawn into the stencil buffer of the scene with the real depth buffer, so the edges are pixel exact. Pixels reached by a source but lit by none are painted black; the sun does not reach pixels under a ceiling. gl_scene_shadow_volumes (default on) switches between this and the older height map ray march (used for Vulkan, multisampling, or when off). Not in the volume version yet: fence (transparent texture) shadows and shadows cast by models.

- Stencil shadow volumes: debug cvar `gl_scene_shadow_debug` (1 = red: black pixels, blue: roofed; 2 = green: pixels inside a volume of the last source); floor/ceiling normals snapped in the facing test.

- Neustart der Modellschatten: der Stencil-Schattenvolumen-Pfad (Doom-3-Algorithmus) wurde entfernt (er zählte auf der GPU falsch, alles schwarz), der Ray-March-Pfad für die Map-Schatten bleibt. Neu: Shadow Maps nur für Models (OpenGL). Die Models werden nach der Szene noch einmal, nur Tiefe, aus Sicht der nächsten dynamischen Map-Lichter (Sector Lights, ...) in eine 1024er Tiefentextur gerendert (hw_modelshadow.cpp, gl_modelshadow.cpp); `modelshadow.fp` malt jedes Pixel (Boden, Wände, Models) rein schwarz, vor dem ein Model zum Licht liegt. Cvars `gl_model_shadow_map` (an) und `gl_model_shadow_map_lights` (1-2). Die planare Bodenprojektion zum nächsten Licht entfällt dann; Sonne und Ray-March bleiben unverändert.

- Modellschatten (Shadow Map): die einfache Bodenprojektion entfällt jetzt wirklich, sobald ein Map-Licht den Actor erreicht (vorher blieb der flache "Schatten direkt unter dem Actor" übrig); die Schattenwerfer kommen jetzt von allen Actors in Reichweite der Lichter statt nur von den gerade gezeichneten Modellen, damit der Wandschatten nicht verschwindet, wenn das Modell aus dem Bild läuft.

- Modell-Schattenkarte: die Lichter werden jetzt unabhängig von der Kamera gewählt (alle aktiven Punktlichter der Map, das jeweils nächste zu einem Modell zuerst), statt der drei der Kamera nächsten; dadurch verschwindet der Schatten nicht mehr, wenn man sich bewegt und ein anderes Licht näher an der Kamera liegt.

- Cvar `gl_model_shadow_map_debug`: meldet in der Konsole, wenn sich ändert, wie viele Lichter / schattenwerfende Modelle / Schattenkarten es gibt.

- Modell-Schattenkarte: von den Lichtern, die ein Modell erreichen, kommt das dem Spieler nächste zuerst; es zählen nur Modelle bis 4096 Einheiten vom Spieler. (Vorher konnten zwei Lichter irgendwo in der Map die beiden Karten belegen.)

- `gl_model_shadow_map_debug` meldet jetzt ausführlicher (warum Modelle ausscheiden: zu weit vom Spieler, außerhalb jeder Lichtreichweite, kein 3D-Modell; Position und Radius des Lichts jeder Schattenkarte) und bei jedem Wechsel, auch wenn keine Karte entsteht.

- Modell-Schattenkarte: Lichter, die niedriger als `gl_model_shadow_map_light_height` (Standard 128) über dem Boden ihres Sektors hängen, werfen die Modellschatten, als hingen sie so hoch (nie über der Decke). Ein Sector Light auf Bodenhöhe (hier: Höhe 16) beschattet sonst nur die Beine der Modelle und wirft nur streifende Schatten, die je nach Blickposition kaum zu sehen sind.

- Modell-Schattenkarte: die Entfernungsgrenze zum Spieler (4096) entfällt; schattenwerfend sind alle Modelle in Reichweite eines Lichts (höchstens 128).

- Modell-Schattenkarte: die Lichthöhe kommt wieder allein aus der Map (Cvar `gl_model_shadow_map_light_height` entfernt). Die Blickrichtung der Karte hängt nicht mehr vom Spieler-Modell ab (es lenkt Achse und Sichtfeld nicht, nur andere Modelle), das Sichtfeld ist auf 130° begrenzt, Modelle außerhalb werden weggelassen, Hüllkugeln der Modelle sind großzügiger (Arme/Waffen), damit Schatten nicht abgeschnitten werden.

- modelshadow.fp: die Texelgröße der Schattenkarte (für die Toleranz beim Tiefenvergleich) wurde aus `VP[0].x` berechnet, das von der Blickrichtung des Lichts abhängt und gegen 0 gehen kann: dann wurde die Toleranz riesig und Teile des Schattens (oder alles) verschwanden. Jetzt aus der Länge der Zeile (= 1 / tan(fov/2)).

- Neu: `gl_model_shadow_map_range` (0.5..4, Standard 1): Reichweite der Modellschatten als Vielfaches des Lichtradius. Menü: Slider "reach of model shadows (x light radius)".

- `gl_model_shadow_map_range` ersetzt durch `gl_model_shadow_map_unlimited` (Standard an): Modellschatten sind nicht mehr auf den Lichtradius begrenzt (Reichweite 8192). Der Schatten kann dadurch auch durch Wände auf dahinterliegende Flächen fallen.

- Modellschatten: ein Model wirft nur für Lichter einen Schatten, in deren echtem Radius es steht (nicht mehr für alle Lichter, wenn die Schattenlänge unbegrenzt ist). Die Länge des Schattens bleibt unbegrenzt.

- Modellschatten: statt eines Sichtfelds, das sich je Frame an die Models anpasst (Flimmern, Models fielen heraus), hat jedes Licht jetzt eine feste Karte in 6 Richtungen (wie eine Cubemap, Atlas 3x2 Kacheln à 1024). Jedes Model wird nur in die Kacheln gezeichnet, die es berührt. Weniger Zeichenarbeit bei vielen Models, kein Springen mehr. modelshadow.fp rechnet die Kachel und Position analytisch aus (geprüft gegen VSMatrix).

- Modellschatten: bis zu 4 Lichter mit eigener Karte (vorher 2), Standard `gl_model_shadow_map_lights 4` (Menü-Slider 1-4). Die Auswahl, welche Lichter eine Karte bekommen (wenn mehr als 4 ein Model erreichen), hängt nicht mehr von der Entfernung zum Spieler ab, sondern von der Zahl der erreichten Models. Kachelgröße 768.

- Modellschatten gehen nicht mehr durch Wände: modelshadow.fp marschiert für jedes Pixel mit Modellschatten den Strahl zum Licht durch die Höhenkarte des Levels (Trace/SnapWall aus sceneshadow.fp, ohne Zäune). Liegt eine Wand, ein Boden oder eine Decke dazwischen, zählt das Licht für dieses Pixel nicht. Neue Eingaben: CellTex/ListTex/LineTex (Bindings 2-4), Schattenkarten jetzt auf 5-8, Uniform `Grid`.

- Modellschatten: ein Model, das vom Licht aus (P_CheckSight vom Lichtobjekt) hinter einer Wand liegt, wirft für dieses Licht keinen Schatten mehr.

- Neu: `gl_model_shadow_map_walls` (Standard 1, nicht gespeichert): schaltet den Wandtest der Modellschatten ab, um Versionen zu vergleichen.

- Mit `gl_model_shadow_map` an gibt es den flachen Bodenschatten (Sonne / senkrecht / vom nächsten Licht projiziert) nicht mehr: er lief als langes Band durch Wände. Es werfen nur noch die Lichter der Map Schatten (Schattenkarten).

- Fix: `gl_model_shadow_map_walls` hing versehentlich am Kartenschatten-Shader (Boden wurde bei 0 schwarz) statt am Modellschatten-Shader. Neu: `gl_model_shadow_map_debug_walls 1` zeichnet Pixel, an denen ein Model im Weg ist, aber eine Wand den Schatten stoppt, als Schachbrett.

- modelshadow.fp: beim Entfernen der Zaun-Texturen aus LinesBlock blieb ein `return true;` übrig, das bei jeder gekreuzten Linie blockierte (auch bei offenen Sektorgrenzen). Jetzt blockieren nur Wände (außerhalb der Durchgangshöhe) und Zäune (Mid-Texturen) den Modellschatten.

- Modellschatten: Flächen, die vom Licht abgewandt sind (Rückseite einer Wand, Boden von unten …), bekommen keinen Modellschatten dieses Lichts mehr (Normale gegen Richtung zum Licht, wie bei den Kartenschatten).

- Neu: `gl_model_shadow_map_debug_light 1..4` zeichnet ein Schachbrett auf alles, was das Licht der jeweiligen Schattenkarte nicht sieht (Wandtest + abgewandte Flächen); 0 = aus. Zeigt, ob die Sichtbarkeit an Wänden stimmt.

- Fix (Ursache der durchgehenden Schatten): Die Uniform-Felder des Modellschatten-Shaders werden in der Reihenfolge der Beschreibung (`Desc()`) deklariert, die Struktur lag aber in anderer Reihenfolge (Grid / NearFar2 / Flags vertauscht). Dadurch las der Shader als Gitter die Near/Far-Werte und der Wandtest lief mit Unsinn. Reihenfolge angeglichen.

- `gl_model_shadow_map_debug_light 5`: Testmodus, die linke Bildschirmhälfte wird schwarz (prüft, ob der Modellschatten-Shader mit den aktuellen Werten läuft).

- Modellschatten, neuer Ansatz für die Wände: Zu jeder Modell-Schattenkarte gibt es jetzt eine Welt-Karte (gleicher Cubemap-Atlas), in die die Level-Geometrie aus dem Level-Mesh der Engine (Wände, Böden, Decken) aus Sicht des Lichts gerendert wird (nur wenn sich Licht oder Level ändern). `modelshadow.fp` prüft pro Pixel, ob das Licht ihn sieht (nichts Näheres in der Welt-Karte) und lässt nur dann den Schatten eines Models zu. Eine Wand begrenzt damit die Reichweite des Schattens. Der Strahltest durch die Höhenkarte ist entfernt. Grenzen: das Level-Mesh ist statisch (Türen/Aufzüge sind in der Ausgangshöhe), nur OpenGL.

- Diagnose: Konsolenbefehl `gl_model_shadow_info` gibt Build-Zeit der Exe, Anzahl der Shader-Aufrufe und den Zustand der Modell-/Welt-Schattenkarten aus.

- Testmodus `gl_model_shadow_map_debug_light 5` läuft jetzt auch ohne aktive Lichter/Modelle (vorher kehrte der Shader dann sofort zurück und der Test zeigte nichts).

- Modellschatten: Flächen, die das Licht nur streift (Winkel zur Lichtrichtung unter ca. 1°; ein größerer Wert schnitt lange Schatten ab), bekommen keinen Modellschatten mehr, weil der Tiefenvergleich dort instabil ist und flimmerte (z. B. Oberseite einer erhöhten Sektorfläche auf Lichthöhe).

- Modellschatten: niedrige Hindernisse (Stufen, flache Erhöhungen; Blockierpunkt weniger als 56 Einheiten über dem Pixel dahinter) unterbrechen den Schatten nicht mehr; echte Wände beenden ihn weiterhin.

- `gl_model_shadow_info` listet jetzt Position (x, y, Höhe) und Reichweite jeder Schattenkarte, damit man sieht, welche Lampe welche Karte (`gl_model_shadow_map_debug_light 1..4`) hat.

- Modellschatten: Länge begrenzt, Schatten bleibt ganz (CVar `gl_model_shadow_max_length`, Standard 512, 0 = aus). Ursache der fehlenden Teile: Liegt die Lampe niedriger als der Kopf des Models (z. B. Lampenhöhe 16 oder 64), fällt der Schatten der oberen Körperteile nie auf den Boden, sondern ins Unendliche. Jetzt wird das Model in der Schattenkarte zu den Füßen hin gestaucht (Faktor aus Lichthöhe, Modellhöhe und Abstand), so als wäre die Lampe höher: Der Schatten reicht dann höchstens ca. 512 Einheiten und ist vom Fuß bis zum Kopf vollständig.

- Modellschatten: Am Rand des Wirkradius einer Lampe (letzte 30 %) schrumpft der Schatten des Models zu den Füßen hin zusammen, statt schlagartig zu verschwinden.

- Modellschatten: Models, die in diesem Bild keinen Schatten aus einer Lichtkarte bekommen (außerhalb jedes Wirkradius, hinter einer Wand von der Lampe aus, Schattenkarten aus), haben wieder den einfachen flachen Schatten von früher (Stil und Einstellungen wie `gl_model_shadow_style`, Sonne usw.). Models mit Lichtkarten-Schatten haben ihn weiterhin nicht. Der Wechsel hinkt ein Bild nach.

- Modellschatten: fließender Übergang zwischen Lichtkarten-Schatten und einfachem flachen Schatten. Am Rand des Wirkradius schrumpft der Karten-Schatten, während der flache Schatten im gleichen Maß wächst (statt harter Umschaltung).

- Modellschatten: nie zwei Schatten pro Actor. Ein Model mit Lichtkarten-Schatten hat keinen flachen Schatten; zum Rand des Wirkradius wird es in der Schattenkarte immer weiter zu den Füßen hin gestaucht, sein Schatten wird zum flachen Schatten direkt unter dem Model, und dieser übernimmt, sobald das Model den Radius verlässt. (Ersetzt das Überblenden von vorhin.)

- Zaunschatten: die feste Grenze von 128 Einheiten ist weg. Neu `gl_scene_shadow_fence_range` (Standard 0 = unbegrenzt): Der Schatten der transparenten Textur (Zaun, Gitter) in Schwarz reicht so weit, wie das Licht ihn wirft, auf Wänden, Boden und Models; nur die Länge auf dem Boden hängt von der Lichtquelle ab. Ein Wert > 0 begrenzt die Reichweite wieder (gegen Moiré in der Ferne).

- Zaunschatten: kein Rauschen mehr und nicht auf Decken. Der Gitterschatten wird nur dort berechnet, wo das Gitter auflösbar ist (Fußabdruck des Pixels am Zaun höchstens zwei Texel der Zaun-Textur; der Texel ergibt sich aus der Skalierung der Textur, gilt also für 128 und 256 Pixel hohe und skalierte Texturen), sonst entfällt der Gitterschatten statt zu flimmern. Auf Decken (Normale nach unten) gibt es keinen Zaunschatten mehr. Die Alpha-Maske der Zaun-Texturen ist jetzt der Mittelwert pro Block (Texturen über 128 Pixel werden heruntergerechnet), 20 % Deckung zählen als Draht.

- Zaunschatten, Überarbeitung: (1) Skalierung: der Schatten ist die Zaun-Textur in Schwarz in Originalgröße (Parallelprojektion entlang der Richtung Zaun → Licht, statt Perspektive vom Pixel zum Licht, die das Muster vergrößerte). Nur die Länge auf dem Boden hängt vom Licht ab. (2) Vollständig: kein Abschneiden mehr in der Ferne. Wo das Gitter feiner als ein Pixel wird, entscheidet die mittlere Deckung der Textur (ab 25 % durchgehend schwarz, sonst kein Schatten) statt eines Rauschens. (3) Das Textur-Raster-Rauschen ist damit weg, Schatten sind überall reines Schwarz.

- Zaunschatten: genauer. Die Alpha-Maske jeder Zaun-Textur hat jetzt 256 x 256 Texel (vorher 128 x 128; 8 x 8 Slots = bis zu 64 verschiedene Zaun-Texturen pro Karte) und wird bilinear abgetastet (glatte Kanten statt Blöcke). Das Texel-Maß für die Auflösungsprüfung stimmt mit der neuen Größe überein. Maße und Skalierung (z. B. 256 x 512 Pixel mit Skalierung 2) kommen wie beim Zaun selbst aus der Textur und den Skalierungen der Seite.

- Zaunschatten: der Ersatz „durchgehend schwarz“ für Gitter, die feiner als ein Pixel sind, gilt nur noch bei Pixelgröße > 1,5 Texel der Zaun-Textur (vorher mit Streckfaktor für flache Blickwinkel, wodurch ganze Böden schwarz wurden).

- Zaunschatten in der Ferne: keine schwarze Fläche mehr. Ist das Gitter feiner als ein Pixel, wird die Deckung des Pixel-Fußabdrucks (bis 4 x 4 Abtastungen, bei sehr großem Fußabdruck die mittlere Deckung der Textur) mit der Bayer-Rasterung des Bildschirms als regelmäßiges Grau dargestellt, nah bleibt es das exakte Gitter in Schwarz.

- Zaunschatten: Ober- und Unterkante stimmen mit der Zaun-Textur überein. Die Projektion startete bisher vom angehobenen Strahlursprung (1,5 Gitterzellen über der Fläche, dadurch um bis zu ein paar Dutzend Einheiten nach oben versetzt); jetzt startet sie von der exakten Position des Pixels auf der Fläche.

- Zaunschatten: Der Schatten bildet jetzt den ganzen Zaun ab (Unterkante bis Oberkante). Liegt die Lichtquelle tiefer als die Zaunoberkante, wurde die Oberkante bisher ins Unendliche bzw. unter den Boden geworfen. Die Projektionsrichtung hat jetzt einen Mindest-Höhenwinkel (Schattenlänge höchstens 2x Zaunhöhe).

- Zaunschatten: Seitlich fächert der Schatten jetzt wie eine echte Projektion von der Lichtquelle auf (Strahl Pixel -> Licht bestimmt die Stelle am Zaun); die Höhe (Länge auf dem Boden, Ende = Zaunoberkante) bleibt wie bisher.

- Neue Schattenoption: `gl_scene_shadow_style 2` stellt Szenen-, Zaun- und Modellschatten als Schachbrettmuster dar (jedes zweite Pixel schwarz, die anderen durchsichtig) statt als geschlossene schwarze Fläche. 0 = schwarz, 1 = Dither mit Stärke, 2 = Schachbrett.

- Zaunschatten: Die Gitterkanten sind scharf (Schwelle 0,5), solange das Gitter aufgelöst werden kann; der geordnete Dither blendet erst zusammen mit der mittleren Deckung in der Ferne ein. Das behebt die streifenweise körnigen Kanten (v.a. Boden, Vulkan).

- Vulkan: Modellschatten (Shadow Maps) auch im Vulkan-Backend. Neue Datei `vulkan/textures/vk_modelshadow.cpp/.h`: Modell-Tiefenatlanten (4 x 2304x1536, über den normalen Render-State), Level-Tiefenatlanten (eigene kleine Tiefen-Pipeline, Level-Mesh als Vertex-/Index-Buffer, Cache nach Licht/Mesh wie in OpenGL), die Texturen für den Post-Process (`PPTextureType::ModelShadow0..3`, `ModelWorld0..3`). `VkRenderState` bekam `PushRenderTarget/PopRenderTarget/FlushClearPass`, um mitten in der Szene in die Atlanten zu rendern und danach Ziel, Viewport und Scissor wiederherzustellen. Die Uniform-Struktur des Modellschatten-Shaders wurde verkleinert (336 -> 192 Byte; nicht benutzte Felder Grid und VP0/VP1 entfernt), weil Uniforms in Vulkan Push-Konstanten sind.

- Neu: `gl_scene_shadow_ambient` (0..1, Standard 0 = wie bisher): die Sektorhelligkeit schwächt die Schatten. Bei 1 zeigt ein Sektor mit Helligkeit 255 gar keinen Schatten mehr, Schatten gibt es nur in dunklen Sektoren (harte Schatten unterhalb von 50 % Helligkeit; beim Dither-Stil skaliert die Deckung mit der Restdunkelheit). Gilt für Szenen-, Zaun- und Modellschatten. Technik: jede Gitterzelle der Höhenkarte kennt ihren Sektor (im Zähler-Kanal oberhalb der unteren 7 Bit), eine kleine Textur mit der aktuellen Helligkeit jedes Sektors (neu bei Änderung, also auch bei flackernden Lichtern) wird als zusätzlicher Eingang (Szene: Binding 8, Modell: 12) gebunden.
- Neu: Modelle in einem voll ausgeleuchteten Sektor (gleiche Regel wie `gl_scene_shadow_ambient`, bei 1 also Helligkeit ≥ 128) bekommen nur noch den einfachen flachen Schatten gerade unter sich: keine Projektion von Sonne oder Licht und kein Lichtkarten-Schatten (`RenderModel` in `models.cpp`). Bei `gl_scene_shadow_ambient 0` unverändert.
- Neu: `gl_model_shadow_map_sun` (Standard an): die Sonne (`gl_scene_shadow_sun`) wirft auch Schatten der Modelle auf Wände und andere Modelle. Die Sonne ist dafür ein zusätzliches Licht in den Modell-Schattenkarten (über der Levelmitte, Reichweite 8192) und bekommt immer die erste der 4 Karten; die Modell-Lichter teilen sich den Rest (mit `gl_model_shadow_map_lights` 4 also nur noch 3 Lichter + Sonne). Empfänger sind nur Pixel unter freiem Himmel (`UnderSky` jetzt auch in `modelshadow.fp`), Böden nicht (dort bleibt der schärfere flache Sonnenschatten). Die Karte der Sonne steht als Zeile 6 in der Datentextur. Technik: `hw_modelshadow.cpp` (Sonne als Licht, zuerst sortiert), `modelshadow.fp`, `SceneShadowFrame::ModelSunMap`.
- Fix: Die Sonnen-Karte hat keine Welt-Tiefenkarte mehr (`hw_modelshadow.cpp`): die Himmelsdecken der Map lagen zwischen Sonne und allem darunter und versteckten alle Empfänger, es kamen keine Modellschatten der Sonne an. Ob ein Pixel unter freiem Himmel liegt, entscheidet `UnderSky` in `modelshadow.fp`.
- Sonnen-Modellschatten: als „Sonne erreicht den Pixel“ zählt in `modelshadow.fp` jetzt auch eine Decke ≥ 512 über dem Boden (nicht nur offener Himmel), wie beim flachen Sonnenschatten auf dem Boden.
- Test-Option `gl_model_shadow_map_sun_anywhere` (Standard aus): die Sonnen-Modellschatten fallen auch auf Pixel ohne freien Himmel darüber. Dazu zeigt `gl_model_shadow_map_debug 1` jetzt, ob die Sonnen-Karte existiert (`sun map: N`). Beides dient der Fehlersuche, falls Modellschatten der Sonne nicht an Wänden ankommen.
- Fix: Reichweite der Sonnen-Karte war fest 8192, bei einer Sonnenhöhe von 8192 lag jeder Pixel außerhalb und bekam nie einen Schatten. Jetzt Reichweite = Sonnenhöhe + 16384, Near-Plane der Sonnen-Karte bis 4096 (Tiefengenauigkeit).
- Fix Zaun an der Kante einer Erhöhung (`sceneshadow.fp`): Für Pixel unterhalb des Zaunfußes (Boden tiefer als der Fuß des Zauns) wird die echte Neigung Zaunfuß → Licht benutzt (Schutz nur noch 0,05 statt 0,5 gegen flaches Licht). Der Zaunschatten setzt dadurch genau dort an, wo der Schatten der Mauer unter dem Zaun endet. Vorher kappte die Mindestneigung die Länge: bei einem Licht knapp über der Erhöhung fehlte der Zaunschatten ganz. Zusätzlich liegt der Strahlenursprung jetzt auf dem echten Licht (vorher bei Lampen 24 Einheiten davor, `gLightWorld`). Mit einer Python-Nachbildung des Querschnitts geprüft (Mauer- und Zaunschatten schließen lückenlos aneinander).
- Sonnen-Modellschatten auf Böden (`modelshadow.fp`): Böden, die höher liegen als der Boden unter dem werfenden Modell (Erhöhungen), bekommen den Schatten jetzt aus der Sonnen-Karte; Böden auf Höhe des Modells behalten den flachen Sonnenschatten.
- Debug: `gl_model_shadow_map_debug 1` zeigt zusätzlich die Lichtquellen der Szenenschatten (`Scene shadows: sun ... at height ...; N dynamic lights: [x y z radius]`), wenn sie sich ändern.
- Debug: Konsolenbefehl `scene_shadow_info`: listet für den Sektor, in dem der Spieler steht, alle Linien mit Höhenstufe oder Zaun (Linie, Nachbarsektor, Böden, Blockierbereich, ob sie in der Linienliste der Schattenkarte ist). Damit lässt sich prüfen, ob z. B. die Kanten einer Box überhaupt im Schatten-Datensatz sind.
- Neu: Position der Sonne einstellbar: `gl_scene_shadow_sun_x` / `gl_scene_shadow_sun_y` (Standard 0 / 0 = Map-Ursprung wie bisher). Bei einer Map weit vom Ursprung (z. B. y = -9600) war die Sonne sehr weit weg und stand flach (bei Höhe 1024 etwa 6°: alles schwarz). Konsolenbefehl `scene_shadow_sun_place [Abstand [Winkel]]` setzt die Sonne vom Spieler aus im Abstand (Standard: Sonnenhöhe, also 45°) in Richtung des Winkels (Standard 135°); die Schatten fallen in die Gegenrichtung. Wirkt auf Szenen-, Zaun- und Modellschatten (alle lesen `SceneShadowFrame::SunPos`).
- Neu: `gl_scene_shadow_caster_height` (Standard 28, 0 = exakte Projektion): Die Schatten der Map-Geometrie (Wände, Boxen, Zäune) werden so berechnet, als läge das Licht um diese Höhe tiefer. Damit haben Map und Modelle die gleiche Schattenlänge: der flache Modellschatten (`models.cpp`) projiziert vom Mittelpunkt des Modells (bei einem 56 hohen Monster 28), die Strahlen der Szenenschatten gingen bisher zur echten Lichthöhe (exakte Perspektive). Die Höhe wandert in `Sun.w` (1/1000), keine Änderung der Uniform-Größe. Gilt für Sonne und Lampen, Boden-, Wand- und Zaunschatten.
- Geändert: Zaunschatten: der scharfe Maschenbereich war nach der Moiré-Änderung zu kurz; der Übergang zum gleichmäßigen Grau (Dither) beginnt wieder später (ab 4 Texel Fußabdruck, voll bei 10). Die genauere Abtastung (echter Fußabdruck, bis 6x6 Proben) bleibt.
- Geändert: Moiré der Zaunschatten (kleine Maschen in der Ferne): die Abtastung nutzt jetzt den echten Fußabdruck des Pixels auf der Fläche (`gFoot`, auf Böden bei flachem Blick viel länger als `gPix`), der Übergang zum gleichmäßigen Grau (Dither) beginnt früher (ab 1.5 statt 4 Texel Fußabdruck, voll bei 4 statt 8) und es werden bis zu 6x6 statt 4x4 Proben genommen.
- Geändert: Die Sonne wirkt nur noch unter offenem Himmel (Sky-Decke). Die Regel "Decke mindestens 512 über dem Boden zählt auch als Außenbereich" ist in den Modellschatten-Shadow-Maps (`modelshadow.fp`) und im flachen Modellschatten (`models.cpp`) entfernt; die Szenenschatten (`sceneshadow.fp`) galten bereits nur für Sky.
- Behoben: Zaunschatten flimmerten durch die neue Linientoleranz hinter dem Strahlstart; die Toleranz gilt jetzt nur noch für Wände (Höhentest), Zäune prüfen wieder streng.
- Geändert: Weiße Pixel am Wandfuß: Linien bis 0.3 Einheiten hinter dem Strahlstart zählen noch (ungenaue Tiefe, ein Pixel kann knapp auf der falschen Seite der Wandlinie liegen); Strahl-Startabstand höchstens 0.5, übersprungenes Stück höchstens 0.02.
- Geändert: Schatten noch dichter an der Wand: Strahl-Startabstand höchstens 0.75 Einheiten, übersprungenes Stück am Strahlanfang höchstens 0.05.
- Geändert: Schatten der Map-Geometrie beginnen noch näher am Wandfuß: Strahl-Startabstand von der Fläche höchstens 1.5 Einheiten (vorher 3), übersprungenes Stück am Strahlanfang höchstens 0.25 (vorher 0.75). Falls Selbstschatten-Flimmern auftritt, wieder erhöhen.
- Behoben: Helle Lücke zwischen Wandfuß (und Zaun) und dem Schatten auf dem Boden: `Trace` übersprang am Strahlanfang `cell * 0.25` (auf großen Maps ca. 8..12 Einheiten), eine näher liegende Wand wurde verpasst. Jetzt höchstens 0.75 Einheiten.
- Behoben: Die Strahlen der Szenenschatten starteten `cell * 1.5` über der Fläche; die Zellgröße hängt von der Mapgröße ab (auf großen Maps 30..50 Einheiten), dadurch gingen die Strahlen über niedrige Wände und Boxen (bis ca. 128) hinweg und nur hohe Türme warfen Schatten. Der Startabstand ist jetzt auf 3 Einheiten begrenzt.
- Neu: Diagnose `gl_scene_shadow_debug 1`: färbt die Szenenschatten-Pixel nach Status der Sonne (blau = Sonne erreicht den Pixel nicht / kein offener Himmel, grün = erreicht und hell, rot = vom Map-Strahl blockiert, gelb = Fläche zeigt von der Sonne weg).
- Geändert: Szenenschatten der Map-Geometrie (`sceneshadow.fp`): Ein Pixel wird schwarz, sobald **eine** erreichende Quelle (Sonne oder Lampe) blockiert ist, wie beim flachen Modellschatten (eine Quelle). Vorher musste **jede** erreichende Quelle blockiert sein; die Box warf dann keinen Bodenschatten, weil ein zweites Licht den Pixel noch erreichte. Ausnahme: Pixel im Umkreis von 64 Einheiten einer Lampe bleiben hell.
- Modellschatten-Shader: Lichter und Near/Far der Karten liegen jetzt in einer kleinen Datentextur (Binding 10) statt in Uniforms: die Uniform-Struktur ist 112 Byte groß und passt damit in die garantierten 128 Byte Push-Konstanten von Vulkan (der Stand davor mit 192 Byte tat das nicht).

- Neu: `gl_light_distance` (0..1, Standard 1 = wie bisher): wie stark die Helligkeit vom Abstand zum Spieler abhängt. Die ZDoom-/Software-Beleuchtung hellt alles in der Nähe der Kamera auf (Sichtweiten-Term `uGlobVis / z`), was im 1-Bit-Look eine weiße Zone um den Spieler ergibt. Bei 0 zählt nur die Helligkeit des Sektors, in dunklen Räumen gelten überall dieselben Verhältnisse. Wirkt auf Wände, Böden und Modelle (OpenGL und Vulkan, es skaliert `mGlobVis` der Viewpoint-Uniforms).

- Schatten nur dort, wo das Licht hinkommt: pro Licht (3 Szenenlichter, 4 Modellkarten) wird jeden Frame vom Sektor des Lichts aus über offene Linien (Öffnung >= 1 Einheit, also nicht durch Wände und geschlossene Türen) bestimmt, welche Sektoren es erreicht (Bitmaske, zusammen mit der Sektorhelligkeit in der Sektor-Textur). Die Shader zeichnen für ein Licht nur in diesen Sektoren Schatten. Vorher waren Wände und Böden in Nachbarräumen innerhalb der Lichtreichweite "im Schatten" der Lampe auf der anderen Seite der Wand. Die Sonne ist davon nicht betroffen.
