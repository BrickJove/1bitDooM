#pragma once

// Jupiter3D: depth maps of the models seen from the dynamic lights (OpenGL only)

constexpr int GL_MODELSHADOW_MAPS = 2;
constexpr int GL_MODELSHADOW_SIZE = 1024;

// binds the depth map of a light as render target (and clears it); restores nothing yet
bool GLModelShadow_Begin(int index);
// restores framebuffer binding, viewport and scissor of before GLModelShadow_Begin
void GLModelShadow_End();
// binds the depth texture of a light to a texture unit
void GLModelShadow_BindTexture(int index, int unit);
