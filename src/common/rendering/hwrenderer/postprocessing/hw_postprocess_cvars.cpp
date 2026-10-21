/*
** hw_postprocess_cvars.cpp
**
** Postprocessing framework
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Copyright 2016-2020 Magnus Norddahl
**
** SPDX-License-Identifier: Zlib
**
**---------------------------------------------------------------------------
**
*/

#include "hw_postprocess_cvars.h"
#include "v_video.h"

//==========================================================================
//
// CVARs
//
//==========================================================================
CVAR(Bool, gl_bloom, false, CVAR_ARCHIVE);
CUSTOM_CVAR(Float, gl_bloom_amount, 1.4f, CVAR_ARCHIVE)
{
	if (self < 0.1f) self = 0.1f;
}

CVAR(Float, gl_exposure_scale, 1.3f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_min, 0.35f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_base, 0.35f, CVAR_ARCHIVE)
CVAR(Float, gl_exposure_speed, 0.05f, CVAR_ARCHIVE)

CUSTOM_CVAR(Int, gl_tonemap, 0, CVAR_ARCHIVE)
{
	if (self < 0 || self > 5)
		self = 0;
}

CVAR(Bool, gl_lens, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CVAR(Float, gl_lens_k, -0.12f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_lens_kcube, 0.1f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_lens_chromatic, 1.12f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, gl_fxaa, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0 || self >= IFXAAShader::Count)
	{
		self = 0;
	}
}

CUSTOM_CVAR(Int, gl_ssao, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0 || self > 3)
		self = 0;
}

// Jupiter3D: screen space outlines
CUSTOM_CVAR(Int, gl_outline, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0 || self > 2)
		self = 0;
}

