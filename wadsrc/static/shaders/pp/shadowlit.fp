/*
** shadowlit.fp
**
** Jupiter3D: decides per pixel (with the stencil test of the draw call) which stencil bits are written:
**   Mode 0: the pixel is reached by the source (sun: always, light: within its radius)
**   Mode 1: ... and the surface faces the source (the stencil test lets only pixels outside of every shadow volume pass)
**   Mode 2: the pixel is so close to the light that it is always lit
**   Mode 3: no test (full screen), used for the roof pass and the final black pass
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D DepthTexture;	// copy of the scene depth
layout(binding=1) uniform sampler2D NormalTexture;

uniform vec4 CamX;
uniform vec4 CamY;
uniform vec4 CamZ;
uniform vec4 ProjParams;	// LinearizeDepthA, LinearizeDepthB, 1/proj[0], 1/proj[5]
uniform vec2 Scale;
uniform vec2 Offset;
uniform vec4 Source;		// xyz = position, w = radius (0 = sun)
uniform int Mode;
uniform int FacingShade;

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

void main()
{
	if (Mode == 3)
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	gTexSize = textureSize(DepthTexture, 0);
	ivec2 ipos = ivec2((Offset + TexCoord * Scale) * vec2(gTexSize));
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);

	// sky, portals: nothing to shade
	if (InvDepthAt(ipos) <= (ProjParams.x + ProjParams.y) * 1.05 || (ns.a > 0.2 && ns.a < 0.45))
		discard;

	vec3 P = PosAt(ipos);
	vec3 toL = Source.xyz - P;
	float dist = length(toL);
	if (Source.w > 0.0 && dist >= Source.w)
		discard;	// not reached
	if (Mode == 2)
	{
		if (dist > 64.0)
			discard;	// only the light itself and its fixture
		FragColor = vec4(0.0);
		return;
	}
	if (Mode == 0)
	{
		FragColor = vec4(0.0);
		return;
	}

	// Mode 1: does the surface face the light?
	if (FacingShade != 0)
	{
		vec3 n = vec3(0.0);
		bool hasNormal = ns.a > 0.5;
		if (hasNormal)
		{
			n = ns.xyz * 2.0 - 1.0;
			if (dot(n, n) > 0.01)
				n = normalize(inverse(mat3(CamX.xyz, CamY.xyz, CamZ.xyz)) * n);
			else
				hasNormal = false;
		}
		else if (ns.a < 0.5 && min(ns.x, min(ns.y, ns.z)) > 0.97)
		{
			// models: normal from the neighbouring depth values
			vec3 Pr = PosAt(ipos + ivec2(1, 0)), Pl = PosAt(ipos - ivec2(1, 0));
			vec3 Pu = PosAt(ipos + ivec2(0, 1)), Pd = PosAt(ipos - ivec2(0, 1));
			vec3 dx = length(Pr - P) < length(P - Pl) ? Pr - P : P - Pl;
			vec3 dy = length(Pu - P) < length(P - Pd) ? Pu - P : P - Pd;
			vec3 dn = cross(dx, dy);
			if (dot(dn, dn) > 1.0e-10)
			{
				dn = normalize(dn);
				if (dot(dn, vec3(CamX.w, CamY.w, CamZ.w) - P) < 0.0)
					dn = -dn;
				n = dn;
				hasNormal = true;
			}
		}
		if (hasNormal && dot(n, toL / max(dist, 1.0e-4)) <= 0.0)
			discard;	// faces away: not lit
	}
	FragColor = vec4(0.0);
}
