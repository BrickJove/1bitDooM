#pragma once

#include <vector>
#include <stdint.h>

struct FLevelLocals;

// Jupiter3D: occluder geometry for stencil shadow volumes (the Doom 3 algorithm), see gl_shadowvolumes.cpp.
// Every occluder is a flat convex polygon (a "plate"): a wall piece, or a ceiling of a sector. Render space
// (x = X, y = Z up, z = Y). The vertex buffer holds, for each plate, its n corners followed by n copies that
// the vertex shader extrudes away from the light (flag 1).
struct ShadowVolumeGeometry
{
	struct Plate
	{
		uint32_t first;		// index of the first corner in the vertex buffer
		uint32_t count;		// number of corners
		float normal[3];		// normal of the corner order (counter clockwise around it)
		float point[3];		// a point of the plate
		bool roof;			// ceiling plate (only used to find out if a pixel lies under a roof)
	};
	std::vector<float> vertices;		// 8 floats per vertex: x, y, z, extrude flag, nx, ny, nz, 0
	std::vector<Plate> plates;
	unsigned version = 0;			// changes when the vertex data changes

	// Triangle indices of the volumes of all wall plates (roof = false) or roof plates (roof = true) for a light at lightPos.
	void BuildIndices(const float *lightPos, bool roof, std::vector<uint32_t> &out) const;
};

// Brings the geometry up to date (rebuilt when the level changes or sectors move). Never null.
const ShadowVolumeGeometry *hw_UpdateShadowVolumes(FLevelLocals *level);
void hw_ResetShadowVolumes();
