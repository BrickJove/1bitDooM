/*
** modelshadow.fp
**
** Jupiter3D: shadows of the models. For each of the (up to 4) lights of the map two depth maps exist, both seen from
** the light in six directions (a cube map laid out as an atlas of 3 x 2 tiles, see hw_modelshadow.cpp):
**   - ShadowMap: the models
**   - WorldMap:  the geometry of the level (walls, floors, ceilings)
** For every pixel the shader rebuilds the world position from the depth buffer and looks it up in both maps:
**   - the light sees the pixel when nothing of the level is nearer to the light (the wall in between ends the shadow)
**   - a model between the light and a pixel that the light sees casts a pure black shadow on it
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
layout(binding=2) uniform sampler2D ShadowMap0;
layout(binding=3) uniform sampler2D ShadowMap1;
layout(binding=4) uniform sampler2D ShadowMap2;
layout(binding=5) uniform sampler2D ShadowMap3;
layout(binding=6) uniform sampler2D WorldMap0;
layout(binding=7) uniform sampler2D WorldMap1;
layout(binding=8) uniform sampler2D WorldMap2;
layout(binding=9) uniform sampler2D WorldMap3;
layout(binding=10) uniform sampler2D DataTexture;	// 4 x 6 floats: position + radius of the 4 lights, near / far of the maps
layout(binding=11) uniform sampler2D CellTexture;	// height map of the level (see sceneshadow.fp): the count channel carries the sector index + 1 above 7 bits
layout(binding=12) uniform sampler2D SectorLightTexture;	// light level (0..255) per sector, 4096 per row

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

vec4 DataRow(int row)
{
	return vec4(texelFetch(DataTexture, ivec2(0, row), 0).x, texelFetch(DataTexture, ivec2(1, row), 0).x,
	            texelFetch(DataTexture, ivec2(2, row), 0).x, texelFetch(DataTexture, ivec2(3, row), 0).x);
}

// light level of the sector at a position, 0..1 (0 when the position is in no sector)
float SectorAmbient(vec3 w)
{
	if (Grid.z <= 0.0)
		return 0.0;
	ivec2 size = textureSize(CellTexture, 0);
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (c.x < 0 || c.y < 0 || c.x >= size.x / 4 || c.y >= size.y)
		return 0.0;
	int sec = (int(texelFetch(CellTexture, ivec2(c.x * 4 + 3, c.y), 0).x + 0.5) >> 7) - 1;
	if (sec < 0)
		return 0.0;
	return clamp(texelFetch(SectorLightTexture, ivec2(sec & 4095, sec >> 12), 0).x / 255.0, 0.0, 1.0);
}

float LinearLightDepth(float d, float n, float f)
{
	return 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n));
}

// the six faces of a map: viewing direction and up vector, as in hw_modelshadow.cpp
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

// 0 = no shadow of this light, 1 = shadow, 2 = a model is in the way but the light does not see the pixel
int InModelShadow(vec3 P, vec3 n, vec4 L, vec2 nf, bool haveWorld, sampler2D map, sampler2D world)
{
	if (L.w <= 0.0)
		return 0;
	vec3 d = P - L.xyz;
	float dist = length(d);
	if (dist >= L.w || dist < 12.0)
		return 0;

	// surfaces that face away from the light or that it only grazes cannot show a stable shadow
	// (the depth comparison is meaningless there and the result would flicker)
	if (dot(n, n) > 0.5 && dot(n, -d) <= 0.02 * dist)
		return 0;

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
		return 0;
	vec2 ndc = vec2(dot(d, r), dot(d, u2)) / m;

	int size = textureSize(map, 0).x / 3;
	ivec2 tp = ivec2(face % 3, face / 3) * size + clamp(ivec2((ndc * 0.5 + 0.5) * float(size)), ivec2(0), ivec2(size - 1));
	float texel = 2.0 * m / float(size);	// world size of one depth map texel at this distance (90 degree field of view)

	// does the light see the pixel? (the level geometry nearer to the light hides it)
	bool hidden = false;
	if (haveWorld)
	{
		float ws = texelFetch(world, tp, 0).x;
		if (ws < 1.0)
		{
			float wall = LinearLightDepth(ws, nf.x, nf.y);
			// surfaces seen at a grazing angle change their depth a lot within one texel
			float slope = dot(n, n) > 0.5 ? 1.0 / max(abs(dot(n, d)) / dist, 0.1) : 3.0;
			hidden = m > wall + texel * (1.0 + slope) + 4.0;
			// a low obstacle (step, low platform) does not end the shadow: only things that rise
			// clearly above the pixel behind them hide it (height of the blocking point above the pixel)
			float blockerY = L.y + d.y * (wall / m);
			if (blockerY - P.y < 56.0)
				hidden = false;
		}
	}

	float stored = texelFetch(map, tp, 0).x;
	if (stored >= 1.0)
		return 0;	// no model here
	float occluder = LinearLightDepth(stored, nf.x, nf.y);
	if (m <= occluder + 2.0 * texel + 1.0)
		return 0;	// the model is not nearer to the light than the pixel
	return hidden ? 2 : 1;
}

void main()
{
	gTexSize = textureSize(DepthTexture, 0);
	vec4 Light0 = DataRow(0), Light1 = DataRow(1), Light2 = DataRow(2), Light3 = DataRow(3);
	vec4 NearFar = DataRow(4), NearFar2 = DataRow(5);
	ivec2 ipos = ivec2((Offset + TexCoord * Scale) * vec2(gTexSize));
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);

	// test mode 5: the left half of the screen black
	if (Flags.y > 4.5)
	{
		if (ipos.x < gTexSize.x / 2)
			FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		else
			discard;
		return;
	}

	// sky, portals: nothing to shade
	if (InvDepthAt(ipos) <= (ProjParams.x + ProjParams.y) * 1.05 || (ns.a > 0.2 && ns.a < 0.45))
		discard;

	vec3 P = PosAt(ipos);

	// surface normal of the map (models and sprites have none)
	vec3 n = ns.xyz * 2.0 - 1.0;
	bool hasNormal = ns.a > 0.5 && dot(n, n) > 0.01;
	if (hasNormal)
		n = normalize(inverse(mat3(CamX.xyz, CamY.xyz, CamZ.xyz)) * n);
	else
		n = vec3(0.0);
	if (hasNormal && abs(n.y) > 0.95)
		n = vec3(0.0, n.y > 0.0 ? 1.0 : -1.0, 0.0);

	int worldMask = int(Flags.z + 0.5);
	int s0 = InModelShadow(P, n, Light0, NearFar.xy, (worldMask & 1) != 0, ShadowMap0, WorldMap0);
	int s1 = InModelShadow(P, n, Light1, NearFar.zw, (worldMask & 2) != 0, ShadowMap1, WorldMap1);
	int s2 = InModelShadow(P, n, Light2, NearFar2.xy, (worldMask & 4) != 0, ShadowMap2, WorldMap2);
	int s3 = InModelShadow(P, n, Light3, NearFar2.zw, (worldMask & 8) != 0, ShadowMap3, WorldMap3);

	// debug: what does the light of map k see? Everything it does not see gets a checkerboard.
	if (Flags.y > 0.5)
	{
		int mask = int(Flags.y + 0.5);
		bool hidden = false;
		vec4 L = mask == 1 ? Light0 : (mask == 2 ? Light1 : (mask == 3 ? Light2 : Light3));
		vec2 nf = mask == 1 ? NearFar.xy : (mask == 2 ? NearFar.zw : (mask == 3 ? NearFar2.xy : NearFar2.zw));
		bool have = (worldMask & (1 << (mask - 1))) != 0;
		if (L.w > 0.0 && have && ((ipos.x + ipos.y) & 1) == 0)
		{
			vec3 d = P - L.xyz;
			float dist = length(d);
			if (dist > 12.0)
			{
				// reuse the lookup of the world map
				vec3 a = abs(d);
				int face;
				if (a.x >= a.y && a.x >= a.z) face = d.x > 0.0 ? 0 : 1;
				else if (a.y >= a.z) face = d.y > 0.0 ? 2 : 3;
				else face = d.z > 0.0 ? 4 : 5;
				vec3 f, u;
				FaceBasis(face, f, u);
				vec3 r = cross(f, u);
				vec3 u2 = cross(r, f);
				float m = dot(d, f);
				vec2 ndc = vec2(dot(d, r), dot(d, u2)) / max(m, 1.0e-3);
				int size = textureSize(WorldMap0, 0).x / 3;
				ivec2 tp = ivec2(face % 3, face / 3) * size + clamp(ivec2((ndc * 0.5 + 0.5) * float(size)), ivec2(0), ivec2(size - 1));
				float ws = mask == 1 ? texelFetch(WorldMap0, tp, 0).x : (mask == 2 ? texelFetch(WorldMap1, tp, 0).x : (mask == 3 ? texelFetch(WorldMap2, tp, 0).x : texelFetch(WorldMap3, tp, 0).x));
				if (ws < 1.0)
				{
					float wall = LinearLightDepth(ws, nf.x, nf.y);
					float texel = 2.0 * m / float(size);
					float slope = dot(n, n) > 0.5 ? 1.0 / max(abs(dot(n, d)) / dist, 0.1) : 3.0;
					hidden = m > wall + texel * (1.0 + slope) + 4.0 || (dot(n, n) > 0.5 && dot(n, -d) <= 0.0);
				}
			}
		}
		if (hidden)
		{
			FragColor = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		discard;
	}

	if (Flags.w > 0.5 && ((ipos.x + ipos.y) & 1) != 0)
		discard;	// checkerboard style: only every second pixel of the shadow is black
	if (s0 == 1 || s1 == 1 || s2 == 1 || s3 == 1)
	{
		// the light level of the sector: a lit sector has nothing that a light could still brighten
		if (Grid.w > 0.0 && 1.0 - Grid.w * SectorAmbient(P + n * 3.0) <= 0.5)
			discard;
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	if (Flags.x > 0.5 && (s0 == 2 || s1 == 2 || s2 == 2 || s3 == 2) && ((ipos.x + ipos.y) & 1) == 0)
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);	// debug: a model is in the way, but the light does not see the pixel
		return;
	}
	discard;
}
