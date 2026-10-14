/*
** actormark.fp
**
** Jupiter3D: marks the pixels of actors (sprites and 3D models) in the alpha channel of the colour
** buffer: alpha 0 = actor, alpha 1 = everything else. This runs before the weapon, the HUD and the
** menu are drawn; whatever is drawn opaque on top writes alpha 1 again, so a pixel that still has
** alpha 0 at the end is an actor pixel that is really visible. actortint.fp reads the mark.
**
** Actors are marked in the normal buffer by surface class (alpha) 0 and a normal with
** x = y = 1: (1,1,0) for sprites, (1,1,1) for models.
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

	bool actor = s.a < 0.1 && s.r > 0.9 && s.g > 0.9;
	FragColor = vec4(color.rgb, actor ? 0.0 : 1.0);
}
