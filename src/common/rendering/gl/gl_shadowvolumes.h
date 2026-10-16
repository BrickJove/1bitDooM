#pragma once

namespace OpenGLRenderer
{

class FGLRenderBuffers;

// Jupiter3D: shadows with stencil shadow volumes (the Doom 3 algorithm). Returns false when it cannot be used
// (multisampling, no data), then the caller uses the height map ray march instead.
bool RenderSceneShadowVolumes(FGLRenderBuffers *buffers);

}
