/*
** hw_shadowheightmap.cpp
**
** Jupiter3D: height map of the level for view independent shadows.
**
** The map is a regular grid over the bounding box of the level. Every cell stores the floor and
** the ceiling height of the sector at the cell centre (R32F, two textures):
**   open space      floor = floor height, ceiling = ceiling height
**   sky ceiling     ceiling = +big (the sun shines through)
**   solid space     floor = +big, ceiling = -big (outside of all sectors and along one sided walls)
** Moving floors and ceilings (doors, lifts) are tracked and re-rasterized; the textures are
** re-uploaded at a limited rate.
*/

#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <math.h>
#include "g_levellocals.h"
#include "r_sky.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hw_shadowheightmap.h"

namespace
{

const float Big = 100000.f;
const int MaxGrid = 1024;
const unsigned UploadInterval = 6;	// frames between two uploads while sectors move

struct SectorState
{
	double floorD = 0, ceilD = 0;
	double floorNX = 0, floorNY = 0, ceilNX = 0, ceilNY = 0;
	bool sky = false;
};

struct ShadowHeightMap
{
	const void *key = nullptr;
	FString mapName;
	unsigned sectorCount = 0, lineCount = 0, vertexCount = 0;
	int W = 0, H = 0;
	float cell = 8.f, ox = 0, oy = 0;
	std::vector<float> floorH, ceilH;
	std::vector<SectorState> states;
	bool dirty = false;
	unsigned frame = 0, lastUpload = 0;
	std::unique_ptr<PPTexture> texFloor, texCeil;
	std::deque<std::pair<unsigned, std::unique_ptr<PPTexture>>> retired;

	bool Matches(FLevelLocals *level) const
	{
		return key == level->sectors.Data() && sectorCount == level->sectors.Size() && lineCount == level->lines.Size()
			&& vertexCount == level->vertexes.Size() && mapName == level->MapName;
	}

	static SectorState StateOf(sector_t *sec)
	{
		SectorState s;
		s.floorD = sec->floorplane.fD();
		s.ceilD = sec->ceilingplane.fD();
		s.floorNX = sec->floorplane.Normal().X; s.floorNY = sec->floorplane.Normal().Y;
		s.ceilNX = sec->ceilingplane.Normal().X; s.ceilNY = sec->ceilingplane.Normal().Y;
		s.sky = sec->GetTexture(sector_t::ceiling) == skyflatnum;
		return s;
	}

	static bool Same(const SectorState &a, const SectorState &b)
	{
		return a.floorD == b.floorD && a.ceilD == b.ceilD && a.floorNX == b.floorNX && a.floorNY == b.floorNY
			&& a.ceilNX == b.ceilNX && a.ceilNY == b.ceilNY && a.sky == b.sky;
	}

	void RasterSector(sector_t *sec)
	{
		bool sky = sec->GetTexture(sector_t::ceiling) == skyflatnum;
		for (int i = 0; i < sec->subsectorcount; i++)
		{
			subsector_t *ss = sec->subsectors[i];
			if (ss == nullptr || ss->numlines < 3)
				continue;
			std::vector<DVector2> pts;
			pts.reserve(ss->numlines);
			for (uint32_t k = 0; k < ss->numlines; k++)
				pts.push_back(ss->firstline[k].v1->p);

			double area = 0, minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
			for (size_t k = 0; k < pts.size(); k++)
			{
				const DVector2 &a = pts[k], &b = pts[(k + 1) % pts.size()];
				area += a.X * b.Y - b.X * a.Y;
				minx = std::min(minx, a.X); maxx = std::max(maxx, a.X);
				miny = std::min(miny, a.Y); maxy = std::max(maxy, a.Y);
			}
			if (fabs(area) < 1e-6)
				continue;
			double sign = area > 0 ? 1.0 : -1.0;
			int x0 = std::max(0, (int)floor((minx - ox) / cell)), x1 = std::min(W - 1, (int)floor((maxx - ox) / cell));
			int y0 = std::max(0, (int)floor((miny - oy) / cell)), y1 = std::min(H - 1, (int)floor((maxy - oy) / cell));
			for (int cy = y0; cy <= y1; cy++)
			{
				for (int cx = x0; cx <= x1; cx++)
				{
					double px = ox + (cx + 0.5) * cell, py = oy + (cy + 0.5) * cell;
					bool inside = true;
					for (size_t k = 0; k < pts.size() && inside; k++)
					{
						const DVector2 &a = pts[k], &b = pts[(k + 1) % pts.size()];
						double cr = (b.X - a.X) * (py - a.Y) - (b.Y - a.Y) * (px - a.X);
						if (cr * sign < -1e-4)
							inside = false;
					}
					if (!inside)
						continue;
					size_t idx = (size_t)cy * W + cx;
					floorH[idx] = (float)sec->floorplane.ZatPoint(px, py);
					ceilH[idx] = sky ? Big : (float)sec->ceilingplane.ZatPoint(px, py);
				}
			}
		}
	}

