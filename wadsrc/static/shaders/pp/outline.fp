/*
** outline.fp
**
** Jupiter3D: screen space geometry outlines.
**
** Silhouettes and convex edges: second derivative of inverse linear depth.
** 1/z is affine in screen space for planar surfaces, so flat floors and
** walls give 0 at any viewing angle and only real depth steps trigger.
** Concave creases: angle between neighbouring eye space normals.
*/

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 FragColor;

#if defined(MULTISAMPLE)
layout(binding=0) uniform sampler2DMS DepthTexture;
layout(binding=1) uniform sampler2DMS NormalTexture;
layout(binding=2) uniform sampler2DMS FogTexture;
#else
layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
layout(binding=2) uniform sampler2D FogTexture;
#endif

ivec2 gTexSize;

ivec2 ClampPos(ivec2 p)
{
	return clamp(p, ivec2(0), gTexSize - ivec2(1));
}

// inverse linear depth (affine in screen space on planes)
float InvDepth(ivec2 p)
{
	float d = texelFetch(DepthTexture, ClampPos(p), 0).x;
	return d * LinearizeDepthA + LinearizeDepthB;
}

vec3 FetchNormal(ivec2 p)
{
	return texelFetch(NormalTexture, ClampPos(p), 0).xyz * 2.0 - 1.0;
}

// Sky never gets lines, and a sky neighbour is no silhouette. The sky dome sits on the far
// plane and is tagged with class 1/3 in the normal buffer. The tag matters because "sky
// walls" (the sky hack at the map border) write their real depth afterwards, but not the normal.
bool IsSky(float inv)
{
	return inv <= (LinearizeDepthA + LinearizeDepthB) * 1.05;
}

bool SkyAt(ivec2 p, float inv)
{
	float a = texelFetch(NormalTexture, ClampPos(p), 0).a;
	return IsSky(inv) || (a > 0.2 && a < 0.45);
}

// 3D models are marked in the normal buffer by alpha 0 and an impossible normal (1,1,1)
// (the alpha channel has only 2 bits, so there is no spare class level for them).
bool IsModelClass(vec4 s)
{
	return s.a < 0.1 && s.r > 0.99 && s.g > 0.99 && s.b > 0.99;
}

bool HasNormal(vec3 n)
{
	return dot(n, n) > 0.01;
}

// Alpha of the normal buffer holds the surface class: 2/3 = wall, 1 = floor / ceiling,
// 0 = model, sprite or masked texture (never outlined).
const float FlatClass = 0.83;

// Is there a floor / ceiling pixel at p that the wall pixel (normal nc, inverse depth ic)
// touches? 1.0 = yes. Flats that are much nearer are ignored: that is a ledge, the depth
// silhouette test draws it on the nearer (floor) side.
float FlatNeighbour(vec3 nc, float ic, ivec2 p)
{
	vec4 ns = texelFetch(NormalTexture, ClampPos(p), 0);
	vec3 n = ns.xyz * 2.0 - 1.0;
	if (ns.a < FlatClass || !HasNormal(n) || InvDepth(p) > ic * 1.12)
		return 0.0;
	return 1.0 - smoothstep(NormalThreshold - 0.08, NormalThreshold, dot(nc, normalize(n)));
}

// Top edge of a wall under a sky ceiling: above it sit the sky wall quads (portal depth with the
// sky tag, same distance as the wall). Far dome pixels do not count (that is open sky).
float SkyQuadNeighbour(float ic, ivec2 p)
{
	float ni = InvDepth(p);
	float a = texelFetch(NormalTexture, ClampPos(p), 0).a;
	return (a > 0.2 && a < 0.45 && !IsSky(ni) && abs(ni - ic) < 0.12 * ic) ? 1.0 : 0.0;
}