// Models beyond a distance get a 1 pixel outline on the inside of their silhouette.
CUSTOM_CVAR(Bool, gl_model_inner_outline, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {}

// Screen space shadows (sun + dynamic lights) on the map geometry and models, see sceneshadow.fp
CUSTOM_CVAR(Bool, gl_scene_shadow, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {}
CVAR(Bool, gl_scene_shadow_sun, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVARD(Bool, gl_scene_shadow_debug, false, 0, "Scene shadow test (pure black patterns): horizontal lines = the sun blocks the pixel, vertical lines = the lamps block it (none lights it), solid = both, dots = a source reaches the pixel and it is lit, nothing = no source reaches it")
CVARD(Float, gl_scene_shadow_caster_height, 28.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Shadow length of the map geometry: it is projected as if the light were this much lower (like the flat shadow of a model, which is projected from its centre; 28 = centre of a 56 high monster, 0 = exact projection)")
CVARD(Float, gl_scene_shadow_sun_x, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Position of the sun (map X). 0 / 0 = the origin of the map; for a map far away from the origin use scene_shadow_sun_place")
CVARD(Float, gl_scene_shadow_sun_y, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Position of the sun (map Y)")
CVARD(Float, gl_scene_shadow_sun_height, 1024.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Height of the sun in map units. The sun is a point light above the map origin (0, 0); lower = longer shadows")
CVARD(Bool, gl_model_shadow_map, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "OpenGL: models cast shadows on floors, walls and models from the dynamic lights of the map (shadow maps)")
CVARD(Bool, gl_model_shadow_map_walls, true, 0, "Model shadows stop at walls (ray test through the height map of the level); 0 = switch the test off (for comparing)")
CVARD(Int, gl_model_shadow_map_debug_light, 0, 0, "Model shadows: 6 = sun map: colour the pixels inside the area of the sun map (blue = no open sky above, green = sun map sees nothing, red = a model shadows the pixel, magenta = no sun map this frame), 5 = paint the left half of the screen black (test that the shader runs), 1..4 = draw a checkerboard on everything the light of that shadow map does not see (wall test and facing), 0 = off")
CVARD(Bool, gl_model_shadow_map_debug_walls, false, 0, "Model shadows: pixels where the wall test stops a shadow are drawn as a checkerboard")
CVARD(Bool, gl_model_shadow_map_debug, false, 0, "Print what the model shadow maps do (lights, models, maps) when it changes")
CVARD(Float, gl_model_shadow_sun_size, 768.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Sun model shadows: half the edge length of the square area around the player that gets sun shadows from models (map units); smaller = sharper")
CVARD(Bool, gl_model_shadow_map_sun, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "The sun (gl_scene_shadow_sun) casts shadows of the models onto walls and other models; uses one of the four model shadow maps")
CVARD(Bool, gl_model_shadow_map_sun_anywhere, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Test: the sun model shadows also fall on pixels that are not under an open sky")
CVARD(Bool, gl_model_shadow_map_unlimited, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Model shadows are not limited by the radius of the light (long shadows)")
CVARD(Int, gl_model_shadow_map_lights, 4, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Number of dynamic lights (1..4) whose light the models block")
CVARD(Int, gl_scene_shadow_lamps, 16, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Number of dynamic lights (0..16) that cast screen space shadows")
CVARD(Float, gl_scene_shadow_strength, 0.6f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "How dark the shadows are (0..1)")
CVARD(Int, gl_scene_shadow_style, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "0 = hard black shadow, 1 = binary dither pattern with gl_scene_shadow_strength as coverage, 2 = black/transparent checkerboard (every second pixel)")
CVARD(Bool, gl_scene_shadow_fences, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Fences (transparent middle textures) cast the silhouette of their texture")
CVARD(Bool, gl_scene_shadow_facing, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Surfaces that face away from the light are in shadow as well")
CVAR(Float, gl_model_inner_outline_distance, 512.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// map units

CVARD(Int, gl_outline_width, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Map outline width in render pixels (1..4)")
CVAR(Float, gl_outline_depth, 0.15f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// depth discontinuity sensitivity (lower = more lines)
CVAR(Float, gl_outline_normal, 50.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// crease angle in degrees, 0 = off
// White lines on dark (black) surfaces: surfaces darker than the level (0..1 brightness) get a white outline.
// Of two close parallel map lines (1-2 pixels apart) only one is drawn.
CVAR(Bool, gl_outline_dark, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_outline_dark_level, 0.15f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_outline_range, 2048.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// max distance in map units, 0 = unlimited

// Pixel size for the player's 3D weapon only (2 = every 2x2 screen pixels, ...), 0 = off.
CVARD(Int, gl_weapon_pixel, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Pixel size of the player's 3D weapon (0 = off, 2 and up = pixel size)")
// Weapon: contour outwards in pixels (0 = off).
CVARD(Int, gl_weapon_outer_outline, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Outer contour of the player's 3D weapon in pixels (0 = off)")
bool hudModelOutlineAllowed = false;	// a HUD model without forcecullbackfaces was drawn this frame (the weapon outline pass only runs then)
bool hudModelDrawn = false;	// set by EndDrawScene when a HUD model was drawn, read by the weapon pixel pass
CVAR(Float, gl_outline_alpha, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Color, gl_outline_color, 0x000000, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, gl_ssao_portals, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0)
		self = 0;
}

CVAR(Float, gl_ssao_strength, 0.7f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, gl_ssao_debug, 0, 0)
CVAR(Float, gl_ssao_bias, 0.2f, 0)
CVAR(Float, gl_ssao_radius, 80.0f, 0)
CUSTOM_CVAR(Float, gl_ssao_blur, 16.0f, 0)
{
	if (self < 0.1f) self = 0.1f;
}

CUSTOM_CVAR(Float, gl_ssao_exponent, 1.8f, 0)
{
	if (self < 0.1f) self = 0.1f;
}

CUSTOM_CVAR(Float, gl_paltonemap_powtable, 2.0f, CVAR_ARCHIVE | CVAR_NOINITCALL)
{
	screen->UpdatePalette();
}

CUSTOM_CVAR(Bool, gl_paltonemap_reverselookup, true, CVAR_ARCHIVE | CVAR_NOINITCALL)
{
	screen->UpdatePalette();
}

CVAR(Float, gl_menu_blur, -1.0f, CVAR_ARCHIVE)

CVAR(Float, gl_model_shadow_max_length, 512.f, CVAR_ARCHIVE)

CVARD(Float, gl_scene_shadow_fence_range, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "How far from the surface that receives it the shadow of a fence (transparent middle texture) reaches, 0 = unlimited (only the light decides the length)")
CVARD(Float, gl_scene_shadow_ambient, 0.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "How much the light level of the sector weakens the shadows (0..1): 0 = shadows are always shown, 1 = a sector with light level 255 shows none, shadows only show in dark sectors (hard shadows below 50% light)")
CVARD(Float, gl_light_distance, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "How much the brightness depends on the distance to the player (0..1): 1 = software style, surfaces near the player are brighter; 0 = only the light level of the sector counts, the same everywhere")
