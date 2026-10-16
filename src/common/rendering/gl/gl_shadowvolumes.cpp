/*
** gl_shadowvolumes.cpp
**
** Jupiter3D: stencil shadow volumes, the algorithm of Doom 3 (depth fail / "Carmack's reverse").
**
** For every source (the sun, up to 3 dynamic lights):
**   1. sun only: the ceiling plates are extruded away from the sun and drawn into the stencil buffer; a pixel inside
**      such a volume lies under a roof and is not reached by the sun (stencil bit 5).
**   2. the wall plates are extruded away from the source and drawn into the stencil buffer (bits 0-4, a pixel with a
**      count other than 0 is inside a shadow volume).
**   3. full screen passes write bit 7 ("reached by a source") and bit 6 ("lit by a source": reached, faces the source,
**      outside of all shadow volumes).
** At the end every pixel with bit 7 set and bit 6 clear (reached by at least one source, lit by none) is painted black.
** The shadow edges are exact: the stencil test uses the real depth buffer, not a reconstruction.
*/

#include "gl_system.h"
#include "gl_debug.h"
#include "gl_renderbuffers.h"
#include "gl_renderer.h"
#include "gl_postprocessstate.h"
#include "gl_shaderprogram.h"
#include "gl_shadowvolumes.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hwrenderer/postprocessing/hw_postprocess_cvars.h"
#include "v_video.h"
#include "printf.h"
#include "../../../rendering/hwrenderer/scene/hw_shadowvolumes.h"

namespace OpenGLRenderer
{

namespace
{

struct VolumePass
{
	FShaderProgram volume, lit;
	bool ready = false;
	GLuint vao = 0, vbo = 0, ebo = 0;
	size_t vboCapacity = 0, eboCapacity = 0;
	const ShadowVolumeGeometry *geoKey = nullptr;
	unsigned geoVersion = ~0u;
	GLuint depthTex = 0, depthFB = 0;
	int depthW = 0, depthH = 0;
	std::vector<uint32_t> indices;

	// uniform locations
	GLint uViewProj, uLight, uCam, uShift;
	GLint lCamX, lCamY, lCamZ, lProj, lScale, lOffset, lSource, lMode, lFacing, lDebug;

	void Init()
	{
		volume.Compile(FShaderProgram::Vertex, "shaders/pp/shadowvolume.vp", "", 330);
		volume.Compile(FShaderProgram::Fragment, "shaders/pp/shadowvolume.fp", "", 330);
		volume.Link("shadowvolume");
		lit.Compile(FShaderProgram::Vertex, "shaders/pp/screenquad.vp", "", 330);
		lit.Compile(FShaderProgram::Fragment, "shaders/pp/shadowlit.fp", "", 330);
		lit.Link("shadowlit");

		GLuint p = volume.Handle();
		uViewProj = glGetUniformLocation(p, "uViewProj");
		uLight = glGetUniformLocation(p, "uLight");
		uCam = glGetUniformLocation(p, "uCam");
		uShift = glGetUniformLocation(p, "uShift");
		p = lit.Handle();
		lCamX = glGetUniformLocation(p, "CamX");
		lCamY = glGetUniformLocation(p, "CamY");
		lCamZ = glGetUniformLocation(p, "CamZ");
		lProj = glGetUniformLocation(p, "ProjParams");
		lScale = glGetUniformLocation(p, "Scale");
		lOffset = glGetUniformLocation(p, "Offset");
		lSource = glGetUniformLocation(p, "Source");
		lMode = glGetUniformLocation(p, "Mode");
		lFacing = glGetUniformLocation(p, "FacingShade");
		lDebug = glGetUniformLocation(p, "DebugColor");

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ebo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (void *)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)16);
		ready = true;
	}

	void EnsureDepthCopy(int w, int h)
	{
		if (depthTex != 0 && depthW == w && depthH == h)
			return;
		if (depthTex == 0)
		{
			glGenTextures(1, &depthTex);
			glGenFramebuffers(1, &depthFB);
		}
		glBindTexture(GL_TEXTURE_2D, depthTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, depthFB);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
		depthW = w;
		depthH = h;
	}

