/*
** actortint.fp
**
** Jupiter3D: recolours only actors (sprites and 3D models) with two colours, after the whole
** post process chain, so it also works on top of a custom 1 bit shader.
**
** Actors are marked in the normal buffer by surface class (alpha) 0 and a normal with
** x = y = 1: (1,1,0) for sprites, (1,1,1) for models. Bright pixels get the light colour,
** dark pixels the dark colour.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;
#if defined(MULTISAMPLE)
layout(binding=1) uniform sampler2DMS NormalTexture;
#else
layout(binding=1) uniform sampler2D NormalTexture;
#endif

void main()
{
	vec4 color = texture(InputTexture, TexCoord);

#if defined(MULTISAMPLE)
	ivec2 nsize = textureSize(NormalTexture);
#else
	ivec2 nsize = textureSize(NormalTexture, 0);
#endif
	ivec2 p = clamp(ivec2(TexCoord * vec2(nsize)), ivec2(0), nsize - ivec2(1));
	vec4 s = texelFetch(NormalTexture, p, 0);

	if (s.a < 0.1 && s.r > 0.9 && s.g > 0.9)
	{
		float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
		vec3 c = lum >= 0.5 ? vec3(LightR, LightG, LightB) : vec3(DarkR, DarkG, DarkB);
		color = vec4(c, color.a);
	}

	FragColor = color;
}
