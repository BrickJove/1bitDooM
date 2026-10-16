/*
** sceneshadow.fp
**
** Jupiter3D: screen space shadows from a sun and a few dynamic lights.
**
** For every pixel the shader rebuilds the eye space position from the depth buffer and marches
** a ray towards the light. The ray is occluded when, at some step, it lies slightly behind the
** surface the depth buffer shows at the projected pixel. Walls, floors, stairs and models all
** cast and receive shadows because they are all in the depth buffer. Only what is on screen can
** cast a shadow, and ceilings never do (otherwise every indoor area would be a shadow).
**
** The result is black with an alpha (soft) or a binary ordered dither (1 bit look), blended over
** the scene colour before the outline pass.
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

float InvDepthAt(ivec2 p)
{
	float d = texelFetch(DepthTexture, ClampPos(p), 0).x;
	return d * ProjParams.x + ProjParams.y;
}

// linear distance along the view axis
float LinDepthAt(ivec2 p)
{
	return 1.0 / max(InvDepthAt(p), 1.0e-8);
}

bool IsSkyPixel(ivec2 p, vec4 ns)
{
	return InvDepthAt(p) <= (ProjParams.x + ProjParams.y) * 1.05 || (ns.a > 0.2 && ns.a < 0.45);
}

// eye space (x right, y up, camera looks down -z), z = positive linear depth
vec3 EyePos(vec2 tc, float z)
{
	return vec3((tc * 2.0 - 1.0) * ProjParams.zw * z, -z);
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

// Is the ray P + D * t, t in (0, len), blocked by something on screen? 1 = blocked
float Trace(vec3 P, vec3 D, float len, float jitter)
{
	int steps = int(Params.w);
	float bias = Params.y;
	float thick = Params.z;
	for (int i = 0; i < 64; i++)
	{
		if (i >= steps)
			break;
		float u = (float(i) + jitter) / float(steps);
		float t = bias + (len - bias) * u * u;
		vec3 S = P + D * t;
		float rayZ = -S.z;
		if (rayZ < 1.0)
			break;
		vec2 tc = (S.xy / (ProjParams.zw * rayZ)) * 0.5 + 0.5;
		if (tc.x < 0.0 || tc.y < 0.0 || tc.x > 1.0 || tc.y > 1.0)
			break;	// left the screen: unknown, treated as lit

		ivec2 ip = TexelOf(tc);
		float sceneZ = LinDepthAt(ip);
		float diff = rayZ - sceneZ;
		if (diff > bias * 0.5 + rayZ * 0.004 && diff < thick + rayZ * 0.05)
		{
			vec4 ns = texelFetch(NormalTexture, ClampPos(ip), 0);
			if (IsSkyPixel(ip, ns))
				continue;
			// ceilings (and everything facing down) never cast shadows
			vec3 n = ns.xyz * 2.0 - 1.0;
			if (ns.a > 0.8 && dot(n, n) > 0.01 && dot(normalize(n), Down.xyz) > 0.7)
				continue;
			return 1.0;
		}
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

	ivec2 ipos = TexelOf(TexCoord);
	vec4 ns = texelFetch(NormalTexture, ClampPos(ipos), 0);
	if (IsSkyPixel(ipos, ns))
	{
		FragColor = vec4(0.0);
		return;
	}

	int flags = int(Down.w + 0.5);
	bool facingShade = (flags & 1) != 0;
	bool dither = (flags & 2) != 0;

	float z = LinDepthAt(ipos);
	vec3 P = EyePos(TexCoord, z);

	// Surface normal (not available for models / sprites)
	vec3 n = ns.xyz * 2.0 - 1.0;
	bool hasNormal = ns.a > 0.5 && dot(n, n) > 0.01;
	if (hasNormal)
		n = normalize(n);

	float jitter = Bayer4(ipos);
	float shadow = 0.0;

	// sun
	if (Sun.w > 0.0)
	{
		float facing = hasNormal ? dot(n, Sun.xyz) : 1.0;
		if (facing > 0.0)
			shadow = max(shadow, Trace(P + n * (hasNormal ? 1.0 : 0.0), Sun.xyz, Sun.w, jitter));
		else if (facingShade)
			shadow = 1.0;
	}

	// dynamic lights (within their radius only, fading out towards the edge)
	for (int k = 0; k < 3; k++)
	{
		vec4 L = k == 0 ? Light0 : (k == 1 ? Light1 : Light2);
		if (L.w <= 0.0)
			continue;
		vec3 toL = L.xyz - P;
		float dist = length(toL);
		if (dist >= L.w || dist < 8.0)
			continue;
		vec3 D = toL / dist;
		float atten = 1.0 - dist / L.w;
		float facing = hasNormal ? dot(n, D) : 1.0;
		if (facing > 0.0)
			shadow = max(shadow, atten * Trace(P, D, dist - 4.0, jitter));
		else if (facingShade)
			shadow = max(shadow, atten);
	}

	// hard shadow: pure black, no soft edge. Dither mode: coverage = strength, ordered dither.
	float a = dither ? ((shadow * Params.x) > Bayer4(ipos) ? 1.0 : 0.0) : (shadow > 0.15 ? 1.0 : 0.0);
	FragColor = vec4(0.0, 0.0, 0.0, a);
}