	void UploadGeometry(const ShadowVolumeGeometry *geo)
	{
		if (geo == geoKey && geo->version == geoVersion)
			return;
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		size_t bytes = geo->vertices.size() * sizeof(float);
		if (bytes > vboCapacity)
		{
			glBufferData(GL_ARRAY_BUFFER, bytes, geo->vertices.data(), GL_DYNAMIC_DRAW);
			vboCapacity = bytes;
		}
		else if (bytes > 0)
			glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, geo->vertices.data());
		geoKey = geo;
		geoVersion = geo->version;
	}

	void DrawIndices()
	{
		if (indices.empty())
			return;
		glBindVertexArray(vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		size_t bytes = indices.size() * sizeof(uint32_t);
		if (bytes > eboCapacity)
		{
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, indices.data(), GL_STREAM_DRAW);
			eboCapacity = bytes;
		}
		else
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, bytes, indices.data());
		glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
	}
};

VolumePass *pass = nullptr;

void ClearStencil(GLuint mask)
{
	glStencilMask(mask);
	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);
}

}

static int dbgState = -1;

bool RenderSceneShadowVolumes(FGLRenderBuffers *buffers)
{
	const SceneShadowFrame &f = sceneShadowFrame;
	if (!gl_scene_shadow || !gl_scene_shadow_volumes || !f.Valid || f.Volumes == nullptr || buffers->SceneIsMultisampled())
	{
		if (gl_scene_shadow_debug != 0)
		{
			int reason = !gl_scene_shadow ? 1 : !gl_scene_shadow_volumes ? 2 : !f.Valid ? 3 : f.Volumes == nullptr ? 4 : 5;
			if (reason != dbgState)
			{
				dbgState = reason;
				static const char *names[] = { "", "gl_scene_shadow is off", "gl_scene_shadow_volumes is off", "no valid shadow frame (non-perspective view?)", "no volume geometry", "scene is multisampled (set gl_multisample 1)" };
				Printf("Shadow volumes NOT used (ray march fallback): %s\n", names[reason]);
			}
		}
		return false;
	}
	if (gl_scene_shadow_debug != 0)
	{
		int key = (int)f.Volumes->plates.size() * 16 + (gl_scene_shadow_sun ? 1 : 0) + 2 * clamp((int)gl_scene_shadow_lights, 0, 3);
		if (key + 100 != dbgState)
		{
			dbgState = key + 100;
			Printf("Shadow volumes active: %d plates, sun %d, lights %d, sun height %d\n", (int)f.Volumes->plates.size(), (int)gl_scene_shadow_sun, (int)gl_scene_shadow_lights, (int)gl_scene_shadow_sun_height);
		}
	}

	if (gl_scene_shadow_debug != 0 && gl_scene_shadow_sun && f.SunPosValid)
	{
		// CPU check: which plates lie between the camera and the sun (the camera is in the shadow of them)
		const float C[3] = { f.InvView[0][3], f.InvView[1][3], f.InvView[2][3] };
		const float *Lp = f.SunPos;
		int blockers = 0, roofs = 0;
		for (const auto &pl : f.Volumes->plates)
		{
			double dc = pl.normal[0] * (C[0] - pl.point[0]) + pl.normal[1] * (C[1] - pl.point[1]) + pl.normal[2] * (C[2] - pl.point[2]);
			double dl = pl.normal[0] * (Lp[0] - pl.point[0]) + pl.normal[1] * (Lp[1] - pl.point[1]) + pl.normal[2] * (Lp[2] - pl.point[2]);
			if ((dc > 0) == (dl > 0) || dc == dl)
				continue;
			double t = dc / (dc - dl);
			double H[3] = { C[0] + (Lp[0] - C[0]) * t, C[1] + (Lp[1] - C[1]) * t, C[2] + (Lp[2] - C[2]) * t };
			int ax = 0;
			double an = fabs(pl.normal[0]);
			if (fabs(pl.normal[1]) > an) { ax = 1; an = fabs(pl.normal[1]); }
			if (fabs(pl.normal[2]) > an) ax = 2;
			int u = (ax + 1) % 3, v = (ax + 2) % 3;
			bool inside = false;
			for (unsigned i = 0, j = pl.count - 1; i < pl.count; j = i++)
			{
				const float *a = &f.Volumes->vertices[(pl.first + i) * 8];
				const float *b = &f.Volumes->vertices[(pl.first + j) * 8];
				if ((a[v] > H[v]) != (b[v] > H[v]) && H[u] < (b[u] - a[u]) * (H[v] - a[v]) / (b[v] - a[v]) + a[u])
					inside = !inside;
			}
			if (inside) { blockers++; if (pl.roof) roofs++; }
		}
		static int lastBlockers = -2;
		if (blockers != lastBlockers)
		{
			lastBlockers = blockers;
			Printf("Shadow volumes CPU check: camera (%.0f %.0f %.0f), sun (%.0f %.0f %.0f): %d plates block the sun (%d roofs)\n", C[0], C[1], C[2], Lp[0], Lp[1], Lp[2], blockers, roofs);
		}
	}

	struct Source { float pos[3]; float radius; bool sun; };
	Source sources[4];
	int numSources = 0;
	if (gl_scene_shadow_sun && f.SunPosValid)
	{
		sources[numSources++] = { { f.SunPos[0], f.SunPos[1], f.SunPos[2] }, 0.f, true };
	}
	int lights = clamp((int)gl_scene_shadow_lights, 0, 3);
	for (int i = 0; i < lights; i++)
		if (f.Light[i][3] > 0.f)
			sources[numSources++] = { { f.Light[i][0], f.Light[i][1], f.Light[i][2] }, f.Light[i][3], false };
	if (numSources == 0)
		return true;	// nothing to do (but handled)

	FGLDebug::PushGroup("ShadowVolumes");

	if (pass == nullptr)
	{
		pass = new VolumePass();
		pass->Init();
	}
	VolumePass &P = *pass;

	// --- state that is changed here and must be restored
	GLint prevVao = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	GLboolean prevColorMask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);
	GLboolean prevDepthMask, prevStencilTest, prevCull;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
	glGetBooleanv(GL_STENCIL_TEST, &prevStencilTest);
	glGetBooleanv(GL_CULL_FACE, &prevCull);
	GLint prevDepthFunc, prevStFunc, prevStRef, prevStVMask, prevStWMask, prevStFail, prevStZFail, prevStZPass;
	glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
	glGetIntegerv(GL_STENCIL_FUNC, &prevStFunc);
	glGetIntegerv(GL_STENCIL_REF, &prevStRef);
	glGetIntegerv(GL_STENCIL_VALUE_MASK, &prevStVMask);
	glGetIntegerv(GL_STENCIL_WRITEMASK, &prevStWMask);
	glGetIntegerv(GL_STENCIL_FAIL, &prevStFail);
	glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &prevStZFail);
	glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &prevStZPass);
	GLint prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);
	// The volume vertex shader does not write gl_ClipDistance: clip planes left enabled by the scene (portals, clip
	// heights) would clip the volumes at random.
	GLboolean prevClip[8];
	for (int i = 0; i < 8; i++)
	{
		prevClip[i] = glIsEnabled(GL_CLIP_DISTANCE0 + i);
		glDisable(GL_CLIP_DISTANCE0 + i);
	}
	GLboolean prevOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_POLYGON_OFFSET_FILL);

	FGLPostProcessState savedState;		// saves and disables depth test, blend, scissor
	savedState.SaveTextureBindings(2);

	int sw = buffers->GetSceneWidth(), sh = buffers->GetSceneHeight();

	// the shaders read a copy of the depth buffer (the stencil of the real one is written while they run)
	P.EnsureDepthCopy(sw, sh);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, buffers->GetSceneFBHandle());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthFB);
	glBlitFramebuffer(0, 0, sw, sh, 0, 0, sw, sh, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	buffers->BindSceneFB(false);
	glViewport(screen->mSceneViewport.left, screen->mSceneViewport.top, screen->mSceneViewport.width, screen->mSceneViewport.height);

	P.UploadGeometry(f.Volumes);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, P.depthTex);
	buffers->BindSceneNormalTexture(1);
	glActiveTexture(GL_TEXTURE0);

	const float camPos[3] = { f.InvView[0][3], f.InvView[1][3], f.InvView[2][3] };
	FVector2 scale = screen->SceneScale(), offset = screen->SceneOffset();

	glEnable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_CLAMP);	// the extruded volumes end at infinity
	glDisable(GL_CULL_FACE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	ClearStencil(0xFF);

	auto setLitUniforms = [&](const Source &s, int mode)
	{
		glUseProgram(P.lit.Handle());
		glUniform4f(P.lCamX, f.InvView[0][0], f.InvView[0][1], f.InvView[0][2], f.InvView[0][3]);
		glUniform4f(P.lCamY, f.InvView[1][0], f.InvView[1][1], f.InvView[1][2], f.InvView[1][3]);
		glUniform4f(P.lCamZ, f.InvView[2][0], f.InvView[2][1], f.InvView[2][2], f.InvView[2][3]);
		glUniform4f(P.lProj, 1.0f / screen->GetZFar() - 1.0f / screen->GetZNear(), max(1.0f / screen->GetZNear(), 1.e-8f), f.FocalX, f.FocalY);
		glUniform2f(P.lScale, scale.X, scale.Y);
		glUniform2f(P.lOffset, offset.X, offset.Y);
		glUniform4f(P.lSource, s.pos[0], s.pos[1], s.pos[2], s.radius);
		glUniform1i(P.lMode, mode);
		glUniform1i(P.lFacing, gl_scene_shadow_facing ? 1 : 0);
		glUniform4f(P.lDebug, 0.f, 0.f, 0.f, 1.f);
	};
	auto drawQuad = [&]()
	{
		glBindVertexArray(prevVao);
		GLRenderer->RenderScreenQuad();
	};
	auto drawVolumes = [&](const Source &s, bool roof)
	{
		P.geoKey->BuildIndices(s.pos, roof, P.indices);
		if (P.indices.empty())
			return;
		glUseProgram(P.volume.Handle());
		glUniformMatrix4fv(P.uViewProj, 1, GL_FALSE, f.ViewProj);
		glUniform3f(P.uLight, s.pos[0], s.pos[1], s.pos[2]);
		glUniform3f(P.uCam, camPos[0], camPos[1], camPos[2]);
		glUniform1f(P.uShift, roof ? -1.0f : 1.0f);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glStencilFunc(GL_ALWAYS, 0, 0xFF);
		glStencilMask(0x1F);
		glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
		glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
		P.DrawIndices();
		glDisable(GL_DEPTH_TEST);
	};

	for (int si = 0; si < numSources; si++)
	{
		const Source &s = sources[si];
		ClearStencil(0x3F);

		if (s.sun)
		{
			// roof: a pixel inside the volume of a ceiling is not reached by the sun (bit 5)
			drawVolumes(s, true);
			setLitUniforms(s, 3);
			glStencilFunc(GL_NOTEQUAL, 0, 0x1F);
			glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
			glStencilMask(0x20);
			drawQuad();
			ClearStencil(0x1F);
		}

		drawVolumes(s, false);

		if (gl_scene_shadow_debug == 2 && si == numSources - 1)
		{
			setLitUniforms(s, 3);
			glUniform4f(P.lDebug, 1.f, 1.f, 1.f, 1.f);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glStencilFunc(GL_NOTEQUAL, 0, 0x1F);
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glStencilMask(0x00);
			drawQuad();
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		}

		// bit 7: reached (not under a roof)
		setLitUniforms(s, 0);
		glStencilFunc(GL_EQUAL, 0x80, 0x20);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0x80);
		drawQuad();

		// bit 6: lit (reached, facing the source, outside of every volume)
		setLitUniforms(s, 1);
		glStencilFunc(GL_EQUAL, 0x40, 0x3F);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0x40);
		drawQuad();

		if (!s.sun)
		{
			// the light source itself and its fixture are always lit
			setLitUniforms(s, 2);
			glStencilFunc(GL_ALWAYS, 0x40, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0x40);
			drawQuad();
		}
	}

	// reached by a source, lit by none: black
	setLitUniforms(sources[0], 3);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilFunc(GL_EQUAL, 0x80, 0xC0);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilMask(0x00);
	if (gl_scene_shadow_debug == 1)
		glUniform4f(P.lDebug, 1.f, 1.f, 1.f, 1.f);
	if (gl_scene_shadow_debug == 3)
	{
		glUniform4f(P.lDebug, 1.f, 1.f, 1.f, 1.f);
		glStencilFunc(GL_EQUAL, 0x20, 0x20);
	}
	if (gl_scene_shadow_debug != 2)
		drawQuad();

	ClearStencil(0xFF);

	// --- restore
	glBindVertexArray(prevVao);
	glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
	glDepthMask(prevDepthMask);
	glDepthFunc(prevDepthFunc);
	glStencilFunc(prevStFunc, prevStRef, prevStVMask);
	glStencilOp(prevStFail, prevStZFail, prevStZPass);
	glStencilMask(prevStWMask);
	if (!prevStencilTest) glDisable(GL_STENCIL_TEST);
	if (prevCull) glEnable(GL_CULL_FACE);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	for (int i = 0; i < 8; i++)
		if (prevClip[i]) glEnable(GL_CLIP_DISTANCE0 + i);
	if (prevOffset) glEnable(GL_POLYGON_OFFSET_FILL);

	FGLDebug::PopGroup();
	return true;
}

}
