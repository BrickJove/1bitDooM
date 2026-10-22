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
vec3 gOcc[4];	// per map: the point of the model that shadows the pixel (set by InModelShadow)

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
	return float(int(texelFetch(SectorLightTexture, ivec2(sec & 4095, sec >> 12), 0).x + 0.5) & 255) / 255.0;
}

// which lights reach the sector at a position (bit 3..6: the model lights), all when unknown
int SectorLightMask(vec3 w)
{
	ivec2 size = textureSize(CellTexture, 0);
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (Grid.z <= 0.0 || c.x < 0 || c.y < 0 || c.x >= size.x / 4 || c.y >= size.y)
		return 65535;
	int sec = (int(texelFetch(CellTexture, ivec2(c.x * 4 + 3, c.y), 0).x + 0.5) >> 7) - 1;
	if (sec < 0)
		return 65535;
	return (int(texelFetch(SectorLightTexture, ivec2(sec & 4095, sec >> 12), 0).x + 0.5) >> 8) & 65535;
}

// Is there an open sky above the position? (same rule as sceneshadow.fp: solid cells next to an open sky carry a marker)
const float Big = 100000.0;
const float SolidOff = 200000.0;
const float NearSkyMark = 90000.0;

bool UnderSky(vec3 w)
{
	ivec2 size = textureSize(CellTexture, 0);
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (Grid.z <= 0.0 || c.x < 0 || c.y < 0 || c.x >= size.x / 4 || c.y >= size.y)
		return false;
	float f = texelFetch(CellTexture, ivec2(c.x * 4, c.y), 0).x;
	float ce = texelFetch(CellTexture, ivec2(c.x * 4 + 1, c.y), 0).x;
	if (f < SolidOff * 0.75)
		return ce > Big * 0.5;	// the sun only shines under an open sky (sky ceiling)
	return abs(ce - NearSkyMark) < 1.0;
}

// an open cell (not solid) with a sky ceiling: walls are lit by the sun only from the side of an open sky
bool UnderOpenSky(vec3 w)
{
	ivec2 size = textureSize(CellTexture, 0);
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (Grid.z <= 0.0 || c.x < 0 || c.y < 0 || c.x >= size.x / 4 || c.y >= size.y)
		return false;
	float f = texelFetch(CellTexture, ivec2(c.x * 4, c.y), 0).x;
	float ce = texelFetch(CellTexture, ivec2(c.x * 4 + 1, c.y), 0).x;
	return f < SolidOff * 0.75 && ce > Big * 0.5;
}

// sun: is the pixel lit by the open sky? (walls: the cell in front of the wall, floors and models: the cell below)
bool SunSky(vec3 P, vec3 n, bool hasNormal)
{
	if (hasNormal && abs(n.y) < 0.7)
		return UnderOpenSky(P + n * (1.1 / Grid.z));
	return UnderSky(P) || (hasNormal && UnderSky(P + n * (3.5 / Grid.z)));
}

