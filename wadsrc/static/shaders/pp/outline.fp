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

// Fixed reference direction used to decide on which side of a crease the line is drawn.
const vec3 SideRef = vec3(0.3714, 0.8281, 0.4152);

// 1.0 = same direction. Neighbours nearer than the centre or without
// normal are ignored, so the line stays on the nearer surface.
// A crease between two surfaces triggers on both sides (the depth is continuous there),
// which gives a double line. So only the side whose normal ranks higher along SideRef
// draws it: every pixel of the crease makes the same choice, the line is exactly
// LineWidth pixels wide and cannot break up.
float NormalDot(vec3 nc, float fc, float ic, ivec2 p)
{
	vec4 ns = texelFetch(NormalTexture, ClampPos(p), 0);
	vec3 n = ns.xyz * 2.0 - 1.0;
	// models, model shadows and masked parts have alpha 0 and are not map geometry
	if (ns.a < 0.5 || !HasNormal(n) || InvDepth(p) > ic * 1.02)
		return 1.0;
	n = normalize(n);
	if (dot(n, SideRef) > fc + 0.002)
		return 1.0;
	return dot(nc, n);
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
	if (centerSample.a < 0.5 || !HasNormal(centerSample.xyz * 2.0 - 1.0) || (LineRange > 0.0 && dist > LineRange))
	{
		FragColor = vec4(0.0);
		return;
	}
	float rangeFade = LineRange > 0.0 ? 1.0 - smoothstep(LineRange * 0.7, LineRange, dist) : 1.0;

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
		float fc = dot(nc, SideRef);
		float m = NormalDot(nc, fc, ic, ipos + dx);
		m = min(m, NormalDot(nc, fc, ic, ipos - dx));
		m = min(m, NormalDot(nc, fc, ic, ipos + dy));
		m = min(m, NormalDot(nc, fc, ic, ipos - dy));
		normalEdge = 1.0 - smoothstep(NormalThreshold - 0.08, NormalThreshold, m);
	}

	float edge = max(depthEdge, normalEdge) * LineAlpha * rangeFade;
	FragColor = vec4(LineR, LineG, LineB, edge);
}
