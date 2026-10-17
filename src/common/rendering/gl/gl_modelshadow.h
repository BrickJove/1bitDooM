#pragma once

// Jupiter3D: depth maps of the models seen from the dynamic lights (OpenGL only)

constexpr int GL_MODELSHADOW_MAPS = 4;
constexpr int GL_MODELSHADOW_SIZE = 768;

// binds the depth map of a light as render target (and clears it); restores nothing yet
bool GLModelShadow_Begin(int index);
// the six faces of the atlas (3 x 2 tiles) of the bound map: sets the viewport to the tile of the face
void GLModelShadow_Face(int face);
// restores framebuffer binding, viewport and scissor of before GLModelShadow_Begin
void GLModelShadow_End();
// binds the depth texture of a light to a texture unit
void GLModelShadow_BindTexture(int index, int unit);