// Strength (0..1) of the map geometry line at a pixel.
float MapEdge(ivec2 ipos)
{
	int w = int(LineWidth);

	ivec2 dx = ivec2(w, 0);
	ivec2 dy = ivec2(0, w);

	vec4 centerSample = texelFetch(NormalTexture, ClampPos(ipos), 0);
	float ic = InvDepth(ipos);
	float dist = 1.0 / max(ic, 1.0e-8);
	if (MapLines < 0.5)
		return 0.0;

	if (centerSample.a < 0.5 || !HasNormal(centerSample.xyz * 2.0 - 1.0) || (LineRange > 0.0 && dist > LineRange))
		return 0.0;
	if (SkyAt(ipos, ic))
		return 0.0;
	float rangeFade = LineRange > 0.0 ? 1.0 - smoothstep(LineRange * 0.7, LineRange, dist) : 1.0;

	// depth silhouettes / convex edges
	// (an axis with a sky neighbour is skipped: the edge towards the sky gets no line)
	float ix1 = InvDepth(ipos + dx), ix2 = InvDepth(ipos - dx);
	float iy1 = InvDepth(ipos + dy), iy2 = InvDepth(ipos - dy);
	float lapX = (SkyAt(ipos + dx, ix1) || SkyAt(ipos - dx, ix2)) ? 0.0 : ix1 + ix2 - 2.0 * ic;
	float lapY = (SkyAt(ipos + dy, iy1) || SkyAt(ipos - dy, iy2)) ? 0.0 : iy1 + iy2 - 2.0 * ic;
	float ridge = max(-lapX, -lapY) / max(ic, 1.0e-8);
	float depthEdge = smoothstep(DepthThreshold, DepthThreshold * 1.5, ridge);

	// Creases: only where a wall meets a floor or ceiling, drawn on the wall side, so the line
	// runs along the bottom and top edge of walls. Wall-wall corners get no line from here;
	// an outer corner with background behind it is a depth silhouette (above).
	float creaseEdge = 0.0;
	if (centerSample.a < FlatClass)
	{
		vec3 nc = FetchNormal(ipos);
		if (HasNormal(nc))
		{
			nc = normalize(nc);
			creaseEdge = FlatNeighbour(nc, ic, ipos + dx);
			creaseEdge = max(creaseEdge, FlatNeighbour(nc, ic, ipos - dx));
			creaseEdge = max(creaseEdge, FlatNeighbour(nc, ic, ipos + dy));
			creaseEdge = max(creaseEdge, FlatNeighbour(nc, ic, ipos - dy));
		}
		creaseEdge = max(creaseEdge, max(SkyQuadNeighbour(ic, ipos + dy), SkyQuadNeighbour(ic, ipos - dy)));
	}

	// Extended mode: also the border of walls against the sky (top edge under an open sky) and
	// concave wall-wall corners (inner corners). The corner line is only taken from the +x / +y
	// neighbour so that it stays one pixel wide.
	float extraEdge = 0.0;
	if (ExtraLines > 0.5 && centerSample.a < FlatClass)
	{
		if (SkyAt(ipos + dx, ix1) || SkyAt(ipos - dx, ix2) || SkyAt(ipos + dy, iy1) || SkyAt(ipos - dy, iy2))
			extraEdge = 1.0;

		vec3 nc2 = FetchNormal(ipos);
		if (HasNormal(nc2))
		{
			nc2 = normalize(nc2);
			float rawLapX = ix1 + ix2 - 2.0 * ic;
			float rawLapY = iy1 + iy2 - 2.0 * ic;
			for (int k = 0; k < 2; k++)
			{
				ivec2 q = ipos + (k == 0 ? dx : dy);
				float qi = k == 0 ? ix1 : iy1;
				vec4 s = texelFetch(NormalTexture, ClampPos(q), 0);
				vec3 n = s.xyz * 2.0 - 1.0;
				float lap = k == 0 ? rawLapX : rawLapY;
				if (s.a > 0.5 && s.a < FlatClass && HasNormal(n) && abs(qi - ic) < 0.05 * ic && lap >= 0.0
				    && dot(nc2, normalize(n)) < NormalThreshold)
					extraEdge = 1.0;
			}
		}
	}

	return max(max(depthEdge, creaseEdge), extraEdge) * LineAlpha * rangeFade;
}

// ---- line clean up (gl_outline_merge) -------------------------------------------------------------
// Goal: every straight line is exactly one pixel wide, without gaps and without a second line next to it.
//  1. hard lines: a pixel is a line or not (no half transparent pixels that the 1-bit conversion drops)
//  2. hysteresis: a weak pixel next to a strong one belongs to the line (depth noise makes lines flicker)
//  3. gap filling: a gap of 1-2 pixels between two line pixels of the same row / column is closed
//  4. thinning: a band two pixels thick keeps only its upper / left row
//  5. parallel lines 2-3 pixels apart: only one of them is kept

bool Strong(ivec2 p)
{
	return MapEdge(p) >= 0.5 * LineAlpha;
}

