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
layout(binding=8) uniform sampler2D SectorLightTexture;	// light level (0..255) per sector, 4096 per row
layout(binding=7) uniform sampler2D LightTexture;	// 3 rows: x, y, z, radius of the nearest dynamic lights
layout(binding=6) uniform sampler2D FenceAtlasTexture;	// alpha masks, 256 x 256 per fence texture (8 x 8 slots)

const float Big = 100000.0;
const float SolidOff = 200000.0;	// solid cells: floor channel = height + SolidOff
const float NearSkyMark = 90000.0;

ivec2 gTexSize;
ivec2 gGridSize;
bool gFences = true;
float gBayer = 0.5;	// ordered dither threshold of the screen pixel
float gPix = 1.0;	// world size of one screen pixel at the depth of the pixel
float gFoot = 1.0;	// world size of the footprint of the pixel on the receiving surface
float gRayLen = 1.0;	// length of the ray of the shadow test
vec3 gLightPos = vec3(0.0);	// the source of the ray of the shadow test (the real position of the light, not the end of the ray)
vec3 gLightWorld = vec3(0.0);	// set by the caller before Trace
vec3 gSurf = vec3(0.0);	// exact position of the pixel on its surface (the origin above is lifted off the surface)
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

// the count channel carries the number of lines of the cell (low 7 bits) and the sector index + 1 (above)
int CellCount(ivec2 c)
{
	return int(CellValue(c, 3) + 0.5) & 127;
}

// light level of the sector at a position, 0..1 (0 when the position is in no sector)
float SectorAmbient(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return 0.0;
	int sec = (int(CellValue(c, 3) + 0.5) >> 7) - 1;
	if (sec < 0)
		return 0.0;
	return float(int(texelFetch(SectorLightTexture, ivec2(sec & 4095, sec >> 12), 0).x + 0.5) & 255) / 255.0;
}

// which lights reach the sector at a position (bit 0..2: the scene lights, 3..6: the model lights), all when unknown
int SectorLightMask(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return 127;
	int sec = (int(CellValue(c, 3) + 0.5) >> 7) - 1;
	if (sec < 0)
		return 127;
	return (int(texelFetch(SectorLightTexture, ivec2(sec & 4095, sec >> 12), 0).x + 0.5) >> 8) & 127;
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

// Coverage (0 or 1) of the fence texture at uv inside the slot of the atlas (256 x 256 texels, bilinear, 30 % = wire)
float FenceCover(vec2 slot0, vec2 uv)
{
	vec2 fp = clamp(uv * 256.0 - 0.5, vec2(0.0), vec2(254.999));
	ivec2 ip = ivec2(floor(fp));
	vec2 fr = fp - vec2(ip);
	ivec2 b0 = ivec2(slot0) + ip;
	float c00 = texelFetch(FenceAtlasTexture, b0, 0).x;
	float c10 = texelFetch(FenceAtlasTexture, b0 + ivec2(1, 0), 0).x;
	float c01 = texelFetch(FenceAtlasTexture, b0 + ivec2(0, 1), 0).x;
	float c11 = texelFetch(FenceAtlasTexture, b0 + ivec2(1, 1), 0).x;
	return mix(mix(c00, c10, fr.x), mix(c01, c11, fr.x), fr.y) > 0.3 ? 1.0 : 0.0;
}

// Does the fence texture cast a shadow at the crossing point (A -> B is the line, u the position along it, y the height)?
// foot = world size of the footprint of the pixel. Finer than a texel: the exact texture. Larger: the coverage of the
// footprint (a few samples, for very large footprints the mean coverage of the texture) is shown with the ordered dither
// of the screen, so far away the lattice becomes a regular gray instead of noise or a black area.
bool FencePixel(int id, vec2 A, vec2 B, float u, float y, float foot)
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
	float uu = (u * length(B - A) + FenceParam(id, 6)) * FenceParam(id, 7);
	vec2 slot0 = vec2(FenceParam(id, 8), FenceParam(id, 9));
	// footprint in texels of the texture (the scale of the texture and of the wall is in the parameters)
	float fpU = foot * FenceParam(id, 7) * 256.0;
	float fpV = foot * 256.0 / max(FenceParam(id, 5), 1.0);
	float fpT = max(fpU, fpV);
	if (fpT <= 1.0)
		return FenceCover(slot0, vec2(fract(uu), v)) > 0.5;
	int nU = int(clamp(ceil(fpU), 1.0, 6.0)), nV = int(clamp(ceil(fpV), 1.0, 6.0));
	float c = 0.0;
	for (int i = 0; i < nU; i++)
		for (int j = 0; j < nV; j++)
		{
			vec2 o = (vec2((float(i) + 0.5) / float(nU), (float(j) + 0.5) / float(nV)) - 0.5) * vec2(fpU, fpV) / 256.0;
			c += FenceCover(slot0, vec2(fract(uu + o.x + 4.0), clamp(v + o.y, 0.0, 1.0)));
		}
	c /= float(nU * nV);
	// crisp edges (threshold 0.5) as long as the lattice can be resolved; the ordered dither only fades in together with the
	// mean coverage far away (a dither threshold at the edges of the lattice made grainy stripes)
	float w = clamp((fpT - 4.0) / 6.0, 0.0, 1.0);
	c = mix(c, FenceParam(id, 14), w);
	return c > mix(0.5, gBayer, w);
}

