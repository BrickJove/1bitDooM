/*
** modelshadow.fp
**
** Jupiter3D: shadows of the models. The models were rendered into a depth map seen from each of the nearest dynamic
** lights (hw_modelshadow.cpp). For every pixel the shader rebuilds the world position from the depth buffer,
** projects it into the depth map of the light and paints it pure black when a model is nearer to the light.
** The light must reach the pixel: the ray to the light is marched through the height map of the level (as in
** sceneshadow.fp), so the shadows of the models do not go through walls.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
layout(binding=2) uniform sampler2D CellTexture;	// 4 floats per cell: floor, ceiling, list start, list count
layout(binding=3) uniform sampler2D ListTexture;	// line indices
layout(binding=4) uniform sampler2D LineTexture;	// 8 floats per line
layout(binding=5) uniform sampler2D ShadowMap0;
layout(binding=6) uniform sampler2D ShadowMap1;
layout(binding=7) uniform sampler2D ShadowMap2;
layout(binding=8) uniform sampler2D ShadowMap3;

const float Big = 100000.0;
const float SolidOff = 200000.0;

ivec2 gGridSize;
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
		// fences (masked mid textures) stop the shadow as well: the lattice would alias
		if (int(texelFetch(LineTexture, base + ivec2(6, 0), 0).x + 0.5) - 1 >= 0)
			return true;
	}
	return false;
}

// 1 = the ray P + D * t (0 < t < len) hits something. The ray is walked cell by cell (exact grid traversal, no fixed
// steps and no jitter). Cells with lines are tested against the exact line segments (straight shadow edges that start
// at the corners of the geometry), the other cells against the floor and ceiling height.
float Trace(vec3 P, vec3 D, float len, float jitter, float topLimit)
{
	float cell = 1.0 / Grid.z;
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
// 0 = no shadow of this light, 1 = shadow, 2 = a model is in the way but a wall blocks the light
int InModelShadow(vec3 P, vec3 Q, vec3 n, float topLimit, vec4 L, vec2 nf, sampler2D map)
{
	if (L.w <= 0.0)
		return 0;
	// surfaces that face away from the light cannot be seen from it: no shadow of this light (back of a wall, ...)
	if (dot(n, n) > 0.5 && dot(n, L.xyz - P) <= 0.0)
		return 0;
	vec3 d = P - L.xyz;
	float dist = length(d);
	if (dist >= L.w || dist < 12.0)
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
	float stored = texelFetch(map, tp, 0).x;
	if (stored >= 1.0)
		return 0;	// nothing was drawn here
	float occluder = LinearLightDepth(stored, nf.x, nf.y);
	float texel = 2.0 * m / float(size);	// world size of one depth map texel at this distance (90 degree field of view)
	if (m <= occluder + 2.0 * texel + 1.0)
		return 0;
	// a wall between the pixel and the light: the shadow stops there
	if (dist > 64.0 && Grid.z > 0.0)
	{
		vec3 D = (L.xyz - Q) / max(distance(L.xyz, Q), 1.0e-3);
		if (Trace(Q, D, distance(L.xyz, Q) - 24.0, 0.0, topLimit) > 0.5)
			return 2;
	}
	return 1;
}

void main()
{
	gTexSize = textureSize(DepthTexture, 0);
	gGridSize = ivec2(textureSize(CellTexture, 0).x / 4, textureSize(CellTexture, 0).y);
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

	// surface normal of the map (models and sprites have none), the position is refined as in sceneshadow.fp
	vec3 n = ns.xyz * 2.0 - 1.0;
	bool hasNormal = ns.a > 0.5 && dot(n, n) > 0.01;
	if (hasNormal)
		n = normalize(inverse(mat3(CamX.xyz, CamY.xyz, CamZ.xyz)) * n);
	else
		n = vec3(0.0);
	float cell = Grid.z > 0.0 ? 1.0 / Grid.z : 16.0;
	if (hasNormal && abs(n.y) > 0.95)
		n = vec3(0.0, n.y > 0.0 ? 1.0 : -1.0, 0.0);
	if (Grid.z > 0.0)
	{
		if (hasNormal && abs(n.y) > 0.95)
		{
			vec3 cam = vec3(CamX.w, CamY.w, CamZ.w);
			vec3 vd = normalize(P - cam);
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
		if (hasNormal && abs(n.y) < 0.3 && dot(n.xz, n.xz) > 0.5)
			SnapWall(P, n, cell);
	}
	vec3 Q = P + n * (cell * 1.5);
	float topLimit = Grid.w;

	// debug: what does the light of map k see? Everything it does not see gets a checkerboard.
	if (Flags.y > 0.5)
	{
		vec4 L = Flags.y < 1.5 ? Light0 : (Flags.y < 2.5 ? Light1 : (Flags.y < 3.5 ? Light2 : Light3));
		if (L.w > 0.0 && ((ipos.x + ipos.y) & 1) == 0)
		{
			bool hidden = dot(n, n) > 0.5 && dot(n, L.xyz - P) <= 0.0;
			if (!hidden && Grid.z > 0.0 && distance(L.xyz, Q) > 64.0)
			{
				vec3 D = (L.xyz - Q) / distance(L.xyz, Q);
				hidden = Trace(Q, D, distance(L.xyz, Q) - 24.0, 0.0, topLimit) > 0.5;
			}
			if (hidden)
			{
				FragColor = vec4(0.0, 0.0, 0.0, 1.0);
				return;
			}
		}
		discard;
	}
	int s0 = InModelShadow(P, Q, n, topLimit, Light0, NearFar.xy, ShadowMap0);
	int s1 = InModelShadow(P, Q, n, topLimit, Light1, NearFar.zw, ShadowMap1);
	int s2 = InModelShadow(P, Q, n, topLimit, Light2, NearFar2.xy, ShadowMap2);
	int s3 = InModelShadow(P, Q, n, topLimit, Light3, NearFar2.zw, ShadowMap3);
	if (s0 == 1 || s1 == 1 || s2 == 1 || s3 == 1)
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	if (Flags.x > 0.5 && (s0 == 2 || s1 == 2 || s2 == 2 || s3 == 2) && ((ipos.x + ipos.y) & 1) == 0)
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);	// debug: a model is in the way, but a wall stops the shadow
		return;
	}
	discard;
}
