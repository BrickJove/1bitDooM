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
#else
layout(binding=0) uniform sampler2D DepthTexture;
layout(binding=1) uniform sampler2D NormalTexture;
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

// surface class of 3D models (written by main.fp)
bool IsModelClass(float a)
{
	return a > 0.1 && a < 0.25;
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

void main()
{
	vec2 uv = Offset + TexCoord * Scale;

#if defined(MULTISAMPLE)
	gTexSize = textureSize(DepthTexture);
#else
	gTexSize = textureSize(DepthTexture, 0);
#endif

	ivec2 ipos = ivec2(uv * vec2(gTexSize));
	int w = int(LineWidth);

	ivec2 dx = ivec2(w, 0);
	ivec2 dy = ivec2(0, w);

	// Only map geometry gets lines: models carry alpha 0, sprites have no normal.
	vec4 centerSample = texelFetch(NormalTexture, ClampPos(ipos), 0);
	float ic = InvDepth(ipos);
	float dist = 1.0 / max(ic, 1.0e-8);
	// 3D models (surface class ~0.1667) beyond ModelInnerDist: 1 pixel line on the inside of the silhouette,
	// i.e. on model pixels that touch something that is not a model.
	if (ModelInnerDist > 0.0 && IsModelClass(centerSample.a) && dist > ModelInnerDist)
	{
		bool inner = !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos + ivec2(1, 0)), 0).a)
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos - ivec2(1, 0)), 0).a)
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos + ivec2(0, 1)), 0).a)
		          || !IsModelClass(texelFetch(NormalTexture, ClampPos(ipos - ivec2(0, 1)), 0).a);
		FragColor = vec4(LineR, LineG, LineB, inner ? LineAlpha : 0.0);
		return;
	}
	if (MapLines < 0.5)
	{
		FragColor = vec4(0.0);
		return;
	}

	if (centerSample.a < 0.5 || !HasNormal(centerSample.xyz * 2.0 - 1.0) || (LineRange > 0.0 && dist > LineRange))
	{
		FragColor = vec4(0.0);
		return;
	}
	if (SkyAt(ipos, ic))
	{
		FragColor = vec4(0.0);
		return;
	}
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

	float edge = max(depthEdge, creaseEdge) * LineAlpha * rangeFade;
	FragColor = vec4(LineR, LineG, LineB, edge);
}
