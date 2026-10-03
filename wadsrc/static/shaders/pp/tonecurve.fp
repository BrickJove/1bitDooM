/*
** tonecurve.fp
**
** Jupiter3D: contrast curve with a pivot. Pushes brightness away from Mid, so fewer
** pixels sit in the grey zone where a 1-bit threshold flickers.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D InputTexture;

void main()
{
	vec4 c = texture(InputTexture, TexCoord);
	vec3 v = (c.rgb - Mid) * Contrast + Mid;
	FragColor = vec4(clamp(v, 0.0, 1.0), c.a);
}
