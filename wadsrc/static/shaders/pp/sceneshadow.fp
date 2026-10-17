/*
** sceneshadow.fp
**
** Jupiter3D: shadows of a sun and dynamic lights that do not depend on the view.
**
** The CPU builds a height map of the level (two textures, one texel per grid cell): the floor
** height and the ceiling height of the sector in the cell. Solid space (outside the sectors, one
** sided walls) has floor = +big, ceiling = -big, a sky ceiling has ceiling = +big.
**
** For every pixel the shader rebuilds the world position from the depth buffer and marches a ray
** towards the light through that height map. The ray is blocked when it is below a floor or above
** a (non sky) ceiling. The sun only shines on pixels under an open sky.
**
** Output: pure black with alpha 1 (hard) or an ordered dither, blended over the scene colour.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

#if defined(MULTISAMPLE)
layout(binding=0) uniform sampler2DMS DepthTexture;
layout(binding=1) uniform sampler2DMS NormalTexture;
#else
layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
#endif
layout(binding=2) uniform sampler2D CellTexture;	// 4 floats per cell: floor, ceiling, list start, list count
layout(binding=3) uniform sampler2D ListTexture;	// line indices
layout(binding=4) uniform sampler2D LineTexture;	// 8 floats per line: x1, y1, x2, y2, blocked below, blocked above, fence, 0
layout(binding=5) uniform sampler2D FenceParamTexture;	// 16 floats per fence (one row each)
layout(binding=7) uniform sampler2D LightTexture;	// 3 rows: x, y, z, radius of the nearest dynamic lights
layout(binding=6) uniform sampler2D FenceAtlasTexture;	// alpha masks, 128 x 128 per fence texture

const float Big = 100000.0;
const float SolidOff = 200000.0;	// solid cells: floor channel = height + SolidOff
const float NearSkyMark = 90000.0;

ivec2 gTexSize;
ivec2 gGridSize;
bool gFences = true;
float gFoot = 1.0;	// world size of the footprint of the pixel on the receiving surface
float gRayLen = 1.0;	// length of the ray of the shadow test
vec3 gLightPos = vec3(0.0);	// the source of the ray of the shadow test
vec3 gOrigin = vec3(0.0);	// start of the ray: fence shadows only count within gl_scene_shadow_fence_range of the receiver

ivec2 ClampPos(ivec2 p)
{
	return clamp(p, ivec2(0), gTexSize - ivec2(1));
}

float InvDepthAt(ivec2 p)
{
	float d = texelFetch(DepthTexture, ClampPos(p), 0).x;
	return d * ProjParams.x + ProjParams.y;
}

bool IsSkyPixel(ivec2 p, vec4 ns)
{
	return InvDepthAt(p) <= (ProjParams.x + ProjParams.y) * 1.05 || (ns.a > 0.2 && ns.a < 0.45);
}

ivec2 TexelOf(vec2 tc)
{
	return ivec2((Offset + tc * Scale) * vec2(gTexSize));
}

float Bayer4(ivec2 p)
{
	int x = p.x & 3, y = p.y & 3;
	const float m[16] = float[16](
		0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
		12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
		3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
		15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0);
	return m[y * 4 + x] + 0.5 / 16.0;
}

// grid cell of a world position (render space: x = X, y = Z up, z = Y), x = -1 when outside the grid
ivec2 CellOf(vec3 w)
{
	ivec2 c = ivec2(floor((w.xz - Grid.xy) * Grid.z));
	if (c.x < 0 || c.y < 0 || c.x >= gGridSize.x || c.y >= gGridSize.y)
		return ivec2(-1);
	return c;
}

float CellValue(ivec2 c, int k)
{
	return texelFetch(CellTexture, ivec2(c.x * 4 + k, c.y), 0).x;
}

// Is there an open sky above the position? Solid cells close to an open sky cell carry the marker 12345 in the ceiling
// channel (see hw_shadowheightmap.cpp), so pixels right at a wall base are classified like the open ground next to them.
bool UnderSky(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return false;
	float f = CellValue(c, 0);
	float ce = CellValue(c, 1);
	if (f < SolidOff * 0.75)
		return ce > Big * 0.5;
	return abs(ce - NearSkyMark) < 1.0;
}

float FenceParam(int id, int k)
{
	return texelFetch(FenceParamTexture, ivec2(k, id), 0).x;
}

// Does the ray piece S0 -> S1 hit an opaque pixel of the fence texture at the crossing point (A -> B is the line, u the
// position along it)?
bool FencePixel(int id, vec2 A, vec2 B, float u, float y, bool solid)
{
	bool wrap = FenceParam(id, 10) > 0.5;
	if (!wrap && (y < FenceParam(id, 11) || y > FenceParam(id, 12)))
		return false;
	float v = (FenceParam(id, 4) - y) / max(FenceParam(id, 5), 1.0);
	if (FenceParam(id, 13) > 0.5)
		u = 1.0 - u;	// texture of the back side runs from the other end of the line
	if (wrap)
		v = fract(v);
	else if (v < 0.0 || v > 1.0)
		return false;
	if (solid)
		return FenceParam(id, 14) >= 0.25;	// lattice finer than the pixel: the mean coverage of the texture decides
	float uu = fract((u * length(B - A) + FenceParam(id, 6)) * FenceParam(id, 7));
	vec2 apos = vec2(FenceParam(id, 8), FenceParam(id, 9)) + min(vec2(uu, v) * 128.0, vec2(127.0));
	return texelFetch(FenceAtlasTexture, ivec2(apos), 0).x > 0.2;
}

// Do the lines in the cell block the ray piece S0 -> S1 (exact segment tests)?
bool LinesBlock(ivec2 c, vec3 S0, vec3 S1)
{
	int count = int(CellValue(c, 3) + 0.5);
	int start = int(CellValue(c, 2) + 0.5);
	vec2 r = S1.xz - S0.xz;
	for (int k = 0; k < 96; k++)
	{
		if (k >= count)
			break;
		int idx = start + k;
		int id = int(texelFetch(ListTexture, ivec2(idx & 4095, idx >> 12), 0).x + 0.5);
		ivec2 base = ivec2((id & 511) * 8, id >> 9);
		vec2 A = vec2(texelFetch(LineTexture, base, 0).x, texelFetch(LineTexture, base + ivec2(1, 0), 0).x);
		vec2 B = vec2(texelFetch(LineTexture, base + ivec2(2, 0), 0).x, texelFetch(LineTexture, base + ivec2(3, 0), 0).x);
		vec2 s = B - A;
		float rxs = r.x * s.y - r.y * s.x;
		if (abs(rxs) < 1.0e-9)
			continue;
		vec2 qp = A - S0.xz;
		float t = (qp.x * s.y - qp.y * s.x) / rxs;
		float u = (qp.x * r.y - qp.y * r.x) / rxs;
		if (t < -1.0e-5 || t > 1.00001 || u < -1.0e-5 || u > 1.00001)
			continue;
		float y = mix(S0.y, S1.y, clamp(t, 0.0, 1.0));
		if (y < texelFetch(LineTexture, base + ivec2(4, 0), 0).x || y > texelFetch(LineTexture, base + ivec2(5, 0), 0).x)
			return true;
		int fence = int(texelFetch(LineTexture, base + ivec2(6, 0), 0).x + 0.5) - 1;
		if (fence >= 0 && gFences)
		{
			float dh = distance(mix(S0, S1, clamp(t, 0.0, 1.0)), gOrigin);
			if (dh < (Light.y > 0.5 ? Light.y : 1.0e9))
			{
				// The shadow of a fence is the texture itself in black, as large as the texture: parallel projection along the
				// direction fence -> light (only the length of the shadow on the floor depends on the light). The ray above only
				// decides that this fence is between the pixel and the light.
				vec2 sv = B - A;
				float flen = max(length(sv), 1.0e-3);
				vec2 sd = sv / flen;
				vec2 nf = vec2(-sd.y, sd.x);
				vec3 dir = normalize(gLightPos - vec3(0.5 * (A.x + B.x), y, 0.5 * (A.y + B.y)));
				float dn = dot(nf, dir.xz);
				if (abs(dn) > 0.04)
				{
					float st = dot(nf, A - gOrigin.xz) / dn;
					vec3 Hp = gOrigin + dir * st;
					float up = dot(Hp.xz - A, sd) / flen;
					if (st > 0.0 && up >= 0.0 && up <= 1.0)
					{
						// the footprint of the pixel at the fence against the texel size of the texture (from its scale, so
						// 128 and 256 pixel high or scaled textures all work): finer than the pixel = mean coverage
						float texel = max(1.0 / (FenceParam(fence, 7) * 128.0), FenceParam(fence, 5) / 128.0);
						bool solid = gFoot * 0.7 > texel;
						if (FencePixel(fence, A, B, up, Hp.y, solid))
							return true;
					}
				}
			}
		}
	}
	return false;
}

// 1 = the ray P + D * t (0 < t < len) hits something. The ray is walked cell by cell (exact grid traversal, no fixed
// steps and no jitter). Cells with lines are tested against the exact line segments (straight shadow edges that start
// at the corners of the geometry), the other cells against the floor and ceiling height.
float Trace(vec3 P, vec3 D, float len, float jitter, float topLimit)
{
	float cell = 1.0 / Grid.z;
	gRayLen = len;
	gLightPos = P + D * len;
	float t0 = cell * 0.25;
	if (len <= t0)
		return 0.0;
	vec3 A = P + D * t0;
	float tEnd = len - t0;
	// above the highest floor / ceiling of the level nothing can block the ray
	if (D.y > 1.0e-4)
		tEnd = min(tEnd, (topLimit - A.y) / D.y);
	else if (A.y > topLimit)
		return 0.0;
	if (tEnd <= 0.0)
		return 0.0;

	vec2 d2 = D.xz;
	vec2 p = (A.xz - Grid.xy) * Grid.z;	// in cell units
	ivec2 c = ivec2(floor(p));
	ivec2 stp = ivec2(int(sign(d2.x)), int(sign(d2.y)));
	vec2 tDelta = vec2(abs(d2.x) > 1.0e-6 ? 1.0 / (abs(d2.x) * Grid.z) : 1.0e30,
	                   abs(d2.y) > 1.0e-6 ? 1.0 / (abs(d2.y) * Grid.z) : 1.0e30);
	vec2 toEdge = vec2(d2.x > 0.0 ? float(c.x) + 1.0 - p.x : p.x - float(c.x),
	                   d2.y > 0.0 ? float(c.y) + 1.0 - p.y : p.y - float(c.y));
	vec2 tMax = vec2(abs(d2.x) > 1.0e-6 ? toEdge.x * tDelta.x : 1.0e30,
	                 abs(d2.y) > 1.0e-6 ? toEdge.y * tDelta.y : 1.0e30);

	float tin = 0.0;
	for (int i = 0; i < 2200; i++)
	{
		if (c.x < 0 || c.y < 0 || c.x >= gGridSize.x || c.y >= gGridSize.y)
			return 0.0;	// left the map
		float tout = min(min(tMax.x, tMax.y), tEnd);
		vec3 S0 = A + D * tin, S1 = A + D * tout;
		if (CellValue(c, 3) > 0.5)
		{
			if (LinesBlock(c, S0, S1))
				return 1.0;
		}
		else
		{
			float fl = CellValue(c, 0);
			if (fl > SolidOff * 0.75)
				fl -= SolidOff;
			if (min(S0.y, S1.y) < fl || max(S0.y, S1.y) > CellValue(c, 1))
				return 1.0;
		}
		if (tout >= tEnd)
			break;
		if (tMax.x < tMax.y)
		{
			c.x += stp.x;
			tMax.x += tDelta.x;
		}
		else
		{
			c.y += stp.y;
			tMax.y += tDelta.y;
		}
		tin = tout;
	}
	return 0.0;
}

// Walls: the depth buffer position is imprecise, so the pixel is moved along its exact view ray onto the plane of the
// nearest map line that is parallel to the wall (the line is looked up in the cell of the pixel and the cell in front).
void SnapWall(inout vec3 P, inout vec3 n, float cell)
{
	vec3 cam = vec3(CamX.w, CamY.w, CamZ.w);
	vec3 vd = normalize(P - cam);
	vec2 n2 = normalize(n.xz);
	float best = 1.0e9;
	vec2 bA = vec2(0.0), bM = vec2(0.0);
	for (int ci = 0; ci < 2; ci++)
	{
		ivec2 c = CellOf(ci == 0 ? P : P + n * (cell * 2.0));
		if (c.x < 0)
			continue;
		int count = int(CellValue(c, 3) + 0.5);
		int start = int(CellValue(c, 2) + 0.5);
		for (int k = 0; k < 48; k++)
		{
			if (k >= count)
				break;
			int idx = start + k;
			int id = int(texelFetch(ListTexture, ivec2(idx & 4095, idx >> 12), 0).x + 0.5);
			ivec2 base = ivec2((id & 511) * 8, id >> 9);
			vec2 A = vec2(texelFetch(LineTexture, base, 0).x, texelFetch(LineTexture, base + ivec2(1, 0), 0).x);
			vec2 B = vec2(texelFetch(LineTexture, base + ivec2(2, 0), 0).x, texelFetch(LineTexture, base + ivec2(3, 0), 0).x);
			vec2 sv = B - A;
			float len = length(sv);
			if (len < 1.0e-3)
				continue;
			vec2 d = sv / len;
			if (abs(dot(d, n2)) > 0.2)
				continue;	// not parallel to this wall
			vec2 m = vec2(-d.y, d.x);
			if (dot(m, n2) < 0.0)
				m = -m;
			float along = dot(d, P.xz - A);
			if (along < -8.0 || along > len + 8.0)
				continue;
			float dist = abs(dot(m, P.xz - A));
			if (dist < best)
			{
				best = dist;
				bA = A;
				bM = m;
			}
		}
	}
	if (best < 10.0)
	{
		float den = dot(bM, vd.xz);
		if (abs(den) > 1.0e-4)
		{
			float tt = dot(bM, bA - cam.xz) / den;
			if (tt > 0.0)
			{
				P = cam + vd * tt;
				n = vec3(bM.x, 0.0, bM.y);
			}
		}
	}
}

// world position (render space) of a depth buffer pixel
vec3 PosAt(ivec2 p)
{
	vec2 tc = ((vec2(p) + 0.5) / vec2(gTexSize) - Offset) / Scale;
	float z = 1.0 / max(InvDepthAt(p), 1.0e-8);
	vec3 eye = vec3((tc * 2.0 - 1.0) * ProjParams.zw * z, -z);
	return vec3(dot(CamX.xyz, eye) + CamX.w, dot(CamY.xyz, eye) + CamY.w, dot(CamZ.xyz, eye) + CamZ.w);
}

void main()
{
#if defined(MULTISAMPLE)
	gTexSize = textureSize(DepthTexture);
#else
	gTexSize = textureSize(DepthTexture, 0);
#endif
	gGridSize = ivec2(textureSize(CellTexture, 0).x / 4, textureSize(CellTexture, 0).y);

	ivec2 ipos = TexelOf(TexCoord);
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);
	if (IsSkyPixel(ipos, ns))
	{
		FragColor = vec4(0.0);
		return;
	}

	int packedBits = int(Grid.w + 0.5);
	int flags = packedBits & 7;
	gFences = (flags & 4) == 0;
	bool facingShade = (flags & 1) != 0;
	bool dither = (flags & 2) != 0;

	// world position (render space) from depth: eye = (ndc * focal * z, -z)
	float z = 1.0 / max(InvDepthAt(ipos), 1.0e-8);
	vec3 eye = vec3((TexCoord * 2.0 - 1.0) * ProjParams.zw * z, -z);
	vec3 P = vec3(dot(CamX.xyz, eye) + CamX.w, dot(CamY.xyz, eye) + CamY.w, dot(CamZ.xyz, eye) + CamZ.w);

	// world normal from the eye space normal (not available for models / sprites)
	vec3 n = ns.xyz * 2.0 - 1.0;
	bool hasNormal = ns.a > 0.5 && dot(n, n) > 0.01;
	if (hasNormal)
		n = normalize(inverse(mat3(CamX.xyz, CamY.xyz, CamZ.xyz)) * n);
	else
		n = vec3(0.0);

	bool mapNormal = hasNormal;

	// Models and sprites have no normal in the buffer (they are marked with 1,1,1): take it from the neighbouring depth
	// values so they can be lit / shadowed like the map (surface facing away from the light = black).
	if (!hasNormal && ns.a < 0.5 && min(ns.x, min(ns.y, ns.z)) > 0.97)
	{
		vec3 Pr = PosAt(ipos + ivec2(1, 0)), Pl = PosAt(ipos - ivec2(1, 0));
		vec3 Pu = PosAt(ipos + ivec2(0, 1)), Pd = PosAt(ipos - ivec2(0, 1));
		vec3 dx = length(Pr - P) < length(P - Pl) ? Pr - P : P - Pl;
		vec3 dy = length(Pu - P) < length(P - Pd) ? Pu - P : P - Pd;
		vec3 dn = cross(dx, dy);
		if (dot(dn, dn) > 1.0e-10)
		{
			dn = normalize(dn);
			vec3 toCam = vec3(CamX.w, CamY.w, CamZ.w) - P;
			if (dot(dn, toCam) < 0.0)
				dn = -dn;
			n = dn;
			hasNormal = true;
		}
	}

	float cell = 1.0 / Grid.z;

	// floors and ceilings face exactly up / down (the normal buffer is only 10 bit: noisy facing tests)
	if (hasNormal && abs(n.y) > 0.95)
		n = vec3(0.0, n.y > 0.0 ? 1.0 : -1.0, 0.0);

	// Floors and ceilings: the depth buffer is imprecise at grazing angles (moire along shadow edges). The height of the
	// surface is known from the height map, so move the position along the view ray onto that exact height.
	if (hasNormal && abs(n.y) > 0.95)
	{
		vec3 cam = vec3(CamX.w, CamY.w, CamZ.w);
		vec3 vd = normalize(P - cam);	// exact view ray of the pixel
		for (int it = 0; it < 3; it++)
		{
			ivec2 sc = CellOf(P);
			if (sc.x < 0 || CellValue(sc, 3) > 0.5)
				break;
			float sf = CellValue(sc, 0);
			float target = n.y > 0.0 ? sf : CellValue(sc, 1);
			if (sf > SolidOff * 0.75 || abs(target) > Big * 0.5 || abs(P.y - target) > 16.0 || abs(vd.y) < 1.0e-4)
				break;
			float tt = (target - cam.y) / vd.y;
			if (tt <= 0.0)
				break;
			P = cam + vd * tt;
		}
	}
	if (mapNormal && abs(n.y) < 0.3 && dot(n.xz, n.xz) > 0.5)
		SnapWall(P, n, cell);
	vec3 Q = P + n * (cell * 1.5);
	gOrigin = Q;
	// no fence shadows on ceilings; the footprint of the pixel on its surface (long on floors seen at a grazing angle)
	if (hasNormal && n.y < -0.5)
		gFences = false;
	{
		vec3 camp = vec3(CamX.w, CamY.w, CamZ.w);
		float pix = z * ProjParams.w * 2.0 / float(gTexSize.y);
		gFoot = pix / (hasNormal ? max(abs(dot(n, normalize(P - camp))), 0.05) : 1.0);
	}
	// start point of the rays, a bit off the surface
	float jitter = Bayer4(ipos);
	float topLimit = float(packedBits >> 10);

	// A pixel is black when at least one source reaches it (sun: pixel under an open sky, light: within its radius)
	// and every source that reaches it is blocked (the surface faces away, or the ray hits the map).
	int reached = 0;
	int lit = 0;

	// sun: a point light above the centre of the level
	if (Sun.w > 0.0 && (UnderSky(Q) || (hasNormal && UnderSky(P + n * (cell * 3.5)))))
	{
		vec3 toS = Sun.xyz - Q;
		float dS = length(toS);
		if (dS > 8.0)
		{
			reached++;
			vec3 D = toS / dS;
			float facing = hasNormal ? dot(n, D) : 1.0;
			if (facing > 0.0)
			{
				if (Trace(Q, D, dS, jitter, topLimit) < 0.5)
					lit++;
			}
			else if (!facingShade)
				lit++;
		}
	}

	// the nearest dynamic lights (surfaces close to a light are always lit by it: the light and its fixture)
	for (int i = 0; i < 3; i++)
	{
		if (float(i) >= Light.x)
			break;
		vec4 L = vec4(texelFetch(LightTexture, ivec2(0, i), 0).x, texelFetch(LightTexture, ivec2(1, i), 0).x,
		              texelFetch(LightTexture, ivec2(2, i), 0).x, texelFetch(LightTexture, ivec2(3, i), 0).x);
		if (L.w <= 0.0)
			continue;
		vec3 toL = L.xyz - Q;
		float dist = length(toL);
		if (dist >= L.w)
			continue;
		reached++;
		if (dist <= 64.0)
		{
			lit++;
			continue;
		}
		vec3 D = toL / dist;
		float facing = hasNormal ? dot(n, D) : 1.0;
		if (facing > 0.0)
		{
			if (Trace(Q, D, dist - 24.0, jitter, topLimit) < 0.5)
				lit++;
		}
		else if (!facingShade)
			lit++;
	}
	float shadow = (reached > 0 && lit == 0) ? 1.0 : 0.0;

	// hard shadow: pure black. Dither mode: coverage = density, ordered dither.
	float density = float((packedBits >> 3) & 127) / 100.0;
	float a = dither ? ((shadow > 0.15 ? density : 0.0) > Bayer4(ipos) ? 1.0 : 0.0) : (shadow > 0.15 ? 1.0 : 0.0);
	FragColor = vec4(0.0, 0.0, 0.0, a);
}
