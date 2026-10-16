/*
** modelshadow.fp
**
** Jupiter3D: shadows of the models. The models were rendered into a depth map seen from each of the nearest dynamic
** lights (hw_modelshadow.cpp). For every pixel the shader rebuilds the world position from the depth buffer,
** projects it into the depth map of the light and paints it pure black when a model is nearer to the light.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
layout(binding=2) uniform sampler2D ShadowMap0;
layout(binding=3) uniform sampler2D ShadowMap1;

ivec2 gTexSize;

ivec2 ClampPos(ivec2 p)
{
	return clamp(p, ivec2(0), gTexSize - ivec2(1));
}

float InvDepthAt(ivec2 p)
{
	float d = texelFetch(DepthTexture, ClampPos(p), 0).x;
	return d * ProjParams.x + ProjParams.y;
}

vec3 PosAt(ivec2 p)
{
	vec2 tc = ((vec2(p) + 0.5) / vec2(gTexSize) - Offset) / Scale;
	float z = 1.0 / max(InvDepthAt(p), 1.0e-8);
	vec3 eye = vec3((tc * 2.0 - 1.0) * ProjParams.zw * z, -z);
	return vec3(dot(CamX.xyz, eye) + CamX.w, dot(CamY.xyz, eye) + CamY.w, dot(CamZ.xyz, eye) + CamZ.w);
}

float LinearLightDepth(float d, float n, float f)
{
	return 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n));
}

// true when a model is between the point and the light
bool InModelShadow(vec3 P, vec4 L, vec4 r0, vec4 r1, vec4 r2, vec4 r3, vec2 nf, sampler2D map)
{
	if (L.w <= 0.0)
		return false;
	float dist = length(P - L.xyz);
	if (dist >= L.w || dist < 12.0)
		return false;
	vec4 pp = vec4(P, 1.0);
	vec4 clip = vec4(dot(r0, pp), dot(r1, pp), dot(r2, pp), dot(r3, pp));
	if (clip.w <= nf.x)
		return false;
	vec2 ndc = clip.xy / clip.w;
	if (abs(ndc.x) >= 1.0 || abs(ndc.y) >= 1.0)
		return false;
	ivec2 size = textureSize(map, 0);
	ivec2 tp = clamp(ivec2((ndc * 0.5 + 0.5) * vec2(size)), ivec2(0), size - ivec2(1));
	float stored = texelFetch(map, tp, 0).x;
	if (stored >= 1.0)
		return false;	// nothing was drawn here
	float occluder = LinearLightDepth(stored, nf.x, nf.y);
	float texel = 2.0 * clip.w / (float(size.x) * max(abs(r0.x), 1.0e-4));	// world size of one depth map texel at this distance
	return clip.w > occluder + 2.0 * texel + 1.0;
}

void main()
{
	gTexSize = textureSize(DepthTexture, 0);
	ivec2 ipos = ivec2((Offset + TexCoord * Scale) * vec2(gTexSize));
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);

	// sky, portals: nothing to shade
	if (InvDepthAt(ipos) <= (ProjParams.x + ProjParams.y) * 1.05 || (ns.a > 0.2 && ns.a < 0.45))
		discard;

	vec3 P = PosAt(ipos);
	if (InModelShadow(P, Light0, VP0_0, VP0_1, VP0_2, VP0_3, NearFar.xy, ShadowMap0) ||
	    InModelShadow(P, Light1, VP1_0, VP1_1, VP1_2, VP1_3, NearFar.zw, ShadowMap1))
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	discard;
}
