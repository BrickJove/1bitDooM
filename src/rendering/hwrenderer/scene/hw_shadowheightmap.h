#pragma once

struct FLevelLocals;
struct SceneShadowFrame;

// Jupiter3D: per level height map (floor and ceiling height per grid cell) for view independent
// shadows, see sceneshadow.fp. Fills the texture part of the frame data.
void hw_UpdateShadowHeightMap(FLevelLocals *level, SceneShadowFrame &frame);
void hw_ResetShadowHeightMap();
