/*
** hw_spritelight.cpp
**
** Light level / fog management / dynamic lights
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "c_dispatch.h"
#include "a_dynlight.h"
#include "p_local.h"
#include "p_effect.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "actorinlines.h"
#include "hw_dynlightdata.h"
#include "hw_shadowmap.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "models.h"
#include "hw_cvars.h"
#include <cmath>	// needed for std::floor on mac

template<class T>
T smoothstep(const T edge0, const T edge1, const T x)
{
	auto t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

LightProbe* FindLightProbe(FLevelLocals* level, float x, float y, float z)
{
	LightProbe* foundprobe = nullptr;
	if (level->LightProbes.Size() > 0)
	{
#if 1
		double rcpCellSize = 1.0 / level->LPCellSize;
		int gridCenterX = (int)std::floor(x * rcpCellSize) - level->LPMinX;
		int gridCenterY = (int)std::floor(y * rcpCellSize) - level->LPMinY;
		int gridWidth = level->LPWidth;
		int gridHeight = level->LPHeight;
		float lastdist = 0.0f;
		for (int gridY = gridCenterY - 1; gridY <= gridCenterY + 1; gridY++)
		{
			for (int gridX = gridCenterX - 1; gridX <= gridCenterX + 1; gridX++)
			{
				if (gridX >= 0 && gridY >= 0 && gridX < gridWidth && gridY < gridHeight)
				{
					const LightProbeCell& cell = level->LPCells[gridX + (size_t)gridY * gridWidth];
					for (int i = 0; i < cell.NumProbes; i++)
					{
						LightProbe* probe = cell.FirstProbe + i;
						float dx = probe->X - x;
						float dy = probe->Y - y;
						float dz = probe->Z - z;
						float dist = dx * dx + dy * dy + dz * dz;
						if (!foundprobe || dist < lastdist)
						{
							foundprobe = probe;
							lastdist = dist;
						}
					}
				}
			}
		}
#else
		float lastdist = 0.0f;
		for (unsigned int i = 0; i < level->LightProbes.Size(); i++)
		{
			LightProbe *probe = &level->LightProbes[i];
			float dx = probe->X - x;
			float dy = probe->Y - y;
			float dz = probe->Z - z;
			float dist = dx * dx + dy * dy + dz * dz;
			if (i == 0 || dist < lastdist)
			{
				foundprobe = probe;
				lastdist = dist;
			}
		}
#endif
	}
	return foundprobe;
}

//==========================================================================
//
// Sets a single light value from all dynamic lights affecting the specified location
//
//==========================================================================

void HWDrawInfo::GetDynSpriteLight(AActor *self, float x, float y, float z, FSection *sec, int portalgroup, float *out)
{
	float frac, lr, lg, lb;
	float radius;

	out[0] = out[1] = out[2] = 0.f;

	LightProbe* probe = FindLightProbe(Level, x, y, z);
	if (probe)
	{
		out[0] = probe->Red;
		out[1] = probe->Green;
		out[2] = probe->Blue;
	}

	for(FDynamicLight * light : sec->dlist)
	{
		if (light->ShouldLightActor(self))
		{
			float dist;
			FVector3 L;

			// This is a performance critical section of code where we cannot afford to let the compiler decide whether to inline the function or not.
			// This will do the calculations explicitly rather than calling one of AActor's utility functions.
			if (Level->Displacements.size > 0)
			{
				int fromgroup = light->Sector->PortalGroup;
				int togroup = portalgroup;
				if (fromgroup == togroup || fromgroup == 0 || togroup == 0) goto direct;

				DVector2 offset = Level->Displacements.getOffset(fromgroup, togroup);
				L = FVector3(x - (float)(light->X() + offset.X), y - (float)(light->Y() + offset.Y), z - (float)light->Z());
			}
			else
			{
			direct:
				L = FVector3(x - (float)light->X(), y - (float)light->Y(), z - (float)light->Z());
			}

			dist = (float)L.LengthSquared();
			radius = light->GetRadius();

			if (dist < radius * radius)
			{
				dist = sqrtf(dist);	// only calculate the square root if we really need it.

				frac = 1.0f - (dist / radius);

				if (light->IsSpot())
				{
					L *= -1.0f / dist;
					DAngle negPitch = -light->Pitch;
					DAngle Angle = light->Yaw;
					double xyLen = negPitch.Cos();
					double spotDirX = -Angle.Cos() * xyLen;
					double spotDirY = -Angle.Sin() * xyLen;
					double spotDirZ = -negPitch.Sin();
					double cosDir = L.X * spotDirX + L.Y * spotDirY + L.Z * spotDirZ;
					frac *= (float)smoothstep(light->pSpotOuterAngle->Cos(), light->pSpotInnerAngle->Cos(), cosDir);
				}

				if (frac > 0 && (!light->IsShadowMapped() || (light->GetRadius() > 0 && screen->mShadowMap.ShadowTest(light->Pos, { x, y, z }))))
				{
					lr = light->GetRed() / 255.0f;
					lg = light->GetGreen() / 255.0f;
					lb = light->GetBlue() / 255.0f;

					if (light->target && (light->target->renderflags2 & RF2_LIGHTMULTALPHA))
					{
						float alpha = (float)light->target->Alpha;
						lr *= alpha;
						lg *= alpha;
						lb *= alpha;
					}

					// Get GLDEFS intensity
					lr *= light->GetLightDefIntensity();
					lg *= light->GetLightDefIntensity();
					lb *= light->GetLightDefIntensity();

					if (light->IsSubtractive())
					{
						float bright = (float)FVector3(lr, lg, lb).Length();
						FVector3 lightColor(lr, lg, lb);
						lr = (bright - lr) * -1;
						lg = (bright - lg) * -1;
						lb = (bright - lb) * -1;
					}

					out[0] += lr * frac;
					out[1] += lg * frac;
					out[2] += lb * frac;
				}
			}
		}
	}
}

void HWDrawInfo::GetDynSpriteLight(AActor *thing, particle_t *particle, float *out)
{
	if (thing && !(thing->renderflags2 & RF2_NODYNAMICLIGHTING))
	{
		GetDynSpriteLight(thing, (float)thing->X(), (float)thing->Y(), (float)thing->Center(), thing->section, thing->Sector->PortalGroup, out);
	}
	else if (particle && !(particle->flags & SPF_NODYNAMICLIGHTING))
	{
		GetDynSpriteLight(NULL, (float)particle->Pos.X, (float)particle->Pos.Y, (float)particle->Pos.Z, particle->subsector->section, particle->subsector->sector->PortalGroup, out);
	}
}

// static so that we build up a reserve (memory allocations stop)
// For multithread processing each worker thread needs its own copy, though.
static thread_local TArray<FDynamicLight*> addedLightsArray;

EXTERN_CVAR(Bool, gl_light_sector_limit)

// Jupiter3D model shadows: position of the dynamic light that shines on the actor most (near the light, relative to its
// radius). Lights that are attached to the actor itself and lights below the floor are ignored.
bool ModelShadowLight(AActor *self, DVector3 &pos)
{
	if (self == nullptr || self->Sector == nullptr || self->section == nullptr)
		return false;

	int group = self->Sector->PortalGroup;
	DVector3 center = self->Pos();
	center.Z = self->Center();

	double best = 0.0;
	bool found = false;
	for (FDynamicLight *light : self->section->dlist)
	{
		if (light->target == self || !light->ShouldLightActor(self))
			continue;
		double radius = light->GetRadius();
		if (radius <= 0.0)
			continue;
		DVector3 lp = light->PosRelative(group);
		if (lp.Z <= self->floorz + 4.0)
			continue;
		double strength = 1.0 - (lp - center).Length() / radius;
		if (strength <= best)
			continue;
		// no line of sight (the light is behind a wall: the engine links a light to every section its radius reaches around
		// corners and through other rooms): it casts no shadow of this actor
		if (gl_light_sector_limit && light->target != nullptr && light->target != self
			&& !P_CheckSight(light->target, self, SF_IGNOREVISIBILITY | SF_IGNOREWATERBOUNDARY | SF_SEEPASTSHOOTABLELINES))
			continue;
		if (strength > best)
		{
			best = strength;
			pos = lp;
			found = true;
		}
	}
	return found;
}

void hw_GetDynModelLight(AActor *self, FDynLightData &modellightdata)
{
	modellightdata.Clear();

	if (self && self->Sector)
	{
		float actorradius = (float)self->RenderRadius();

		if(actorradius <= gl_model_light_optimization_radius)
		{
			int group = self->Sector->PortalGroup;
			FSection *sec = self->section;

			for(FDynamicLight * light : sec->dlist)
			{
				if (light->ShouldLightActor(self))
				{
					AddLightToList(modellightdata, group, light, true);
				}
			}
		}
		else
		{
			auto &addedLights = addedLightsArray;	// avoid going through the thread local storage for each use.

			addedLights.Clear();

			float x = (float)self->X();
			float y = (float)self->Y();
			float z = (float)self->Center();
			float radiusSquared = actorradius * actorradius;
			dl_validcount++;

			//TODO replace with blockmap search
			BSPWalkCircle(self->Level, x, y, radiusSquared, [&](subsector_t *subsector) // Iterate through all subsectors potentially touched by actor
			{
				auto section = subsector->section;
				if (section->validcount == dl_validcount) return;	// already done from a previous subsector.

				for(FDynamicLight * light : subsector->section->dlist)
				{
					if (light->ShouldLightActor(self))
					{
						int group = subsector->sector->PortalGroup;
						DVector3 pos = light->PosRelative(group);
						float radius = (float)(light->GetRadius() + actorradius);
						double dx = pos.X - x;
						double dy = pos.Y - y;
						double dz = pos.Z - z;
						double distSquared = dx * dx + dy * dy + dz * dz;
						if (distSquared < radius * radius) // Light and actor touches
						{
							if (std::find(addedLights.begin(), addedLights.end(), light) == addedLights.end()) // Check if we already added this light from a different subsector
							{
								AddLightToList(modellightdata, group, light, true);
								addedLights.Push(light);
							}
						}
					}
				}
			});
		}
	}
}

// Console: why does the nearest model not get light from the lamps? (1bitDooM)
EXTERN_CVAR(Bool, gl_light_sprites)
#include "d_player.h"
#include "d_main.h"
CCMD(gl_model_light_info)
{
	if (gamestate != GS_LEVEL || players[consoleplayer].mo == nullptr)
	{
		Printf("not in a level\n");
		return;
	}
	AActor *me = players[consoleplayer].mo;
	FLevelLocals *level = me->Level;
	AActor *best = nullptr;
	double bestD = 1e30;
	auto it = level->GetThinkerIterator<AActor>();
	while (AActor *a = it.Next())
	{
		if (a == me || a->player != nullptr || a->section == nullptr)
			continue;
		if (a->flags & MF_MISSILE)
			continue;
		double d = (a->Pos() - me->Pos()).Length();
		if (d < bestD)
		{
			bestD = d;
			best = a;
		}
	}
	if (best == nullptr)
	{
		Printf("no actor found\n");
		return;
	}
	Printf("nearest actor: %s, %.0f units away\n", best->GetClass()->TypeName.GetChars(), bestD);
	Printf("gl_light_sprites=%d level has dynamic lights=%d bright(flags5)=%d fullbright(renderflag)=%d nodynamiclighting=%d lightlevel=%d\n",
		(int)gl_light_sprites, (int)level->HasDynamicLights, (int)!!(best->flags5 & MF5_BRIGHT), (int)!!(best->renderflags & RF_FULLBRIGHT),
		(int)!!(best->renderflags2 & RF2_NODYNAMICLIGHTING), best->GetLightLevel(best->Sector));
	int n = 0;
	for (FDynamicLight *light : best->section->dlist)
	{
		DVector3 lp = light->Pos;
		double d = (lp - best->Pos()).Length();
		Printf("  light %d: radius %.0f, distance %.0f, lights this actor: %s, shadowmapped: %s\n", n, light->GetRadius(), d,
			light->ShouldLightActor(best) ? "yes" : "NO", light->IsShadowMapped() ? "yes" : "no");
		Printf("     owner: %s, active: %d, dontlightself: %d, dontlightothers: %d, dontlightactors: %d\n",
			light->target ? light->target->GetClass()->TypeName.GetChars() : "-", (int)!!light->IsActive(),
			(int)!!((*light->pLightFlags) & LF_DONTLIGHTSELF), (int)!!((*light->pLightFlags) & LF_DONTLIGHTOTHERS), (int)!!((*light->pLightFlags) & LF_DONTLIGHTACTORS));
		n++;
	}
	Printf("lights linked to the section of the actor: %d\n", n);
	FDynLightData data;
	hw_GetDynModelLight(best, data);
	Printf("lights that the model shader gets: %d (floats %d / %d / %d)\n", (int)((data.arrays[0].Size() + data.arrays[1].Size() + data.arrays[2].Size()) / 8),
		(int)data.arrays[0].Size(), (int)data.arrays[1].Size(), (int)data.arrays[2].Size());
}
