#pragma once

struct FLevelLocals;
struct SceneShadowFrame;

// Jupiter3D: per level height map (floor and ceiling height per grid cell) for view independent
// shadows, see sceneshadow.fp. Fills the texture part of the frame data.
void hw_UpdateShadowHeightMap(FLevelLocals *level, SceneShadowFrame &frame);
void hw_ResetShadowHeightMap();
struct sector_t;
// console command scene_shadow_info: what the height map knows about the lines of a sector
void hw_DumpShadowHeightMap(FLevelLocals *level, sector_t *sec);
