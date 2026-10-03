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

// Raw edge strength 0..1 at a pixel (before alpha / range fade), 0 for anything that
// does not get lines. w = tap distance in pixels.
float EdgeAt(ivec2 ipos, int w, out float rangeFade)
{
	rangeFade = 1.0;
	ivec2 dx = ivec2(w, 0);
	ivec2 dy = ivec2(0, w);

	// Only map geometry gets lines: models carry alpha 0, sprites have no normal.
	vec4 centerSample = texelFetch(NormalTexture, ClampPos(ipos), 0);
	float ic = InvDepth(ipos);
	float dist = 1.0 / max(ic, 1.0e-8);
	if (centerSample.a < 0.5 || !HasNormal(centerSample.xyz * 2.0 - 1.0) || (LineRange > 0.0 && dist > LineRange))
		return 0.0;
	rangeFade = LineRange > 0.0 ? 1.0 - smoothstep(LineRange * 0.7, LineRange, dist) : 1.0;

	// depth silhouettes / convex edges
	float lapX = InvDepth(ipos + dx) + InvDepth(ipos - dx) - 2.0 * ic;
	float lapY = InvDepth(ipos + dy) + InvDepth(ipos - dy) - 2.0 * ic;
	float ridge = max(-lapX, -lapY) / max(ic, 1.0e-8);
	float depthEdge = smoothstep(DepthThreshold, DepthThreshold * 1.5, ridge);

	// normal creases
	float normalEdge = 0.0;
	vec3 nc = FetchNormal(ipos);
	if (HasNormal(nc))
	{
		nc = normalize(nc);
		float m = NormalDot(nc, ic, ipos + dx);
		m = min(m, NormalDot(nc, ic, ipos - dx));
		m = min(m, NormalDot(nc, ic, ipos + dy));
		m = min(m, NormalDot(nc, ic, ipos - dy));
		normalEdge = 1.0 - smoothstep(NormalThreshold - 0.08, NormalThreshold, m);
	}

	// quantised so equal strengths on both sides of a crease compare as equal
	return floor(max(depthEdge, normalEdge) * 32.0 + 0.5) / 32.0;
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

	float rangeFade;
	float s = EdgeAt(ipos, w, rangeFade);
	if (s <= 0.0)
	{
		FragColor = vec4(0.0);
		return;
	}

	// One pixel lines: a crease between floor and wall triggers on both sides of the
	// crease (the depth is continuous there), which gives 2 px wide, stair-stepped lines.
	// Keep only the local maximum; ties go to the left / upper pixel, so the choice is
	// the same for every pixel of the line and no steps appear.
	if (w <= 1)
	{
		float r;
		if (EdgeAt(ipos + ivec2(1, 0), 1, r) > s || EdgeAt(ipos + ivec2(0, 1), 1, r) > s ||
			EdgeAt(ipos - ivec2(1, 0), 1, r) >= s || EdgeAt(ipos - ivec2(0, 1), 1, r) >= s)
		{
			FragColor = vec4(0.0);
			return;
		}
	}

	FragColor = vec4(LineR, LineG, LineB, s * LineAlpha * rangeFade);
}
