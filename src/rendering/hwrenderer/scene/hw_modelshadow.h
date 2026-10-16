#pragma once

class HWDrawInfo;
class FRenderState;

// Jupiter3D: renders the models seen from the nearest dynamic lights into depth maps (OpenGL only),
// see modelshadow.fp for how they are used.
void hw_RenderModelShadowMaps(HWDrawInfo *di, FRenderState &state);
