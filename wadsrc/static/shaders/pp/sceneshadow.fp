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
layout(binding=6) uniform sampler2D FenceAtlasTexture;	// alpha masks, 64 x 64 per fence texture

const float Big = 100000.0;

ivec2 gTexSize;
ivec2 gGridSize;

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

// Is there an open sky above the position? Near walls the cell centre can lie in solid space, so the
// neighbouring cells are used then.
bool UnderSky(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return false;
	for (int i = 0; i < 5; i++)
	{
		ivec2 o = i == 0 ? ivec2(0) : (i == 1 ? ivec2(1, 0) : (i == 2 ? ivec2(-1, 0) : (i == 3 ? ivec2(0, 1) : ivec2(0, -1))));
		ivec2 n = c + o;
		if (n.x < 0 || n.y < 0 || n.x >= gGridSize.x || n.y >= gGridSize.y)
			continue;
		float f = CellValue(n, 0);
		if (f < Big * 0.5)
			return CellValue(n, 1) > Big * 0.5;
	}
	return false;
}

float FenceParam(int id, int k)
{
	return texelFetch(FenceParamTexture, ivec2(k, id), 0).x;
}

// Does the ray piece S0 -> S1 hit an opaque pixel of the fence texture at the crossing point (A -> B is the line, u the
// position along it)?
bool FencePixel(int id, vec2 A, vec2 B, float u, float y)
{
	bool wrap = FenceParam(id, 10) > 0.5;
	if (!wrap && (y < FenceParam(id, 11) || y > FenceParam(id, 12)))
		return false;
	float v = (FenceParam(id, 4) - y) / max(FenceParam(id, 5), 1.0);
	if (wrap)
		v = fract(v);
	else if (v < 0.0 || v > 1.0)
		return false;
	float uu = fract((u * length(B - A) + FenceParam(id, 6)) * FenceParam(id, 7));
	vec2 apos = vec2(FenceParam(id, 8), FenceParam(id, 9)) + vec2(uu, v) * 63.0;
	return texelFetch(FenceAtlasTexture, ivec2(apos + 0.5), 0).x > 0.5;
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
		if (fence >= 0 && FencePixel(fence, A, B, clamp(u, 0.0, 1.0), y))
			return true;
	}
	return false;
}

// 1 = the ray P + D * t (0 < t < len) hits something. The ray is walked cell by cell (exact grid traversal, no fixed
// steps and no jitter). Cells with lines are tested against the exact line segments (straight shadow edges that start
// at the corners of the geometry), the other cells against the floor and ceiling height.
float Trace(vec3 P, vec3 D, float len, float jitter)
{
	float cell = 1.0 / Grid.z;
	float t0 = cell * 0.25;
	if (len <= t0)
		return 0.0;
	vec3 A = P + D * t0;
	float tEnd = len - t0;

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
	for (int i = 0; i < 900; i++)
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
		else if (min(S0.y, S1.y) < CellValue(c, 0) || max(S0.y, S1.y) > CellValue(c, 1))
			return 1.0;
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
	int flags = packedBits & 3;
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

	float cell = 1.0 / Grid.z;
	vec3 Q = P + n * (cell * 1.5);	// start point of the rays, a bit off the surface
	float jitter = Bayer4(ipos);
	float shadow = 0.0;

	// sun: only under an open sky
	if (Sun.w > 0.0 && UnderSky(Q))
	{
		float facing = hasNormal ? dot(n, Sun.xyz) : 1.0;
		if (facing > 0.0)
			shadow = Trace(Q, Sun.xyz, Sun.w, jitter);
		else if (facingShade)
			shadow = 1.0;
	}

	// one dynamic light (more lights = more passes), within its radius only
	if (Light.w > 0.0 && shadow < 0.5)
	{
		vec3 toL = Light.xyz - Q;
		float dist = length(toL);
		if (dist < Light.w && dist > 8.0)
		{
			vec3 D = toL / dist;
			float atten = 1.0 - dist / Light.w;
			float facing = hasNormal ? dot(n, D) : 1.0;
			float s = 0.0;
			if (facing > 0.0)
				s = Trace(Q, D, dist - 4.0, jitter);
			else if (facingShade)
				s = 1.0;
			shadow = max(shadow, s * atten);
		}
	}

	// hard shadow: pure black. Dither mode: coverage = density, ordered dither.
	float density = float((packedBits >> 2) & 127) / 100.0;
	float a = dither ? ((shadow > 0.15 ? density : 0.0) > Bayer4(ipos) ? 1.0 : 0.0) : (shadow > 0.15 ? 1.0 : 0.0);
	FragColor = vec4(0.0, 0.0, 0.0, a);
}
