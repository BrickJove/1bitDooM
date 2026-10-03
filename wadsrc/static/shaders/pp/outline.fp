/*
** outline.fp
**
** Jupiter3D: screen space geometry outlines.
**
** Pass 1 of 3: writes a one pixel wide edge mask (R = strength * range fade).
** outline_dilate.fp then widens it to the configured line width (so lines are
** exactly N pixels wide in every direction) and draws it over the scene.
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

bool HasNormal(vec3 n)
{
	return dot(n, n) > 0.01;
}

// 1.0 = same direction. Neighbours nearer than the centre or without
// normal are ignored, so the line stays on the nearer surface.
float NormalDot(vec3 nc, float ic, ivec2 p)
{
	vec3 n = FetchNormal(p);
	if (!HasNormal(n) || InvDepth(p) > ic * 1.02)
		return 1.0;
	return dot(nc, normalize(n));
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
	float ic = InvDepth(ipos);
	float dist = 1.0 / max(ic, 1.0e-8);
	if (centerSample.a < 0.5 || !HasNormal(centerSample.xyz * 2.0 - 1.0) || (LineRange > 0.0 && dist > LineRange))
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	float rangeFade = LineRange > 0.0 ? 1.0 - smoothstep(LineRange * 0.7, LineRange, dist) : 1.0;

	const ivec2 dx = ivec2(1, 0);
	const ivec2 dy = ivec2(0, 1);

	// depth silhouettes / convex edges (drawn on the nearer pixel only)
	float lapX = InvDepth(ipos + dx) + InvDepth(ipos - dx) - 2.0 * ic;
	float lapY = InvDepth(ipos + dy) + InvDepth(ipos - dy) - 2.0 * ic;
	float ridge = max(-lapX, -lapY) / max(ic, 1.0e-8);
	float depthEdge = smoothstep(DepthThreshold, DepthThreshold * 1.5, ridge);

	// normal creases: only against the left / upper neighbour, so a crease is seen
	// from exactly one side and the mask is one pixel wide
	float normalEdge = 0.0;
	vec3 nc = FetchNormal(ipos);
	if (HasNormal(nc))
	{
		nc = normalize(nc);
		float m = NormalDot(nc, ic, ipos - dx);
		m = min(m, NormalDot(nc, ic, ipos - dy));
		normalEdge = 1.0 - smoothstep(NormalThreshold - 0.08, NormalThreshold, m);
	}

	FragColor = vec4(max(depthEdge, normalEdge) * rangeFade, 0.0, 0.0, 1.0);
}
