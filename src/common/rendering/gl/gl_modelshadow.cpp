/*
** gl_modelshadow.cpp
**
** Jupiter3D: render targets for the shadow maps of the models (see hw_modelshadow.cpp)
*/

#include <vector>
#include "gl_system.h"
#include "gl_modelshadow.h"

namespace
{
	GLuint depthTex[GL_MODELSHADOW_MAPS] = {};
	GLuint colorRb[GL_MODELSHADOW_MAPS] = {};
	GLuint fbo[GL_MODELSHADOW_MAPS] = {};
	bool created = false;
	bool failed = false;

	GLint savedFB = 0;
	GLint savedViewport[4];
	GLboolean savedScissor = GL_FALSE;
	GLboolean savedColorMask[4];
	GLboolean savedDepthMask = GL_TRUE;

	void Create()
	{
		created = true;
		glGenTextures(GL_MODELSHADOW_MAPS, depthTex);
		glGenRenderbuffers(GL_MODELSHADOW_MAPS, colorRb);
		glGenFramebuffers(GL_MODELSHADOW_MAPS, fbo);

		GLint prevFB = 0, prevTex = 0, prevRB = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRB);

		for (int i = 0; i < GL_MODELSHADOW_MAPS; i++)
		{
			glBindTexture(GL_TEXTURE_2D, depthTex[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

			glBindRenderbuffer(GL_RENDERBUFFER, colorRb[i]);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_R8, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2);

			glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRb[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex[i], 0);
			GLenum buf = GL_COLOR_ATTACHMENT0;
			glDrawBuffers(1, &buf);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				failed = true;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, prevFB);
		glBindTexture(GL_TEXTURE_2D, prevTex);
		glBindRenderbuffer(GL_RENDERBUFFER, prevRB);
	}
}

bool GLModelShadow_Begin(int index)
{
	if (index < 0 || index >= GL_MODELSHADOW_MAPS)
		return false;
	if (!created)
		Create();
	if (failed)
		return false;

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFB);
	glGetIntegerv(GL_VIEWPORT, savedViewport);
	glGetBooleanv(GL_SCISSOR_TEST, &savedScissor);
	glGetBooleanv(GL_COLOR_WRITEMASK, savedColorMask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &savedDepthMask);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo[index]);
	glViewport(0, 0, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);
	return true;
}

void GLModelShadow_Face(int face)
{
	face = face < 0 ? 0 : (face > 5 ? 5 : face);
	glViewport((face % 3) * GL_MODELSHADOW_SIZE, (face / 3) * GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE);
}

void GLModelShadow_End()
{
	glBindFramebuffer(GL_FRAMEBUFFER, savedFB);
	glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
	if (savedScissor) glEnable(GL_SCISSOR_TEST);
	glColorMask(savedColorMask[0], savedColorMask[1], savedColorMask[2], savedColorMask[3]);
	glDepthMask(savedDepthMask);
}

void GLModelShadow_BindTexture(int index, int unit)
{
	if (!created || failed || index < 0 || index >= GL_MODELSHADOW_MAPS)
		return;
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, depthTex[index]);
}


//==========================================================================
//
// World depth maps
//
//==========================================================================

namespace
{
	GLuint worldTex[GL_MODELSHADOW_MAPS] = {};
	GLuint worldFbo[GL_MODELSHADOW_MAPS] = {};
	GLuint worldColorRb = 0;
	bool worldCreated = false;
	bool worldFailed = false;

	GLuint meshVao = 0, meshVbo = 0, meshEbo = 0;
	unsigned meshElems = 0;
	unsigned meshVerts = 0;
	const void *meshKey = nullptr;
	unsigned meshGeneration = 0;

	GLuint worldProgram = 0;
	GLint worldUniform = -1;

	struct WorldCache
	{
		float pos[3] = { 0, 0, 0 };
		float farp = 0;
		unsigned generation = 0;
		bool valid = false;
	} worldCache[GL_MODELSHADOW_MAPS];

