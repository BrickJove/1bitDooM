/*
** hw_shadowheightmap.cpp
**
** Jupiter3D: height map of the level for view independent shadows.
**
** The map is a regular grid over the bounding box of the level. One R32F texture (4 floats per cell):
**   floor, ceiling   height of the sector at the cell centre
**                    sky ceiling: ceiling = +big (the sun shines through)
**                    solid space (outside of all sectors): floor = +big, ceiling = -big
**   list start, count  the lines that touch the cell (index into the list texture)
** The lines themselves (exact segments, with the height range in which they block a ray) are in a
** line table. A ray is tested against these exact segments, so shadow edges are straight lines that
** start exactly at the corners of the geometry; the cell heights only matter in cells without lines.
** Moving floors and ceilings (doors, lifts) update the cells and the lines of the sector; the
** textures are re-uploaded at a limited rate.
*/

#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <math.h>
#include "g_levellocals.h"
#include "r_sky.h"
#include "texturemanager.h"
#include "bitmap.h"
#include "doomdata.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hw_shadowheightmap.h"

namespace
{

const float Big = 100000.f;
const float SolidOff = 200000.f;	// solid cells: floor channel = height of the solid + SolidOff
const float NearSkyMark = 90000.f;	// solid cells close to an open sky: ceiling channel
const int MaxGrid = 1024;
const unsigned UploadInterval = 6;	// frames between two uploads while sectors move
const int LineRowWidth = 512;		// lines per row of the line table (8 floats each)
const int ListWidth = 4096;

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
	float cell = 8.f, ox = 0, oy = 0, midX = 0, midY = 0, topLimit = 0;
	std::vector<float> cellData;		// 4 floats per cell
	std::vector<float> topNum;		// numeric ceiling height of the open cells (-Big = none), for the height of the solids
	std::unique_ptr<PPTexture> texLight;
	std::vector<float> lineData;		// 8 floats per included line
	std::vector<int> lineIndex;		// level line -> index in the line table, -1 = not included
	std::vector<int> lineFence;		// level line -> fence index + 1
	std::vector<SectorState> states;
	bool cellsDirty = false, linesDirty = false;
	unsigned frame = 0, lastUpload = 0;
	std::unique_ptr<PPTexture> texCell, texList, texLine, texFenceParam, texFenceAtlas;
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

	// Ceilings that are very far above the floor (a courtyard with a high textured ceiling) are no roof for the sun.
	static constexpr double OpenHeight = 512.0;

	static float CeilingOf(sector_t *sec, const DVector2 &p)
	{
		if (sec->GetTexture(sector_t::ceiling) == skyflatnum)
			return Big;
		double c = sec->ceilingplane.ZatPoint(p);
		return (c - sec->floorplane.ZatPoint(p) >= OpenHeight) ? Big : (float)c;
	}

	// Highest finite floor / ceiling height of the level: above it nothing can block a ray.
	static float TopOf(FLevelLocals *level)
	{
		double top = 0;
		for (auto &sec : level->sectors)
			for (unsigned k = 0; k < sec.Lines.Size(); k++)
			{
				const DVector2 &p = sec.Lines[k]->v1->p;
				top = std::max(top, std::max(sec.floorplane.ZatPoint(p), sec.ceilingplane.ZatPoint(p)));
			}
		return (float)top;
	}

