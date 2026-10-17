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

// The geometry of the level seen from the lights (depth only, same atlas layout as the models): a pixel that the light
// does not see (a wall in between) gets no shadow, and a wall ends the shadow.
// verts: x, y, z per vertex in map coordinates, elems: triangles. key identifies the mesh (the arrays are copied to the GPU
// when key or sizes change).
void GLModelShadow_SetWorld(const float *verts, unsigned nverts, const unsigned int *elems, unsigned nelems, const void *key);
// renders the world into the world map of a light when the light or the mesh changed; faceVP = 6 matrices (projection * view, column major)
bool GLModelShadow_RenderWorld(int index, const float *lightPos, float farp, const float *faceVP);
void GLModelShadow_BindWorldTexture(int index, int unit);