	bool CreateWorldProgram()
	{
		const char *vs =
			"#version 330 core\n"
			"layout(location = 0) in vec3 aPos;\n"
			"uniform mat4 uVP;\n"
			"void main() { gl_Position = uVP * vec4(aPos, 1.0); }\n";
		const char *fs =
			"#version 330 core\n"
			"void main() { }\n";
		GLuint v = glCreateShader(GL_VERTEX_SHADER);
		GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(v, 1, &vs, nullptr);
		glShaderSource(f, 1, &fs, nullptr);
		glCompileShader(v);
		glCompileShader(f);
		GLint okv = 0, okf = 0;
		glGetShaderiv(v, GL_COMPILE_STATUS, &okv);
		glGetShaderiv(f, GL_COMPILE_STATUS, &okf);
		worldProgram = glCreateProgram();
		glAttachShader(worldProgram, v);
		glAttachShader(worldProgram, f);
		glLinkProgram(worldProgram);
		GLint okp = 0;
		glGetProgramiv(worldProgram, GL_LINK_STATUS, &okp);
		glDeleteShader(v);
		glDeleteShader(f);
		if (!okv || !okf || !okp)
			return false;
		worldUniform = glGetUniformLocation(worldProgram, "uVP");
		return worldUniform >= 0;
	}

	void CreateWorld()
	{
		worldCreated = true;
		if (!CreateWorldProgram())
		{
			worldFailed = true;
			return;
		}
		GLint prevFB = 0, prevTex = 0, prevRB = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRB);

		glGenTextures(GL_MODELSHADOW_MAPS, worldTex);
		glGenFramebuffers(GL_MODELSHADOW_MAPS, worldFbo);
		glGenRenderbuffers(1, &worldColorRb);
		glBindRenderbuffer(GL_RENDERBUFFER, worldColorRb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_R8, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2);
		for (int i = 0; i < GL_MODELSHADOW_MAPS; i++)
		{
			glBindTexture(GL_TEXTURE_2D, worldTex[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
			glBindFramebuffer(GL_FRAMEBUFFER, worldFbo[i]);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, worldColorRb);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, worldTex[i], 0);
			GLenum buf = GL_COLOR_ATTACHMENT0;
			glDrawBuffers(1, &buf);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				worldFailed = true;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, prevFB);
		glBindTexture(GL_TEXTURE_2D, prevTex);
		glBindRenderbuffer(GL_RENDERBUFFER, prevRB);
	}
}

void GLModelShadow_SetWorld(const float *verts, unsigned nverts, const unsigned int *elems, unsigned nelems, const void *key)
{
	if (key == meshKey && nverts == meshVerts && nelems == meshElems && meshVao != 0)
		return;
	meshKey = key;
	if (nverts == 0 || nelems == 0)
	{
		meshElems = 0;
		meshVerts = 0;
		meshGeneration++;
		return;
	}

	// render space: x = X, y = Z (up), z = Y
	std::vector<float> conv((size_t)nverts * 3);
	for (unsigned i = 0; i < nverts; i++)
	{
		conv[i * 3 + 0] = verts[i * 3 + 0];
		conv[i * 3 + 1] = verts[i * 3 + 2];
		conv[i * 3 + 2] = verts[i * 3 + 1];
	}

	GLint prevVao = 0, prevArray = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
	if (meshVao == 0)
	{
		glGenVertexArrays(1, &meshVao);
		glGenBuffers(1, &meshVbo);
		glGenBuffers(1, &meshEbo);
	}
	glBindVertexArray(meshVao);
	glBindBuffer(GL_ARRAY_BUFFER, meshVbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(conv.size() * sizeof(float)), conv.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshEbo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)nelems * sizeof(unsigned int)), elems, GL_STATIC_DRAW);
	glBindVertexArray(prevVao);
	glBindBuffer(GL_ARRAY_BUFFER, prevArray);

	meshElems = nelems;
	meshVerts = nverts;
	meshGeneration++;
}