// floor height of the sector at a position (a huge value when unknown or solid)
float FloorAt(vec3 w)
{
	ivec2 size = textureSize(CellTexture, 0);
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (Grid.z <= 0.0 || c.x < 0 || c.y < 0 || c.x >= size.x / 4 || c.y >= size.y)
		return 1.0e9;
	float f = texelFetch(CellTexture, ivec2(c.x * 4, c.y), 0).x;
	return f > SolidOff * 0.75 ? 1.0e9 : f;
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

// The sun: a perspective depth map from the position of the sun, narrowed to the area around the player (a 2 x 2 mosaic of the
// tiles 0, 1, 3, 4 of the atlas, see hw_modelshadow.cpp). L.xyz = the sun, F = viewing direction, T = tangent of the half edge.
// 0 = no shadow, 1 = a model is nearer to the sun than the pixel
int gSunDbg = 0;	// test mode 6: 0 = outside of the area, 1 = the map is empty here, 2 = a model is in the map here
int InSunMap(vec3 P, vec3 n, vec4 L, vec2 nf, float T, vec3 F, sampler2D map, int mi)
{
	vec3 d = P - L.xyz;
	float dist = length(d);
	if (dist < 12.0)
		return 0;
	bool hasN = dot(n, n) > 0.5;
	float cosT = hasN ? dot(n, -d) / dist : 1.0;
	if (hasN && cosT <= 0.02)
		return 0;	// the surface faces away from the sun (it is dark anyway)
	vec3 u = abs(F.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
	vec3 r = normalize(cross(F, u));
	vec3 u2 = cross(r, F);
	float m = dot(d, F);
	if (m <= nf.x)
		return 0;
	vec2 t = vec2(dot(d, r), dot(d, u2)) / m;	// tangents
	if (abs(t.x) >= T || abs(t.y) >= T)
		return 0;	// outside of the area of the map
	int qx = t.x > 0.0 ? 1 : 0;
	int qy = t.y > 0.0 ? 1 : 0;
	vec2 c = vec2(-0.5 * T + float(qx) * T, -0.5 * T + float(qy) * T);
	vec2 ndc = (t - c) / (0.5 * T);
	int size = textureSize(map, 0).x / 3;
	int face = qx + 3 * qy;
	ivec2 tp = ivec2(face % 3, face / 3) * size + clamp(ivec2((ndc * 0.5 + 0.5) * float(size)), ivec2(0), ivec2(size - 1));
	float texel = T * m / float(size);	// world size of one texel at this distance
	// the edge of the shadow is smooth: the occupancy of the 4 nearest texels is interpolated and cut at 50 % (a single texel gave
	// a jagged staircase edge, one step per texel of the depth map)
	float bias = hasN ? min(texel / max(cosT, 0.2), 3.0) + 0.3 : 64.0 + 2.0 * texel;
	vec2 fp = (ndc * 0.5 + 0.5) * float(size) - 0.5;
	ivec2 b0 = ivec2(floor(fp));
	vec2 w = fp - vec2(b0);
	ivec2 org = ivec2(face % 3, face / 3) * size;
	float cov = 0.0, occluder = 1.0e30;
	bool anyModel = false;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
		{
			float st = texelFetch(map, org + clamp(b0 + ivec2(i, j), ivec2(0), ivec2(size - 1)), 0).x;
			if (st >= 1.0)
				continue;
			anyModel = true;
			float oc = LinearLightDepth(st, nf.x, nf.y);
			if (m > oc + bias)
			{
				cov += (i == 0 ? 1.0 - w.x : w.x) * (j == 0 ? 1.0 - w.y : w.y);
				occluder = min(occluder, oc);
			}
		}
	gSunDbg = anyModel ? 2 : 1;
	if (cov < 0.5)
		return 0;
	gOcc[mi] = L.xyz + d * (occluder / m);
	return 1;
}

// 0 = no shadow of this light, 1 = shadow, 2 = a model is in the way but the light does not see the pixel
int InModelShadow(vec3 P, vec3 n, vec4 L, vec2 nf, bool haveWorld, sampler2D map, sampler2D world, int mi)
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
			// (only when the obstacle is close behind the pixel's line of sight: a wall that is hit low but far in front of the pixel
			// is a wall, not a step, and hides everything behind it - the shadow must not continue in the next room)
			float blockerY = L.y + d.y * (wall / m);
			if (blockerY - P.y < 56.0 && m - wall < 40.0)
				hidden = false;
		}
	}

	// smooth edge: the occupancy of the 4 nearest texels is interpolated and cut at 50 % (no staircase of single texels)
	vec2 fp = (ndc * 0.5 + 0.5) * float(size) - 0.5;
	ivec2 b0 = ivec2(floor(fp));
	vec2 w = fp - vec2(b0);
	ivec2 org = ivec2(face % 3, face / 3) * size;
	float cov = 0.0, occluder = 1.0e30;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++)
		{
			float st = texelFetch(map, org + clamp(b0 + ivec2(i, j), ivec2(0), ivec2(size - 1)), 0).x;
			if (st >= 1.0)
				continue;
			float oc = LinearLightDepth(st, nf.x, nf.y);
			if (m > oc + 2.0 * texel + 1.0)
			{
				cov += (i == 0 ? 1.0 - w.x : w.x) * (j == 0 ? 1.0 - w.y : w.y);
				occluder = min(occluder, oc);
			}
		}
	if (cov < 0.5)
		return 0;	// no model here, or the model is not nearer to the light than the pixel
	gOcc[mi] = L.xyz + d * (occluder / m);
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
	if (Flags.y > 4.5 && Flags.y < 5.5)
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

	// test mode 6: what does the sun map do? (only inside of its area)
	if (Flags.y > 5.5)
	{
		vec4 sr = DataRow(7);
		int sm = int(floor(DataRow(6).x + 0.5));
		if (sm < 0 || sr.w <= 0.0)
		{
			// no parallel sun map in this frame: horizontal lines (pure black: the 1-bit shader would turn colours into white)
			if ((ipos.y & 3) == 0)
				FragColor = vec4(0.0, 0.0, 0.0, 1.0);
			else
				discard;
			return;
		}
		vec4 Ls = sm == 0 ? Light0 : (sm == 1 ? Light1 : (sm == 2 ? Light2 : Light3));
		vec2 nfs = sm == 0 ? NearFar.xy : (sm == 1 ? NearFar.zw : (sm == 2 ? NearFar2.xy : NearFar2.zw));
		vec3 fs = sr.xyz;
		vec3 us = abs(fs.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
		vec3 rs = normalize(cross(fs, us));
		vec3 u2s = cross(rs, fs);
		vec3 ds = P - Ls.xyz;
		float ms = dot(ds, fs);
		if (ms <= 0.0 || abs(dot(ds, rs)) >= sr.w * ms || abs(dot(ds, u2s)) >= sr.w * ms)
			discard;	// outside of the area of the map
		bool skyOk = SunSky(P, n, hasNormal);
		int hit = 0;
		if (sm == 0) hit = InSunMap(P, n, Light0, NearFar.xy, sr.w, sr.xyz, ShadowMap0, 0);
		else if (sm == 1) hit = InSunMap(P, n, Light1, NearFar.zw, sr.w, sr.xyz, ShadowMap1, 1);
		else if (sm == 2) hit = InSunMap(P, n, Light2, NearFar2.xy, sr.w, sr.xyz, ShadowMap2, 2);
		else hit = InSunMap(P, n, Light3, NearFar2.zw, sr.w, sr.xyz, ShadowMap3, 3);
		// pure black patterns (the 1-bit shader turns coloured pixels with partial alpha into white)
		bool paint = false;
		if (!skyOk)
			paint = (ipos.x & 3) == 0;	// vertical lines: no open sky above
		else if (hit == 1)
			paint = true;	// solid: a model shadows the pixel
		else if (gSunDbg == 2)
			paint = (((ipos.x >> 1) + (ipos.y >> 1)) & 1) == 0;	// coarse checkerboard: the map holds a model at this place (a silhouette)
		else
			paint = ((ipos.x & 7) < 2 && (ipos.y & 7) < 2);	// dots: inside of the area, the map is empty here
		if (paint)
			FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		else
			discard;
		return;
	}

	int worldMask = int(Flags.z + 0.5);
	int s0 = InModelShadow(P, n, Light0, NearFar.xy, (worldMask & 1) != 0, ShadowMap0, WorldMap0, 0);
	int s1 = InModelShadow(P, n, Light1, NearFar.zw, (worldMask & 2) != 0, ShadowMap1, WorldMap1, 1);
	int s2 = InModelShadow(P, n, Light2, NearFar2.xy, (worldMask & 4) != 0, ShadowMap2, WorldMap2, 2);
	int s3 = InModelShadow(P, n, Light3, NearFar2.zw, (worldMask & 8) != 0, ShadowMap3, WorldMap3, 3);

	// the light of a map only makes shadows in the sectors that it reaches through open lines (not behind walls)
	{
		int reach = SectorLightMask(P + n * 3.0);
		// the map of the sun (row 6 of the data): no sector rule, but only pixels under an open sky, and not the floor
		// (the floor gets the flat projected shadow of the sun)
		int sunMap = int(floor(DataRow(6).x + 0.5));
		if (sunMap >= 0)
		{
			bool sky = DataRow(6).y > 0.5 || SunSky(P, n, hasNormal);
			bool floorPixel = hasNormal && abs(n.y) > 0.7;
			int sk = sunMap == 0 ? s0 : (sunMap == 1 ? s1 : (sunMap == 2 ? s2 : s3));
			vec4 sunRow = DataRow(7);
			if (sunRow.w > 0.0)
			{
				// the sun: perspective map from the sun position, narrowed to the area around the player
				vec3 sd = sunRow.xyz;
				if (sunMap == 0) sk = InSunMap(P, n, Light0, NearFar.xy, sunRow.w, sd, ShadowMap0, 0);
				else if (sunMap == 1) sk = InSunMap(P, n, Light1, NearFar.zw, sunRow.w, sd, ShadowMap1, 1);
				else if (sunMap == 2) sk = InSunMap(P, n, Light2, NearFar2.xy, sunRow.w, sd, ShadowMap2, 2);
				else sk = InSunMap(P, n, Light3, NearFar2.zw, sunRow.w, sd, ShadowMap3, 3);
			}
			// Floors get the flat projected shadow of the sun (models.cpp) where the model stands on that floor. A floor that is
			// higher than the floor under the model (a raised sector) gets the shadow from the map.
			if (!sky || (floorPixel && !(sk == 1 && P.y > FloorAt(gOcc[sunMap]) + 8.0)))
				sk = 0;
			if (sunMap == 0) s0 = sk;
			else if (sunMap == 1) s1 = sk;
			else if (sunMap == 2) s2 = sk;
			else s3 = sk;
			reach |= 4096 << sunMap;
		}
		if ((reach & 4096) == 0) s0 = 0;
		if ((reach & 8192) == 0) s1 = 0;
		if ((reach & 16384) == 0) s2 = 0;
		if ((reach & 32768) == 0) s3 = 0;
	}

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
