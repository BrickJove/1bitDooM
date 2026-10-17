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
layout(binding=4) uniform sampler2D ShadowMap2;
layout(binding=5) uniform sampler2D ShadowMap3;

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

// the six faces of a map (like a cube map in a 3 x 2 atlas): viewing direction and up vector, as in hw_modelshadow.cpp
void FaceBasis(int face, out vec3 f, out vec3 u)
{
	u = vec3(0.0, 1.0, 0.0);
	if (face == 0) f = vec3(1.0, 0.0, 0.0);
	else if (face == 1) f = vec3(-1.0, 0.0, 0.0);
	else if (face == 2) { f = vec3(0.0, 1.0, 0.0); u = vec3(0.0, 0.0, 1.0); }
	else if (face == 3) { f = vec3(0.0, -1.0, 0.0); u = vec3(0.0, 0.0, 1.0); }
	else if (face == 4) f = vec3(0.0, 0.0, 1.0);
	else f = vec3(0.0, 0.0, -1.0);
}

// true when a model is between the point and the light
bool InModelShadow(vec3 P, vec4 L, vec2 nf, sampler2D map)
{
	if (L.w <= 0.0)
		return false;
	vec3 d = P - L.xyz;
	float dist = length(d);
	if (dist >= L.w || dist < 12.0)
		return false;

	vec3 a = abs(d);
	int face;
	if (a.x >= a.y && a.x >= a.z) face = d.x > 0.0 ? 0 : 1;
	else if (a.y >= a.z) face = d.y > 0.0 ? 2 : 3;
	else face = d.z > 0.0 ? 4 : 5;

	vec3 f, u;
	FaceBasis(face, f, u);
	vec3 r = cross(f, u);
	vec3 u2 = cross(r, f);
	float m = dot(d, f);	// distance along the viewing direction of the face = w of the clip position
	if (m <= nf.x)
		return false;
	vec2 ndc = vec2(dot(d, r), dot(d, u2)) / m;

	int size = textureSize(map, 0).x / 3;
	ivec2 tp = ivec2(face % 3, face / 3) * size + clamp(ivec2((ndc * 0.5 + 0.5) * float(size)), ivec2(0), ivec2(size - 1));
	float stored = texelFetch(map, tp, 0).x;
	if (stored >= 1.0)
		return false;	// nothing was drawn here
	float occluder = LinearLightDepth(stored, nf.x, nf.y);
	float texel = 2.0 * m / float(size);	// world size of one depth map texel at this distance (90 degree field of view)
	return m > occluder + 2.0 * texel + 1.0;
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
	if (InModelShadow(P, Light0, NearFar.xy, ShadowMap0) ||
	    InModelShadow(P, Light1, NearFar.zw, ShadowMap1) ||
	    InModelShadow(P, Light2, NearFar2.xy, ShadowMap2) || InModelShadow(P, Light3, NearFar2.zw, ShadowMap3))
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	discard;
}
