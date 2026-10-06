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
	if (self < 0 || self > 1)
		self = 0;
}

CVAR(Float, gl_outline_width, 1.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// pixels at 1080p, scales with resolution
CVAR(Float, gl_outline_depth, 0.15f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// depth discontinuity sensitivity (lower = more lines)
CVAR(Float, gl_outline_normal, 50.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// crease angle in degrees, 0 = off
CVAR(Float, gl_outline_range, 2048.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)	// max distance in map units, 0 = unlimited

// Pixel size for the player's 3D weapon only (2 = every 2x2 screen pixels, ...), 0 = off.
CVARD(Int, gl_weapon_pixel, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Pixel size of the player's 3D weapon (0 = off, 2 and up = pixel size)")
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
