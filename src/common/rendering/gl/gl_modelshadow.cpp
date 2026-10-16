/*
** gl_modelshadow.cpp
**
** Jupiter3D: render targets for the shadow maps of the models (see hw_modelshadow.cpp)
*/

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
			glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

			glBindRenderbuffer(GL_RENDERBUFFER, colorRb[i]);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE);

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
	glViewport(0, 0, GL_MODELSHADOW_SIZE, GL_MODELSHADOW_SIZE);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_TRUE);
	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);
	return true;
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
