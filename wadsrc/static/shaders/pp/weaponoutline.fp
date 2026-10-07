/*
** weaponoutline.fp
**
** Jupiter3D: outline for the player's 3D weapon (HUD model) only, independent of the world and
** of every other actor.
**
** The HUD model is drawn last after the depth buffer was cleared, so every pixel whose depth is not
** on the far plane belongs to the weapon. Weapon pixels near the silhouette get a contour line of
** OuterWidth pixels (outwards, on the background). Everything else is copied.
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

// A weapon pixel that is not the hull: the hull is drawn flat in the outline colour, so a weapon
// pixel with exactly that colour counts as hull. Only the solid model gets the inner contour,
// otherwise the contour would just paint over the (already coloured) hull.
bool IsSolid(ivec2 p)
{
	if (!IsWeapon(p))
		return false;
	if (HullActive > 0.5)
	{
		vec3 c = texelFetch(InputTexture, p, 0).rgb;
		if (distance(c, vec3(LineR, LineG, LineB)) < 0.03)
			return false;
	}
	return true;
}

float InvAt(ivec2 p)
{
	float d = texelFetch(DepthTexture, p, 0).x;
	return d * LinearizeDepthA + LinearizeDepthB;
}

void main()
{
	gTexSize = textureSize(InputTexture, 0);
	ivec2 p = ivec2(TexCoord * vec2(gTexSize));
	vec4 color = texelFetch(InputTexture, ClampPos(p), 0);

	// only the background gets the line; weapon pixels (including the hull) stay as they are
	if (IsWeapon(p))
	{
		FragColor = color;
		return;
	}

	bool line = false;

	// contour outwards: background pixels within OuterWidth pixels of the solid model. Hull pixels
	// do not count as solid, so a model that already has a hull is not outlined twice.
	int w = int(OuterWidth);
	for (int j = -w; j <= w && !line && w > 0; j++)
	{
		for (int i = -w; i <= w; i++)
		{
			if (i * i + j * j > w * w)
				continue;
			if (IsSolid(p + ivec2(i, j)))
			{
				line = true;
				break;
			}
		}
	}

	if (line)
		color.rgb = vec3(LineR, LineG, LineB);
	FragColor = color;
}
