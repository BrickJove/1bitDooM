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
layout(binding=2) uniform sampler2D FloorTexture;
layout(binding=3) uniform sampler2D CeilTexture;
layout(binding=4) uniform sampler2D FenceGridTexture;	// fence index + 1 per cell
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

// Is the position inside solid space (below the floor, above a ceiling or inside a wall)?
bool Blocked(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return false;
	float f = texelFetch(FloorTexture, c, 0).x;
	float ce = texelFetch(CeilTexture, c, 0).x;
	return w.y < f || w.y > ce;
}

// Is there an open sky above the position?
bool UnderSky(vec3 w)
{
	ivec2 c = CellOf(w);
	if (c.x < 0)
		return false;
	return texelFetch(CeilTexture, c, 0).x > Big * 0.5 && texelFetch(FloorTexture, c, 0).x < Big * 0.5;
}

float FenceParam(int id, int k)
{
	return texelFetch(FenceParamTexture, ivec2(k, id), 0).x;
}

// Does the ray segment S0 -> S1 hit an opaque pixel of the fence texture (chain link fence etc.) in the cell c?
bool HitsFence(ivec2 c, vec3 S0, vec3 S1)
{
	if (c.x < 0)
		return false;
	int id = int(texelFetch(FenceGridTexture, c, 0).x + 0.5) - 1;
	if (id < 0)
		return false;
	vec2 A = vec2(FenceParam(id, 0), FenceParam(id, 1));
	vec2 B = vec2(FenceParam(id, 2), FenceParam(id, 3));
	vec2 s = B - A;
	vec2 r = S1.xz - S0.xz;
	float rxs = r.x * s.y - r.y * s.x;
	if (abs(rxs) < 1.0e-6)
		return false;
	vec2 qp = A - S0.xz;
	float t = (qp.x * s.y - qp.y * s.x) / rxs;
	float u = (qp.x * r.y - qp.y * r.x) / rxs;
	if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0)
		return false;
	float y = mix(S0.y, S1.y, t);
	bool wrap = FenceParam(id, 10) > 0.5;
	if (!wrap && (y < FenceParam(id, 11) || y > FenceParam(id, 12)))
		return false;
	float v = (FenceParam(id, 4) - y) / max(FenceParam(id, 5), 1.0);
	if (wrap)
		v = fract(v);
	else if (v < 0.0 || v > 1.0)
		return false;
	float uu = fract((u * length(s) + FenceParam(id, 6)) * FenceParam(id, 7));
	vec2 apos = vec2(FenceParam(id, 8), FenceParam(id, 9)) + vec2(uu, v) * 63.0;
	return texelFetch(FenceAtlasTexture, ivec2(apos + 0.5), 0).x > 0.5;
}

// 1 = the ray P + D * t (0 < t < len) hits solid space
float Trace(vec3 P, vec3 D, float len, float jitter)
{
	float cell = 1.0 / Grid.z;
	float t0 = cell * 1.5;
	if (len <= t0)
		return 0.0;
	int maxSteps = int(Grid.w + 0.5) >> 9;
	int steps = int(clamp(ceil((len - t0) / cell), 8.0, float(maxSteps)));
	float dt = (len - t0) / float(steps);
	vec3 prev = P + D * t0;
	for (int i = 0; i < 160; i++)
	{
		if (i >= steps)
			break;
		vec3 S = P + D * (t0 + (float(i) + jitter) * dt);
		if (Blocked(S))
			return 1.0;
		ivec2 c = CellOf(S);
		if (HitsFence(c, prev, S))
			return 1.0;
		ivec2 cp = CellOf(prev);
		if (cp != c && HitsFence(cp, prev, S))
			return 1.0;
		prev = S;
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
	gGridSize = textureSize(FloorTexture, 0);

	ivec2 ipos = TexelOf(TexCoord);
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);
	if (IsSkyPixel(ipos, ns))
	{
		FragColor = vec4(0.0);
		return;
	}

	int packed = int(Grid.w + 0.5);
	int flags = packed & 3;
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
	float density = float((packed >> 2) & 127) / 100.0;
	float a = dither ? ((shadow > 0.15 ? density : 0.0) > Bayer4(ipos) ? 1.0 : 0.0) : (shadow > 0.15 ? 1.0 : 0.0);
	FragColor = vec4(0.0, 0.0, 0.0, a);
}
