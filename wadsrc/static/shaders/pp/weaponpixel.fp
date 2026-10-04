/*
** weaponpixel.fp
**
** Jupiter3D: renders the player weapon as if the game ran at a low base resolution.
**
** The weapon model was drawn into the scene normally and its pixels were marked with
** alpha 0. The screen is divided into blocks of BlockSize pixels. The block centre decides:
**   weapon at the centre -> the whole block shows the centre colour (weapon grows to full blocks)
**   no weapon at the centre -> weapon pixels in that block take the centre colour (background),
**                              all other pixels stay untouched.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;

void main()
{
	ivec2 size = textureSize(InputTexture, 0);
	ivec2 p = clamp(ivec2(TexCoord * vec2(size)), ivec2(0), size - ivec2(1));
	vec4 own = texelFetch(InputTexture, p, 0);

	ivec2 c = clamp(ivec2((floor(vec2(p) / BlockSize) + 0.5) * BlockSize), ivec2(0), size - ivec2(1));
	vec4 centre = texelFetch(InputTexture, c, 0);

	bool ownWeapon = own.a < 0.5;
	bool centreWeapon = centre.a < 0.5;

	vec3 col = own.rgb;
	if (centreWeapon || ownWeapon)
		col = centre.rgb;

	FragColor = vec4(col, 1.0);
}
