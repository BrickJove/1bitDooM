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
