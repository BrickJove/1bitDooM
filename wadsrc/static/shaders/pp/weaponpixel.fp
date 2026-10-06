/*
** weaponpixel.fp
**
** Jupiter3D: pixelates only the player's 3D weapon.
**
** The HUD model is drawn last, after the depth buffer was cleared, so every pixel whose depth is
** not on the far plane belongs to the weapon. Every PixelSize x PixelSize block that touches the
** weapon shows the colour of its centre pixel (nearest neighbour downsampling of the whole
** weapon layer, silhouette included). All other pixels are copied unchanged.
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

// nearer than the far plane = part of the weapon
bool IsWeapon(ivec2 p)
{
	float d = texelFetch(DepthTexture, ClampPos(p), 0).x;
	float inv = d * LinearizeDepthA + LinearizeDepthB;
	return inv > (LinearizeDepthA + LinearizeDepthB) * 1.05;
}

void main()
{
	gTexSize = textureSize(InputTexture, 0);
	ivec2 p = ivec2(TexCoord * vec2(gTexSize));

	int n = int(PixelSize);
	ivec2 o = (p / n) * n;

	bool touched = false;
	for (int j = 0; j < n && !touched; j++)
	{
		for (int i = 0; i < n; i++)
		{
			if (IsWeapon(o + ivec2(i, j)))
			{
				touched = true;
				break;
			}
		}
	}

	if (touched)
		FragColor = texelFetch(InputTexture, ClampPos(o + ivec2(n / 2)), 0);
	else
		FragColor = texelFetch(InputTexture, ClampPos(p), 0);
}