	void RasterSector(sector_t *sec)
	{
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
					size_t idx = ((size_t)cy * W + cx) * 4;
					cellData[idx] = (float)sec->floorplane.ZatPoint(px, py);
					cellData[idx + 1] = CeilingOf(sec, DVector2(px, py));
					double fz = sec->floorplane.ZatPoint(px, py), cz = sec->ceilingplane.ZatPoint(px, py);
					if (!topNum.empty())
						topNum[idx / 4] = (cz - fz >= 32.0) ? (float)cz : -Big;
				}
			}
		}
	}

	// Height range in which a line blocks a ray (the ray is blocked below lo and above hi).
	// One sided: everything. Two sided: below the higher floor and above the lower ceiling.
	void LineRange(const line_t &line, float &lo, float &hi) const
	{
		if (line.backsector == nullptr || line.frontsector == nullptr)
		{
			// a wall is as high as the ceiling of its sector; under an open sky the light passes above it
			sector_t *sec = line.frontsector != nullptr ? line.frontsector : line.backsector;
			DVector2 m = (line.v1->p + line.v2->p) * 0.5;
			if (sec != nullptr && CeilingOf(sec, m) > Big * 0.5f)
			{
				lo = (float)sec->ceilingplane.ZatPoint(m); hi = Big;
			}
			else
			{
				lo = Big; hi = -Big;
			}
			return;
		}
		DVector2 mid = (line.v1->p + line.v2->p) * 0.5;
		lo = (float)std::max(line.frontsector->floorplane.ZatPoint(mid), line.backsector->floorplane.ZatPoint(mid));
		hi = std::min(CeilingOf(line.frontsector, mid), CeilingOf(line.backsector, mid));
	}

	// Calls fn(cell index) for every cell the segment a-b passes through (exact grid traversal).
	template<class F> void WalkCells(const DVector2 &a, const DVector2 &b, F fn)
	{
		double ax = (a.X - ox) / cell, ay = (a.Y - oy) / cell, bx = (b.X - ox) / cell, by = (b.Y - oy) / cell;
		double dx = bx - ax, dy = by - ay;
		int cx = (int)floor(ax), cy = (int)floor(ay);
		int ex = (int)floor(bx), ey = (int)floor(by);
		int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0), sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
		double tdx = dx != 0 ? fabs(1.0 / dx) : 1e30, tdy = dy != 0 ? fabs(1.0 / dy) : 1e30;
		double tmx = dx != 0 ? (dx > 0 ? (cx + 1 - ax) : (ax - cx)) * tdx : 1e30;
		double tmy = dy != 0 ? (dy > 0 ? (cy + 1 - ay) : (ay - cy)) * tdy : 1e30;
		for (int i = 0; i < 100000; i++)
		{
			if (cx >= 0 && cy >= 0 && cx < W && cy < H)
				fn((size_t)cy * W + cx);
			if (cx == ex && cy == ey)
				break;
			if (fabs(tmx - tmy) < 1e-9)
			{
				// passes exactly through a grid corner: both neighbours as well
				if (cx + sx >= 0 && cy >= 0 && cx + sx < W && cy < H) fn((size_t)cy * W + cx + sx);
				if (cx >= 0 && cy + sy >= 0 && cx < W && cy + sy < H) fn((size_t)(cy + sy) * W + cx);
				cx += sx; cy += sy; tmx += tdx; tmy += tdy;
			}
			else if (tmx < tmy) { cx += sx; tmx += tdx; }
			else { cy += sy; tmy += tdy; }
			if (std::min(tmx, tmy) > 1.0 + 1e-9 && !(cx == ex && cy == ey))
			{
				// numerical drift: make sure the end cell is visited
				if (ex >= 0 && ey >= 0 && ex < W && ey < H) fn((size_t)ey * W + ex);
				break;
			}
		}
	}

	void Build(FLevelLocals *level)
	{
		key = level->sectors.Data();
		mapName = level->MapName;
		sectorCount = level->sectors.Size();
		lineCount = level->lines.Size();
		vertexCount = level->vertexes.Size();
		texCell.reset(); texList.reset(); texLight.reset(); texLine.reset(); texFenceParam.reset(); texFenceAtlas.reset();
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
		midX = (float)((minx + maxx) * 0.5);
		midY = (float)((miny + maxy) * 0.5);
		topLimit = TopOf(level);
		double dim = std::max(maxx - minx, maxy - miny) + 16.0;
		cell = 2.f;
		while (dim / cell > MaxGrid)
			cell *= 2.f;
		ox = (float)(minx - 8.0);
		oy = (float)(miny - 8.0);
		W = std::max(1, (int)ceil((maxx - minx + 16.0) / cell));
		H = std::max(1, (int)ceil((maxy - miny + 16.0) / cell));

		cellData.assign((size_t)W * H * 4, 0.f);
		for (size_t i = 0; i < (size_t)W * H; i++)
		{
			cellData[i * 4] = Big;
			cellData[i * 4 + 1] = -Big;
		}
		topNum.assign((size_t)W * H, -Big);

		states.resize(level->sectors.Size());
		for (unsigned i = 0; i < level->sectors.Size(); i++)
		{
			sector_t *sec = &level->sectors[i];
			RasterSector(sec);
			states[i] = StateOf(sec);
		}

		FillSolids();
		MarkNearSky();
		BuildFences(level);
		BuildLines(level);
		cellsDirty = linesDirty = true;
		lastUpload = frame - UploadInterval;
	}

	// Solid space (outside of all sectors) is as high as the nearest open cell (nearest ceiling, flood fill), so a ray
	// that passes over a wall is not stopped by the void behind it. Solid cells: floor channel = height + SolidOff.
	void FillSolids()
	{
		std::vector<float> height((size_t)W * H, -Big);
		std::vector<int> queue;
		queue.reserve((size_t)W * H);
		for (size_t c = 0; c < (size_t)W * H; c++)
			if (cellData[c * 4] < Big * 0.5f && topNum[c] > -Big * 0.5f)
			{
				height[c] = topNum[c];
				queue.push_back((int)c);
			}
		for (size_t q = 0; q < queue.size(); q++)
		{
			int c = queue[q];
			int cx = c % W, cy = c / W;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int x = cx + dx, y = cy + dy;
					if (x < 0 || y < 0 || x >= W || y >= H) continue;
					size_t n = (size_t)y * W + x;
					if (height[n] > -Big * 0.5f || cellData[n * 4] < Big * 0.5f) continue;
					height[n] = height[c];
					queue.push_back((int)n);
				}
		}
		for (size_t c = 0; c < (size_t)W * H; c++)
			if (cellData[c * 4] >= Big * 0.5f)
			{
				cellData[c * 4] = (height[c] > -Big * 0.5f ? height[c] : Big * 0.5f) + SolidOff;
				cellData[c * 4 + 1] = Big;
			}
	}

	// Solid cells within 3 cells of an open sky cell get the marker 12345 in the ceiling channel: a pixel on the ground
	// right at a wall base can fall into such a cell and must still count as lying under the open sky.
	void MarkNearSky()
	{
		const int R = 3;
		std::vector<int> marks;
		for (int cy = 0; cy < H; cy++)
		{
			for (int cx = 0; cx < W; cx++)
			{
				size_t idx = ((size_t)cy * W + cx) * 4;
				if (cellData[idx] < SolidOff * 0.75f)
					continue;
				bool near = false;
				for (int dy = -R; dy <= R && !near; dy++)
				{
					int y = cy + dy;
					if (y < 0 || y >= H) continue;
					for (int dx = -R; dx <= R; dx++)
					{
						int x = cx + dx;
						if (x < 0 || x >= W) continue;
						size_t n = ((size_t)y * W + x) * 4;
						if (cellData[n] < SolidOff * 0.75f && cellData[n + 1] > Big * 0.5f) { near = true; break; }
					}
				}
				if (near)
					marks.push_back((int)idx);
			}
		}
		for (int idx : marks)
			cellData[idx + 1] = NearSkyMark;
	}

	// The lines that can block a ray: one sided lines, lines with a height step, fences, and lines of sectors
	// that have a tag or a special (they may move later).
	void BuildLines(FLevelLocals *level)
	{
		lineIndex.assign(level->lines.Size(), -1);
		std::vector<const line_t *> included;
		for (unsigned i = 0; i < level->lines.Size(); i++)
		{
			const line_t &line = level->lines[i];
			if (line.v1 == nullptr || line.v2 == nullptr)
				continue;
			bool use = line.backsector == nullptr || line.frontsector == nullptr || lineFence[i] > 0;
			if (!use)
			{
				float lo, hi;
				LineRange(line, lo, hi);
				DVector2 mid = (line.v1->p + line.v2->p) * 0.5;
				float flo = (float)std::min(line.frontsector->floorplane.ZatPoint(mid), line.backsector->floorplane.ZatPoint(mid));
				float chi = std::max(CeilingOf(line.frontsector, mid), CeilingOf(line.backsector, mid));
				use = lo > flo || hi < chi
					|| level->SectorHasTags(line.frontsector) || level->SectorHasTags(line.backsector)
					|| line.frontsector->special != 0 || line.backsector->special != 0;
			}
			if (use)
			{
				lineIndex[i] = (int)included.size();
				included.push_back(&line);
			}
		}

		lineData.assign(std::max<size_t>(included.size(), 1) * 8, 0.f);
		for (size_t n = 0; n < included.size(); n++)
			WriteLine(*included[n], n, lineFence[included[n] - &level->lines[0]]);

		// per cell: start / count of the lines, then the list itself
		std::vector<unsigned> counts((size_t)W * H, 0);
		for (auto *line : included)
			WalkCells(line->v1->p, line->v2->p, [&](size_t c) { counts[c]++; });
		size_t total = 0;
		for (size_t c = 0; c < counts.size(); c++)
		{
			cellData[c * 4 + 2] = (float)total;
			cellData[c * 4 + 3] = (float)counts[c];
			total += counts[c];
		}
		std::vector<float> list(((total + ListWidth - 1) / ListWidth + 1) * ListWidth, 0.f);
		std::vector<unsigned> fill((size_t)W * H, 0);
		for (size_t n = 0; n < included.size(); n++)
		{
			WalkCells(included[n]->v1->p, included[n]->v2->p, [&](size_t c)
			{
				list[(size_t)cellData[c * 4 + 2] + fill[c]++] = (float)n;
			});
		}
		texList = MakeTexture(ListWidth, (int)(list.size() / ListWidth), list);
	}

	void WriteLine(const line_t &line, size_t n, int fenceId)
	{
		float lo, hi;
		LineRange(line, lo, hi);
		float *row = &lineData[n * 8];
		row[0] = (float)line.v1->p.X; row[1] = (float)line.v1->p.Y;
		row[2] = (float)line.v2->p.X; row[3] = (float)line.v2->p.Y;
		row[4] = lo; row[5] = hi;
		row[6] = (float)fenceId;
		row[7] = 0.f;
	}

	static std::unique_ptr<PPTexture> MakeTexture(int w, int h, const std::vector<float> &src)
	{
		std::shared_ptr<float> data(new float[src.size()], std::default_delete<float[]>());
		std::copy(src.begin(), src.end(), data.get());
		return std::unique_ptr<PPTexture>(new PPTexture(w, h, PixelFormat::R32f, std::shared_ptr<void>(data)));
	}

	// Lines with a (transparent) middle texture, e.g. chain link fences: their texture alpha casts a shadow.
	// Per fence 16 floats, and an atlas with the alpha masks (64 x 64 per texture, 16 x 16 slots).
	void BuildFences(FLevelLocals *level)
	{
		const int Slot = 128, SlotsPerRow = 16, MaxSlots = SlotsPerRow * SlotsPerRow, MaxFences = 8192;
		lineFence.assign(level->lines.Size(), 0);
		std::vector<float> params;
		std::vector<float> atlas((size_t)Slot * SlotsPerRow * Slot * SlotsPerRow, 0.f);
		std::vector<int> slotTex;	// texture id per slot

		for (unsigned li = 0; li < level->lines.Size(); li++)
		{
			line_t &line = level->lines[li];
			if (line.backsector == nullptr || line.frontsector == nullptr || (line.flags & ML_3DMIDTEX) || params.size() / 16 >= MaxFences)
				continue;
			// the side that has a middle texture (the front side first); the back side runs the other way along the line
			bool flip = false;
			side_t *side = line.sidedef[0];
			if (side == nullptr || !side->GetTexture(side_t::mid).isValid())
			{
				side = line.sidedef[1];
				flip = true;
			}
			if (side == nullptr)
				continue;
			FTextureID texid = side->GetTexture(side_t::mid);
			if (!texid.isValid())
				continue;
			FGameTexture *tex = TexMan.GetGameTexture(texid, true);
			if (tex == nullptr || !tex->isValid() || tex->GetDisplayWidth() < 1 || tex->GetDisplayHeight() < 1)
				continue;

			int slot = -1;
			for (size_t i = 0; i < slotTex.size(); i++)
				if (slotTex[i] == texid.GetIndex()) { slot = (int)i; break; }
			if (slot < 0)
			{
				if ((int)slotTex.size() >= MaxSlots)
					continue;
				FBitmap bmp = tex->GetTexture()->GetBgraBitmap(nullptr);
				int bw = bmp.GetWidth(), bh = bmp.GetHeight();
				const uint8_t *px = (const uint8_t *)bmp.GetPixels();
				if (px == nullptr || bw < 1 || bh < 1)
					continue;
				slot = (int)slotTex.size();
				slotTex.push_back(texid.GetIndex());
				int ax = (slot % SlotsPerRow) * Slot, ay = (slot / SlotsPerRow) * Slot;
				for (int sy = 0; sy < Slot; sy++)
				{
					int y0 = sy * bh / Slot, y1 = std::max(y0 + 1, (sy + 1) * bh / Slot);
					for (int sx = 0; sx < Slot; sx++)
					{
						int x0 = sx * bw / Slot, x1 = std::max(x0 + 1, (sx + 1) * bw / Slot);
						double sum = 0; int cnt = 0;
						for (int yy = y0; yy < y1 && yy < bh; yy++)
							for (int xx = x0; xx < x1 && xx < bw; xx++)
							{
								sum += px[(size_t)(yy * bw + xx) * 4 + 3];	// BGRA: alpha is byte 3
								cnt++;
							}
						atlas[(size_t)(ay + sy) * (Slot * SlotsPerRow) + ax + sx] = cnt ? (float)(sum / cnt / 255.0) : 0.f;
					}
				}
			}

			DVector2 a = line.v1->p, b = line.v2->p;
			DVector2 mid = (a + b) * 0.5;
			double ffloor = line.frontsector->floorplane.ZatPoint(mid), bfloor = line.backsector->floorplane.ZatPoint(mid);
			double fceil = line.frontsector->ceilingplane.ZatPoint(mid), bceil = line.backsector->ceilingplane.ZatPoint(mid);
			// the same texture coordinate rules as the wall renderer (hw_walls.cpp, DoMidTexture)
			FTexCoordInfo tci;
			tci.GetFromTexture(tex, (float)side->GetTextureXScale(side_t::mid), (float)side->GetTextureYScale(side_t::mid),
				!!(level->flags3 & LEVEL3_FORCEWORLDPANNING));
			if (tci.mRenderHeight < 0)
			{
				tci.mRenderHeight = -tci.mRenderHeight;
				tci.mScale.Y = -tci.mScale.Y;
			}
			if (tci.mRenderWidth < 1 || tci.mRenderHeight < 1)
				continue;
			double rowoffset = tci.RowOffset((float)side->GetTextureYOffset(side_t::mid));
			double texH = tci.mRenderHeight;
			double top;
			if (line.flags & ML_DONTPEGBOTTOM)
				top = std::max(ffloor, bfloor) + rowoffset + texH;
			else
				top = std::min(fceil, bceil) + rowoffset;
			bool wrap = (line.flags & ML_WRAP_MIDTEX) || (side->Flags & WALLF_WRAP_MIDTEX);

			float row[16] = {};
			row[0] = (float)a.X; row[1] = (float)a.Y; row[2] = (float)b.X; row[3] = (float)b.Y;
			row[4] = (float)top; row[5] = (float)texH;
			row[6] = tci.TextureOffset((float)side->GetTextureXOffset(side_t::mid));
			row[7] = 1.f / (float)tci.mRenderWidth;	// repeats per map unit along the line
			row[8] = (float)((slot % SlotsPerRow) * Slot); row[9] = (float)((slot / SlotsPerRow) * Slot);
			row[10] = wrap ? 1.f : 0.f;
			row[11] = (float)std::max(ffloor, bfloor); row[12] = (float)std::min(fceil, bceil);
			row[13] = flip ? 1.f : 0.f;
			lineFence[li] = (int)(params.size() / 16) + 1;
			params.insert(params.end(), row, row + 16);
		}

		if (params.empty())
			params.assign(16, 0.f);
		texFenceParam = MakeTexture(16, (int)(params.size() / 16), params);
		texFenceAtlas = MakeTexture(Slot * SlotsPerRow, Slot * SlotsPerRow, atlas);
	}

	void Upload()
	{
		auto retire = [&](std::unique_ptr<PPTexture> &tex)
		{
			if (tex)
				retired.emplace_back(frame, std::move(tex));
		};
		if (cellsDirty || !texCell)
		{
			retire(texCell);
			texCell = MakeTexture(W * 4, H, cellData);
			cellsDirty = false;
		}
		if (linesDirty || !texLine)
		{
			retire(texLine);
			size_t rows = std::max<size_t>(1, (lineData.size() / 8 + LineRowWidth - 1) / LineRowWidth);
			std::vector<float> padded(rows * LineRowWidth * 8, 0.f);
			std::copy(lineData.begin(), lineData.end(), padded.begin());
			texLine = MakeTexture(LineRowWidth * 8, (int)rows, padded);
			linesDirty = false;
		}
		lastUpload = frame;
	}

	void Update(FLevelLocals *level, SceneShadowFrame &out)
	{
		frame++;
		if (!Matches(level))
			Build(level);
		else
		{
			// moving planes: re-rasterize the sectors that changed and refresh their lines
			bool topChanged = false;
			for (unsigned i = 0; i < level->sectors.Size() && i < states.size(); i++)
			{
				sector_t *sec = &level->sectors[i];
				SectorState now = StateOf(sec);
				if (Same(now, states[i]))
					continue;
				states[i] = now;
				topChanged = true;
				RasterSector(sec);
				cellsDirty = true;
				for (unsigned k = 0; k < sec->Lines.Size(); k++)
				{
					line_t *l = sec->Lines[k];
					int n = lineIndex[l - &level->lines[0]];
					if (n >= 0)
					{
						WriteLine(*l, n, lineFence[l - &level->lines[0]]);
						linesDirty = true;
					}
				}
			}
			if (topChanged)
				topLimit = TopOf(level);
		}

		if (W <= 0 || H <= 0)
			return;
		if (!texCell || !texLine || ((cellsDirty || linesDirty) && frame - lastUpload >= UploadInterval))
			Upload();

		// old textures may still be in use by frames in flight
		while (!retired.empty() && frame - retired.front().first > 8)
			retired.pop_front();

		// the nearest dynamic lights: one row each (x, y, z, radius), new every frame (the old one may still be in use)
		{
			std::vector<float> ld(16, 0.f);
			for (int i = 0; i < 3; i++)
				for (int k = 0; k < 4; k++)
					ld[i * 4 + k] = out.Light[i][k];
			if (texLight)
				retired.emplace_back(frame, std::move(texLight));
			texLight = MakeTexture(4, 4, ld);
			out.LightTex = texLight.get();
		}
		out.CellTex = texCell.get();
		out.ListTex = texList.get();
		out.LineTex = texLine.get();
		out.FenceParamTex = texFenceParam.get();
		out.FenceAtlasTex = texFenceAtlas.get();
		out.GridOriginX = ox;
		out.GridOriginY = oy;
		out.GridInvCell = 1.0f / cell;
		out.MidX = midX;
		out.MidY = midY;
		out.TopLimit = topLimit;
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
