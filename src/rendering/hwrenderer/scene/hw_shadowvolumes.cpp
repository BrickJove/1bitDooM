/*
** hw_shadowvolumes.cpp
**
** Jupiter3D: occluder plates for stencil shadow volumes.
**
** Walls: the lines of the level. One sided lines: from the floor to the ceiling of the sector (also under a sky, the
** light passes above them). Two sided lines: the piece between the two floors and the piece between the two ceilings
** (not when both are sky). Ceilings (sectors with a solid ceiling) are separate plates: they decide if the sun reaches
** a pixel at all.
*/

#include <math.h>
#include <string.h>
#include <algorithm>
#include "g_levellocals.h"
#include "r_sky.h"
#include "hw_shadowvolumes.h"

namespace
{

struct SecState
{
	double fd, cd, fnx, fny, cnx, cny;
	bool sky;
	bool operator==(const SecState &o) const
	{
		return fd == o.fd && cd == o.cd && fnx == o.fnx && fny == o.fny && cnx == o.cnx && cny == o.cny && sky == o.sky;
	}
};

struct VolumeBuilder
{
	ShadowVolumeGeometry geo;
	const void *key = nullptr;
	unsigned sectorCount = 0, lineCount = 0;
	FString mapName;
	std::vector<SecState> states;
	unsigned frame = 0, lastBuild = 0;
	bool built = false;

	static SecState StateOf(sector_t *s)
	{
		SecState st;
		st.fd = s->floorplane.fD(); st.cd = s->ceilingplane.fD();
		st.fnx = s->floorplane.Normal().X; st.fny = s->floorplane.Normal().Y;
		st.cnx = s->ceilingplane.Normal().X; st.cny = s->ceilingplane.Normal().Y;
		st.sky = s->GetTexture(sector_t::ceiling) == skyflatnum;
		return st;
	}

	// polygon with the corners given in render space, in any order
	void AddPlate(const float (*pts)[3], int n, bool roof)
	{
		if (n < 3)
			return;
		// Newell normal
		double nx = 0, ny = 0, nz = 0;
		for (int i = 0; i < n; i++)
		{
			const float *a = pts[i], *b = pts[(i + 1) % n];
			nx += ((double)a[1] - b[1]) * ((double)a[2] + b[2]);
			ny += ((double)a[2] - b[2]) * ((double)a[0] + b[0]);
			nz += ((double)a[0] - b[0]) * ((double)a[1] + b[1]);
		}
		double len = sqrt(nx * nx + ny * ny + nz * nz);
		if (len < 1e-3)
			return;
		ShadowVolumeGeometry::Plate p;
		p.first = (uint32_t)(geo.vertices.size() / 8);
		p.count = (uint32_t)n;
		p.normal[0] = (float)(nx / len); p.normal[1] = (float)(ny / len); p.normal[2] = (float)(nz / len);
		p.point[0] = pts[0][0]; p.point[1] = pts[0][1]; p.point[2] = pts[0][2];
		p.roof = roof;
		for (int ext = 0; ext < 2; ext++)
			for (int i = 0; i < n; i++)
			{
				float v[8] = { pts[i][0], pts[i][1], pts[i][2], (float)ext, p.normal[0], p.normal[1], p.normal[2], 0.f };
				geo.vertices.insert(geo.vertices.end(), v, v + 8);
			}
		geo.plates.push_back(p);
	}

	// vertical quad over the line (x1,y1)-(x2,y2) between the heights b1/t1 (at the first corner) and b2/t2
	void AddWall(const DVector2 &a, const DVector2 &b, double b1, double t1, double b2, double t2)
	{
		if (t1 - b1 < 0.01 && t2 - b2 < 0.01)
			return;
		float pts[4][3] = {
			{ (float)a.X, (float)b1, (float)a.Y },
			{ (float)b.X, (float)b2, (float)b.Y },
			{ (float)b.X, (float)t2, (float)b.Y },
			{ (float)a.X, (float)t1, (float)a.Y } };
		AddPlate(pts, 4, false);
	}

	static bool IsSky(sector_t *s)
	{
		return s->GetTexture(sector_t::ceiling) == skyflatnum;
	}

