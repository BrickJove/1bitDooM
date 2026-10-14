/*
** actortint.fp
**
** Jupiter3D: recolours the actors with two colours as the very last step (after custom shaders
** such as a 1 bit shader). Actor pixels are the ones with alpha 0, written by actormark.fp
** before the weapon, HUD and menu were drawn (those cover the mark). Bright pixels get the
** light colour, dark pixels the dark colour.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;

void main()
{
	vec4 color = texture(InputTexture, TexCoord);

	if (color.a < 0.05)
	{
		float lum = dot(color.rgb, vec3(0.299, 0.587, 0.114));
		color.rgb = lum >= 0.5 ? vec3(LightR, LightG, LightB) : vec3(DarkR, DarkG, DarkB);
	}

	FragColor = vec4(color.rgb, 1.0);
}