// Do the lines in the cell block the ray piece S0 -> S1 (exact segment tests)?
bool LinesBlock(ivec2 c, vec3 S0, vec3 S1)
{
	int count = CellCount(c);
	int start = int(CellValue(c, 2) + 0.5);
	vec2 r = S1.xz - S0.xz;
	float tol = max(0.3 / max(length(r), 0.01), 1.0e-5);
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
		// a line up to 0.3 units behind the start of the piece still counts (the depth of the pixel is not exact, a pixel at the
		// foot of a wall can lie a little on the wrong side of its line)
		if (t < -tol || t > 1.00001 || u < -1.0e-5 || u > 1.00001)
			continue;
		bool behind = t < -1.0e-5;	// only the tolerance reaches this line: counts for walls, not for fences
		float y = mix(S0.y, S1.y, clamp(t, 0.0, 1.0));
		if (y < texelFetch(LineTexture, base + ivec2(4, 0), 0).x || y > texelFetch(LineTexture, base + ivec2(5, 0), 0).x)
			return true;
		if (behind)
			continue;
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
				// Direction fence -> light. The light is usually lower than the top of the fence, which would throw the top edge
				// of the fence infinitely far (or below the floor), so the elevation is limited: the shadow of the whole fence
				// (bottom edge to top edge) is at most twice as long as the fence is high.
				vec3 dl = gLightPos - vec3(0.5 * (A.x + B.x), FenceParam(fence, 11), 0.5 * (A.y + B.y));
				float hl = max(length(dl.xz), 1.0e-3);
				// A pixel below the foot of the fence (the fence stands on the edge of a raised sector, the pixel is on the lower floor):
				// the wall under the fence casts the shadow up to the point where the ray from the pixel just passes the foot of the fence.
				// The shadow of the fence has to go on from exactly there, so the real elevation (foot of the fence -> light) is used,
				// only protected against a light at the height of the foot.
				float minElev = gSurf.y < FenceParam(fence, 11) - 4.0 ? 0.05 : 0.5;
				vec3 dir = normalize(vec3(dl.x, max(dl.y, minElev * hl), dl.z));
				float dn = dot(nf, dir.xz);
				if (abs(dn) > 0.04)
				{
					float st = dot(nf, A - gSurf.xz) / dn;
					vec3 Hp = gSurf + dir * st;
					// sideways the shadow fans out like a real projection from the light (the ray pixel -> light decides where
					// the pixel meets the fence), only the height keeps the parallel projection above, so that the end of the
					// shadow is the top edge of the fence
					vec2 rd = gLightPos.xz - gSurf.xz;
					float rn = dot(nf, rd);
					float up = -1.0;
					if (abs(rn) > 1.0e-4)
						up = dot(gSurf.xz + rd * (dot(nf, A - gSurf.xz) / rn) - A, sd) / flen;
					if (st > 0.0 && up >= 0.0 && up <= 1.0)
					{
						if (FencePixel(fence, A, B, up, Hp.y, max(gPix, gFoot)))
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
	gLightPos = gLightWorld;
	// the ray skips the start of its way; limited, because on a big map the cell is large and a wall closer than the skipped piece
	// would be missed (lit gap between the foot of the wall and its shadow)
	float t0 = min(cell * 0.25, 0.02);
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
		if (CellCount(c) > 0)
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
		int count = CellCount(c);
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
			if (sc.x < 0 || CellCount(sc) > 0)
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
	// start of the rays: just off the surface. The cell size depends on the size of the map (30..50 units on a big map), an offset
	// of that size would let the rays start above low walls and boxes, so it is limited
	vec3 Q = P + n * min(cell * 1.5, 0.5);
	gOrigin = Q;
	gSurf = P;
	// no fence shadows on ceilings; the footprint of the pixel on its surface (long on floors seen at a grazing angle)
	if (hasNormal && n.y < -0.5)
		gFences = false;
	{
		vec3 camp = vec3(CamX.w, CamY.w, CamZ.w);
		float pix = z * ProjParams.w * 2.0 / float(gTexSize.y);
		gPix = pix;
		gFoot = pix / (hasNormal ? max(abs(dot(n, normalize(P - camp))), 0.05) : 1.0);
	}
	// start point of the rays, a bit off the surface
	float jitter = Bayer4(ipos);
	gBayer = jitter;
	float topLimit = float(packedBits >> 10);

	// A pixel is black when at least one source reaches it (sun: pixel under an open sky, light: within its radius)
	// and every source that reaches it is blocked (the surface faces away, or the ray hits the map).
	int reached = 0;
	int lit = 0;
	int dbg = 0;	// debug colours: 1 lit, 2 blocked by the map, 3 faces away
	int blocked = 0;	// sources that reach the pixel but are blocked (like the flat shadow of a model: one blocked source is enough)
	bool nearLit = false;

	// sun: a point light above the centre of the level
	// The shadows of the map are projected like the flat shadows of the models (models.cpp): from the light lowered by the
	// reference height of the casters, so a model and a wall of the same height cast shadows of the same length.
	float casterH = (Sun.w > 0.5 ? Sun.w - 1.0 : Sun.w) * 1000.0;
	if (Sun.w > 0.5 && (UnderSky(Q) || (hasNormal && UnderSky(P + n * (cell * 3.5)))))
	{
		vec3 sunP = vec3(Sun.x, max(Sun.y - casterH, min(Sun.y, Q.y + 4.0)), Sun.z);
		vec3 toS = sunP - Q;
		float dS = length(toS);
		if (dS > 8.0)
		{
			reached++;
			vec3 D = toS / dS;
			float facing = hasNormal ? dot(n, D) : 1.0;
			if (facing > 0.0)
			{
				gLightWorld = sunP;
				if (Trace(Q, D, dS, jitter, topLimit) < 0.5)
				{
					lit++;
					dbg = 1;
				}
				else
				{
					blocked++;
					dbg = 2;
				}
			}
			else if (!facingShade)
				lit++;
			else
			{
				blocked++;
				dbg = 3;
			}
		}
	}

	// the nearest dynamic lights (surfaces close to a light are always lit by it: the light and its fixture)
	int reachMask = Light.x > 0.5 ? SectorLightMask(Q) : 127;
	for (int i = 0; i < 3; i++)
	{
		if (float(i) >= Light.x)
			break;
		if ((reachMask & (1 << i)) == 0)
			continue;	// behind a wall (another room): this light makes no shadow there
		vec4 L = vec4(texelFetch(LightTexture, ivec2(0, i), 0).x, texelFetch(LightTexture, ivec2(1, i), 0).x,
		              texelFetch(LightTexture, ivec2(2, i), 0).x, texelFetch(LightTexture, ivec2(3, i), 0).x);
		if (L.w <= 0.0)
			continue;
		vec3 lightP = vec3(L.x, max(L.y - casterH, min(L.y, Q.y + 4.0)), L.z);
		vec3 toL = lightP - Q;
		float dist = length(toL);
		if (dist >= L.w)
			continue;
		reached++;
		if (dist <= 64.0)
		{
			lit++;
			nearLit = true;
			continue;
		}
		vec3 D = toL / dist;
		float facing = hasNormal ? dot(n, D) : 1.0;
		if (facing > 0.0)
		{
			gLightWorld = lightP;
			if (Trace(Q, D, dist - 24.0, jitter, topLimit) < 0.5)
				lit++;
			else
				blocked++;
		}
		else if (!facingShade)
			lit++;
		else
			blocked++;
	}
	if (Light.z > 1.5)
	{
		FragColor = reached == 0 ? vec4(0.0, 0.0, 1.0, 0.6) : (dbg == 1 ? vec4(0.0, 1.0, 0.0, 0.35) : (dbg == 2 ? vec4(1.0, 0.0, 0.0, 0.8) : (dbg == 3 ? vec4(1.0, 1.0, 0.0, 0.8) : vec4(0.0))));
		return;
	}
	float shadow = (reached > 0 && blocked > 0 && !nearLit) ? 1.0 : 0.0;

	// hard shadow: pure black. Dither mode: coverage = density, ordered dither.
	float density = float((packedBits >> 3) & 127) / 100.0;
	// the light level of the sector weakens the shadow: a lit sector has nothing that a light could still brighten
	float remain = 1.0;
	if (Light.w > 0.0 && shadow > 0.15)
		remain = 1.0 - Light.w * SectorAmbient(Q);
	float a = dither ? ((shadow > 0.15 ? density * remain : 0.0) > Bayer4(ipos) ? 1.0 : 0.0) : (shadow > 0.15 && remain > 0.5 ? 1.0 : 0.0);
	if (Light.z > 0.5 && ((ipos.x + ipos.y) & 1) != 0)
		a = 0.0;	// checkerboard style: only every second pixel of the shadow is black
	FragColor = vec4(0.0, 0.0, 0.0, a);
}
