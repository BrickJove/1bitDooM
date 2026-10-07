# 1bitDooM – Änderungsliste (Stand ce736db, 2026-10-07)

Basis: UZDoom trunk 907448a. Alles unten ist der aktuelle Endzustand.

## Fork / Menü
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
- `gl_outline` = 0 Aus / 1 Normal / 2 Erweitert. Erweitert zeichnet zusätzlich die Wand-Sky-Grenze und konkave Wand-Innenecken (1 px). Menü-Auswahl "OutlineMode".

## 3D-Modelle
- Model-Outline (Hull) über Menü (`gl_model_outline`, Breite `gl_model_outline_width` in Karteneinheiten); Hull wird bei Skins mit transparenten Pixeln übersprungen.
- Modell-Schatten: flache schwarze Silhouette am Boden, wählbar für Gegner/Dekoration, Spieler-Schalter, Filter (Shootable/nicht-solide überspringen).
- Modelle über TEXF_Model/NoOutline-Flags im Normalpuffer markiert (OpenGL wie Vulkan).
- Innere Outline für Modelle (`gl_model_inner_outline`, Abstand `gl_model_inner_outline_distance`, Standard 512): 1 px Linie innen an der Silhouette. Modell-Markierung im 2-Bit-Alpha: Alpha 0 + Normale (1,1,1).
- Ferne Monster komplett schwarz ab Abstand (`gl_model_black_distance`).

## Spielerwaffe
- Waffen-Outline als Hülle (Inverted Hull) nur für das HUD-Modell, Dicke in Bildschirmpixeln (`gl_weapon_outline`, `gl_weapon_outline_width` 1-8, `gl_weapon_outline_color`, `gl_weapon_outline_flip`); zeichnet auch Linien zwischen Waffenteilen. HUD-Modelle ignorieren `gl_model_outline`.
- Waffen-Pixelung nur für das HUD-Modell (`gl_weapon_pixel`: Aus, 2, 3, 4, 6, 8), unabhängig von jupiter3d_pixelate.

## Ausprobiert und wieder entfernt
- Outline-Varianten: dünner werdende Linien, Local-Maximum-Suppression, 3-Pass-Dilation, Kontrastkurve.
- Frühe Waffenpixelung (Texturen-Snapping, Alpha-Maske).
- Distanz-Unschärfe (Disc, Gauss), Fern-Texturen schwarz/weiß, halbe Auflösung, ganzzahliger Faktor.
- Modell-Outline in Pixeldicke, Stufen nach Auflösung, fließender Übergang.
- Crash-Fix (Zugriff auf veraltetes Material) vor der Entfernung der Fern-Textur-Features.
