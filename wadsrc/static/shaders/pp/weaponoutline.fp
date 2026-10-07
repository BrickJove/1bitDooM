/*
** weaponoutline.fp
**
** Jupiter3D: outline for the player's 3D weapon (HUD model) only, independent of the world and
** of every other actor.
**
** The HUD model is drawn last after the depth buffer was cleared, so every pixel whose depth is not
** on the far plane belongs to the weapon. The silhouette of that mask gets a line of Width pixels:
** outside (on the background), inside (on the weapon) or both. Everything else is copied.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;
#if defined(MULTISAMPLE)
layout(binding=1) uniform sampler2DMS DepthTexture;
#else
layout(binding=1) uniform sampler2D DepthTexture;
#endif

ivec2 gTexSize;

ivec2 ClampPos(ivec2 p)
{
	return clamp(p, ivec2(0), gTexSize - ivec2(1));
}

// nearer than the far plane = part of the weapon (pixels outside the screen count as background)
bool IsWeapon(ivec2 p)
{
	if (p.x < 0 || p.y < 0 || p.x >= gTexSize.x || p.y >= gTexSize.y)
		return false;
	float d = texelFetch(DepthTexture, p, 0).x;
	float inv = d * LinearizeDepthA + LinearizeDepthB;
	return inv > (LinearizeDepthA + LinearizeDepthB) * 1.05;
}

float InvAt(ivec2 p)
{
	float d = texelFetch(DepthTexture, p, 0).x;
	return d * LinearizeDepthA + LinearizeDepthB;
}

// Depth step inside the weapon: a pixel that lies behind a nearer part of the weapon (second
// derivative of inverse depth, like the map outline). Drawn on the far side, about Width pixels thick.
// An axis with a background neighbour is skipped: that is the silhouette, handled by Mode.
float InnerEdge(ivec2 p, int w)
{
	ivec2 dx = ivec2(w, 0);
	ivec2 dy = ivec2(0, w);
	float ic = InvAt(p);
	float e = 0.0;
	if (IsWeapon(p + dx) && IsWeapon(p - dx))
		e = max(e, InvAt(p + dx) + InvAt(p - dx) - 2.0 * ic);
	if (IsWeapon(p + dy) && IsWeapon(p - dy))
		e = max(e, InvAt(p + dy) + InvAt(p - dy) - 2.0 * ic);
	return e / max(ic, 1.0e-8);
}

void main()
{
	gTexSize = textureSize(InputTexture, 0);
	ivec2 p = ivec2(TexCoord * vec2(gTexSize));
	vec4 color = texelFetch(InputTexture, ClampPos(p), 0);

	int w = int(Width);
	bool self = IsWeapon(p);

	// Lines between parts of the weapon (hand over gun, ...), independent of the silhouette mode
	if (self && Detail > 0.0 && InnerEdge(p, w) > Detail)
	{
		color.rgb = mix(color.rgb, vec3(LineR, LineG, LineB), LineAlpha);
		FragColor = color;
		return;
	}

	// Mode: 1 = outside, 2 = inside, 3 = both
	bool wantOutside = (Mode == 1.0 || Mode == 3.0) && !self;
	bool wantInside = (Mode == 2.0 || Mode == 3.0) && self;
	if (!wantOutside && !wantInside)
	{
		FragColor = color;
		return;
	}

	// a pixel of the opposite kind within Width pixels makes this pixel part of the line
	bool hit = false;
	for (int j = -w; j <= w && !hit; j++)
	{
		for (int i = -w; i <= w; i++)
		{
			if (i * i + j * j > w * w)
				continue;
			if (IsWeapon(p + ivec2(i, j)) != self)
			{
				hit = true;
				break;
			}
		}
	}

	if (hit)
		color.rgb = mix(color.rgb, vec3(LineR, LineG, LineB), LineAlpha);
	FragColor = color;
}
