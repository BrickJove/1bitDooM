/*
** outline_dilate.fp
**
** Jupiter3D: pass 2 and 3 of the outline effect. Widens the one pixel edge mask
** to LineWidth pixels (separable box, horizontal then vertical), so the line is
** exactly N pixels wide whatever its direction. The vertical pass draws the result.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D EdgeTexture;

void main()
{
	ivec2 size = textureSize(EdgeTexture, 0);
	ivec2 p = ivec2(TexCoord * vec2(size));

	int before = int(Before);
	int after = int(After);
	float m = 0.0;

#if defined(DILATE_V)
	for (int k = -before; k <= after; k++)
		m = max(m, texelFetch(EdgeTexture, clamp(p + ivec2(0, k), ivec2(0), size - ivec2(1)), 0).r);
	FragColor = vec4(LineR, LineG, LineB, m * LineAlpha);
#else
	for (int k = -before; k <= after; k++)
		m = max(m, texelFetch(EdgeTexture, clamp(p + ivec2(k, 0), ivec2(0), size - ivec2(1)), 0).r);
	FragColor = vec4(m, 0.0, 0.0, 1.0);
#endif
}