bool GLModelShadow_RenderWorld(int index, const float *lightPos, float farp, const float *faceVP)
{
	if (index < 0 || index >= GL_MODELSHADOW_MAPS)
		return false;
	if (!worldCreated)
		CreateWorld();
	if (worldFailed || meshElems == 0)
	{
		worldCache[index].valid = false;
		return false;
	}
	WorldCache &wc = worldCache[index];
	if (wc.valid && wc.generation == meshGeneration && wc.farp == farp
		&& wc.pos[0] == lightPos[0] && wc.pos[1] == lightPos[1] && wc.pos[2] == lightPos[2])
		return true;	// the light and the level did not change, the map is still good

	GLint savedFB = 0, savedProgram = 0, savedVao = 0, savedViewport[4], savedDepthFunc = GL_LESS;
	GLboolean savedScissor = glIsEnabled(GL_SCISSOR_TEST), savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean savedCull = glIsEnabled(GL_CULL_FACE), savedStencil = glIsEnabled(GL_STENCIL_TEST), savedOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	GLboolean savedBlend = glIsEnabled(GL_BLEND);
	GLboolean savedColorMask[4], savedDepthMask = GL_TRUE;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFB);
	glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVao);
	glGetIntegerv(GL_VIEWPORT, savedViewport);
	glGetIntegerv(GL_DEPTH_FUNC, &savedDepthFunc);
	glGetBooleanv(GL_COLOR_WRITEMASK, savedColorMask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &savedDepthMask);

	GLboolean savedClip[3];
	for (int i = 0; i < 3; i++)
	{
		savedClip[i] = glIsEnabled(GL_CLIP_DISTANCE0 + i);
		glDisable(GL_CLIP_DISTANCE0 + i);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, worldFbo[index]);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glViewport(0, 0, GL_MODELSHADOW_SIZE * 3, GL_MODELSHADOW_SIZE * 2);
	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);

	glUseProgram(worldProgram);
	glBindVertexArray(meshVao);
	for (int face = 0; face < 6; face++)
	{
		glViewport((face % 3) * GL_MODELSHADOW_SIZE, (face / 3) * GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE);
		glUniformMatrix4fv(worldUniform, 1, GL_FALSE, faceVP + face * 16);
		glDrawElements(GL_TRIANGLES, (GLsizei)meshElems, GL_UNSIGNED_INT, nullptr);
	}

	glBindVertexArray(savedVao);
	glUseProgram(savedProgram);
	glBindFramebuffer(GL_FRAMEBUFFER, savedFB);
	glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
	glDepthFunc(savedDepthFunc);
	glColorMask(savedColorMask[0], savedColorMask[1], savedColorMask[2], savedColorMask[3]);
	glDepthMask(savedDepthMask);
	if (savedScissor) glEnable(GL_SCISSOR_TEST);
	if (!savedDepthTest) glDisable(GL_DEPTH_TEST);
	if (savedCull) glEnable(GL_CULL_FACE);
	if (savedStencil) glEnable(GL_STENCIL_TEST);
	if (savedOffset) glEnable(GL_POLYGON_OFFSET_FILL);
	if (savedBlend) glEnable(GL_BLEND);
	for (int i = 0; i < 3; i++)
		if (savedClip[i]) glEnable(GL_CLIP_DISTANCE0 + i);

	wc.valid = true;
	wc.generation = meshGeneration;
	wc.farp = farp;
	wc.pos[0] = lightPos[0]; wc.pos[1] = lightPos[1]; wc.pos[2] = lightPos[2];
	return true;
}

void GLModelShadow_BindWorldTexture(int index, int unit)
{
	if (!worldCreated || worldFailed || index < 0 || index >= GL_MODELSHADOW_MAPS)
		return;
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, worldTex[index]);
}