bool GapAlong(ivec2 p, ivec2 axis)
{
	if (Strong(p - axis))
		return Strong(p + axis) || Strong(p + axis * 2);
	return Strong(p - axis * 2) && Strong(p + axis);
}

// two pixels thick: this pixel is part of a run and the pixel above / left of it is part of a run too
bool Thick(ivec2 p)
{
	ivec2 x = ivec2(1, 0), y = ivec2(0, 1);
	if (Strong(p - x) && Strong(p + x) && Strong(p - y) && Strong(p - y - x) && Strong(p - y + x))
		return true;
	if (Strong(p - y) && Strong(p + y) && Strong(p - x) && Strong(p - x - y) && Strong(p - x + y))
		return true;
	return false;
}

bool ParallelNeighbour(ivec2 p, float e)
{
	bool runH = MapEdge(p + ivec2(1, 0)) > 0.05 || MapEdge(p - ivec2(1, 0)) > 0.05;
	bool runV = MapEdge(p + ivec2(0, 1)) > 0.05 || MapEdge(p - ivec2(0, 1)) > 0.05;
	if (runH == runV)
		return false;
	ivec2 axis = runH ? ivec2(0, 1) : ivec2(1, 0);
	for (int d = 2; d <= 3; d++)
	{
		float up = MapEdge(p + axis * d);
		float down = MapEdge(p - axis * d);
		if (up > 0.05 && up >= e)
			return true;
		if (down > 0.05 && down > e)
			return true;
	}
	return false;
}

void main()
{
	vec2 uv = Offset + TexCoord * Scale;

#if defined(MULTISAMPLE)
	gTexSize = textureSize(DepthTexture);
#else
	gTexSize = textureSize(DepthTexture, 0);
#endif

	ivec2 ipos = ivec2(uv * vec2(gTexSize));

	// Only map geometry gets lines: models carry alpha 0, sprites have no normal.
	vec4 centerSample = texelFetch(NormalTexture, ClampPos(ipos), 0);
	float dist = 1.0 / max(InvDepth(ipos), 1.0e-8);
	// 3D models (surface class ~0.1667) beyond ModelInnerDist: 1 pixel line on the inside of the silhouette,
	// i.e. on model pixels that touch something that is not a model.
	if (ModelInnerDist > 0.0 && IsModelClass(centerSample) && dist > ModelInnerDist)
	{
		bool inner = !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos + ivec2(1, 0)), 0))
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos - ivec2(1, 0)), 0))
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos + ivec2(0, 1)), 0))
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos - ivec2(0, 1)), 0));
		FragColor = vec4(LineR, LineG, LineB, inner ? LineAlpha : 0.0);
		return;
	}

	float edge = MapEdge(ipos);
	if (MergeLines > 0.5)
	{
		bool line = edge >= 0.5 * LineAlpha;
		// hysteresis: weak pixel next to a strong one
		if (!line && edge > 0.15 * LineAlpha)
			line = Strong(ipos + ivec2(1, 0)) || Strong(ipos - ivec2(1, 0)) || Strong(ipos + ivec2(0, 1)) || Strong(ipos - ivec2(0, 1));
		// gaps of 1-2 pixels along a row or a column
		if (!line)
			line = GapAlong(ipos, ivec2(1, 0)) || GapAlong(ipos, ivec2(0, 1));
		if (line && Thick(ipos))
			line = false;
		if (line && ParallelNeighbour(ipos, max(edge, 0.5 * LineAlpha)))
			line = false;
		// fully opaque: half transparent pixels become coloured fringes in the 1-bit conversion
		edge = line ? 1.0 : 0.0;
	}

	vec3 lineColor = vec3(LineR, LineG, LineB);
	if (MergeLines > 0.5)
		lineColor = floor(lineColor + 0.5); // pure black / white, no grey or tinted lines
	// dark surfaces (the alpha of the fog buffer holds the surface brightness) get a white line.
	// Decided from the brightest pixel of the 3x3 neighbourhood, so the colour of a line does not
	// flip between black and white along its length when the texture varies.
	if (DarkLines > 0.0 && edge > 0.0)
	{
		float m = 0.0;
		for (int y = -1; y <= 1; y++)
			for (int x = -1; x <= 1; x++)
				m = max(m, texelFetch(FogTexture, ClampPos(ipos + ivec2(x, y)), 0).a);
		if (m < DarkLines)
			lineColor = vec3(1.0);
	}
	FragColor = vec4(lineColor, edge);
}