	void Build(FLevelLocals *level)
	{
		geo.vertices.clear();
		geo.plates.clear();
		geo.version++;

		for (unsigned i = 0; i < level->lines.Size(); i++)
		{
			line_t &l = level->lines[i];
			if (l.v1 == nullptr || l.v2 == nullptr || l.frontsector == nullptr)
				continue;
			DVector2 a = l.v1->p, b = l.v2->p;
			sector_t *f = l.frontsector, *k = l.backsector;
			double ff1 = f->floorplane.ZatPoint(a), ff2 = f->floorplane.ZatPoint(b);
			double fc1 = f->ceilingplane.ZatPoint(a), fc2 = f->ceilingplane.ZatPoint(b);
			if (k == nullptr)
			{
				AddWall(a, b, ff1, fc1, ff2, fc2);
				continue;
			}
			if (f == k)
				continue;
			double bf1 = k->floorplane.ZatPoint(a), bf2 = k->floorplane.ZatPoint(b);
			double bc1 = k->ceilingplane.ZatPoint(a), bc2 = k->ceilingplane.ZatPoint(b);
			// lower piece: between the two floors
			AddWall(a, b, std::min(ff1, bf1), std::max(ff1, bf1), std::min(ff2, bf2), std::max(ff2, bf2));
			// upper piece: between the two ceilings, not when both are sky
			if (!(IsSky(f) && IsSky(k)))
				AddWall(a, b, std::min(fc1, bc1), std::max(fc1, bc1), std::min(fc2, bc2), std::max(fc2, bc2));
		}

		// ceilings
		for (unsigned si = 0; si < level->sectors.Size(); si++)
		{
			sector_t *sec = &level->sectors[si];
			if (IsSky(sec))
				continue;
			for (int i = 0; i < sec->subsectorcount; i++)
			{
				subsector_t *ss = sec->subsectors[i];
				if (ss == nullptr || ss->numlines < 3 || ss->numlines > 64)
					continue;
				float pts[64][3];
				bool high = true;
				for (uint32_t k = 0; k < ss->numlines; k++)
				{
					const DVector2 &p = ss->firstline[k].v1->p;
					double cz = sec->ceilingplane.ZatPoint(p);
					high = high && (cz - sec->floorplane.ZatPoint(p) >= 512.0);	// very high ceilings count as open
					pts[k][0] = (float)p.X; pts[k][1] = (float)cz; pts[k][2] = (float)p.Y;
				}
				if (!high)
					AddPlate(pts, (int)ss->numlines, true);
			}
		}

		states.resize(level->sectors.Size());
		for (unsigned i = 0; i < level->sectors.Size(); i++)
			states[i] = StateOf(&level->sectors[i]);
		built = true;
		lastBuild = frame;
	}

	const ShadowVolumeGeometry *Update(FLevelLocals *level)
	{
		frame++;
		bool same = built && key == level->sectors.Data() && sectorCount == level->sectors.Size() && lineCount == level->lines.Size() && mapName == level->MapName;
		if (!same)
		{
			key = level->sectors.Data();
			sectorCount = level->sectors.Size();
			lineCount = level->lines.Size();
			mapName = level->MapName;
			Build(level);
			return &geo;
		}
		// moving floors and ceilings: rebuild (at most every second frame)
		if (frame - lastBuild >= 2)
		{
			for (unsigned i = 0; i < level->sectors.Size() && i < states.size(); i++)
				if (!(StateOf(&level->sectors[i]) == states[i]))
				{
					Build(level);
					break;
				}
		}
		return &geo;
	}
};

VolumeBuilder builder;

}

void ShadowVolumeGeometry::BuildIndices(const float *L, bool roofSet, std::vector<uint32_t> &out) const
{
	out.clear();
	for (const Plate &p : plates)
	{
		if (p.roof != roofSet)
			continue;
		double side = (double)p.normal[0] * (L[0] - p.point[0]) + (double)p.normal[1] * (L[1] - p.point[1]) + (double)p.normal[2] * (L[2] - p.point[2]);
		if (fabs(side) < 1e-4)
			continue;	// the light lies in the plane of the plate: no volume
		const uint32_t n = p.count;
		auto corner = [&](uint32_t k, bool ext) -> uint32_t
		{
			uint32_t idx = side > 0 ? k : (n - 1 - k);
			return p.first + idx + (ext ? n : 0);
		};
		// front cap (the plate, facing the light), back cap (extruded, reversed)
		for (uint32_t i = 1; i + 1 < n; i++)
		{
			out.push_back(corner(0, false)); out.push_back(corner(i, false)); out.push_back(corner(i + 1, false));
			out.push_back(corner(0, true)); out.push_back(corner(i + 1, true)); out.push_back(corner(i, true));
		}
		// sides
		for (uint32_t k = 0; k < n; k++)
		{
			uint32_t kk = (k + 1) % n;
			out.push_back(corner(k, false)); out.push_back(corner(k, true)); out.push_back(corner(kk, true));
			out.push_back(corner(k, false)); out.push_back(corner(kk, true)); out.push_back(corner(kk, false));
		}
	}
}

const ShadowVolumeGeometry *hw_UpdateShadowVolumes(FLevelLocals *level)
{
	return builder.Update(level);
}

void hw_ResetShadowVolumes()
{
	builder = VolumeBuilder();
}