	// cells touched by a line (supercover walk) become solid, so that thin walls cannot leak
	void MarkWall(const DVector2 &a, const DVector2 &b)
	{
		double len = (b - a).Length();
		int n = std::max(1, (int)ceil(len / (cell * 0.5)));
		for (int i = 0; i <= n; i++)
		{
			double t = (double)i / n;
			int cx = (int)floor((a.X + (b.X - a.X) * t - ox) / cell);
			int cy = (int)floor((a.Y + (b.Y - a.Y) * t - oy) / cell);
			if (cx < 0 || cy < 0 || cx >= W || cy >= H)
				continue;
			size_t idx = (size_t)cy * W + cx;
			floorH[idx] = Big;
			ceilH[idx] = -Big;
		}
	}

	void Build(FLevelLocals *level)
	{
		key = level->sectors.Data();
		mapName = level->MapName;
		sectorCount = level->sectors.Size();
		lineCount = level->lines.Size();
		vertexCount = level->vertexes.Size();
		texFloor.reset();
		texCeil.reset();
		retired.clear();

		double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
		for (auto &v : level->vertexes)
		{
			minx = std::min(minx, v.p.X); maxx = std::max(maxx, v.p.X);
			miny = std::min(miny, v.p.Y); maxy = std::max(maxy, v.p.Y);
		}
		if (minx > maxx)
		{
			W = H = 0;
			return;
		}
		double dim = std::max(maxx - minx, maxy - miny) + 16.0;
		cell = 4.f;
		while (dim / cell > MaxGrid)
			cell *= 2.f;
		ox = (float)(minx - 8.0);
		oy = (float)(miny - 8.0);
		W = std::max(1, (int)ceil((maxx - minx + 16.0) / cell));
		H = std::max(1, (int)ceil((maxy - miny + 16.0) / cell));

		floorH.assign((size_t)W * H, Big);
		ceilH.assign((size_t)W * H, -Big);

		states.resize(level->sectors.Size());
		for (unsigned i = 0; i < level->sectors.Size(); i++)
		{
			sector_t *sec = &level->sectors[i];
			RasterSector(sec);
			states[i] = StateOf(sec);
		}
		for (auto &line : level->lines)
		{
			if (line.backsector == nullptr && line.v1 && line.v2)
				MarkWall(line.v1->p, line.v2->p);
		}
		dirty = true;
		lastUpload = frame - UploadInterval;
	}

	void Upload()
	{
		auto make = [&](std::vector<float> &src, std::unique_ptr<PPTexture> &tex)
		{
			if (tex)
				retired.emplace_back(frame, std::move(tex));
			std::shared_ptr<float> data(new float[src.size()], std::default_delete<float[]>());
			std::copy(src.begin(), src.end(), data.get());
			tex.reset(new PPTexture(W, H, PixelFormat::R32f, std::shared_ptr<void>(data)));
		};
		make(floorH, texFloor);
		make(ceilH, texCeil);
		dirty = false;
		lastUpload = frame;
	}

	void Update(FLevelLocals *level, SceneShadowFrame &out)
	{
		frame++;
		if (!Matches(level))
			Build(level);
		else
		{
			// moving planes: re-rasterize the sectors that changed
			for (unsigned i = 0; i < level->sectors.Size() && i < states.size(); i++)
			{
				sector_t *sec = &level->sectors[i];
				SectorState now = StateOf(sec);
				if (!Same(now, states[i]))
				{
					states[i] = now;
					RasterSector(sec);
					dirty = true;
				}
			}
		}

		if (W <= 0 || H <= 0)
			return;
		if (!texFloor || (dirty && frame - lastUpload >= UploadInterval))
			Upload();

		// old textures may still be in use by frames in flight
		while (!retired.empty() && frame - retired.front().first > 8)
			retired.pop_front();

		out.FloorTex = texFloor.get();
		out.CeilTex = texCeil.get();
		out.GridOriginX = ox;
		out.GridOriginY = oy;
		out.GridInvCell = 1.0f / cell;
	}
};

ShadowHeightMap heightMap;

}

void hw_UpdateShadowHeightMap(FLevelLocals *level, SceneShadowFrame &frame)
{
	heightMap.Update(level, frame);
}

void hw_ResetShadowHeightMap()
{
	heightMap = ShadowHeightMap();
}
