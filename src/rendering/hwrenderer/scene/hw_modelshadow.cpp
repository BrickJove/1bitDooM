/*
** hw_modelshadow.cpp
**
** Jupiter3D: shadow maps for the models.
**
** RenderModel collects the models that were drawn this frame. After the scene they are drawn again, depth only,
** from the point of view of the nearest dynamic lights of the map (sector lights, ...). The post process
** modelshadow.fp then paints every pixel black that has such a model between itself and the light.
*/

#include <math.h>
#include "hw_drawinfo.h"
#include "hw_renderstate.h"
#include "hw_viewpointbuffer.h"
#include "hw_viewpointuniforms.h"
#include "hw_modelshadow.h"
#include "flatvertices.h"
#include "hw_models.h"
#include "models.h"
#include "actor.h"
#include "actorinlines.h"
#include "printf.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "d_net.h"
#include "d_player.h"
#include "r_utility.h"
#include "v_video.h"
#include "matrix.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hwrenderer/postprocessing/hw_postprocess_cvars.h"

static void ModelShadowReport(const FString &text)
{
	static FString last;
	if (text != last)
	{
		last = text;
		Printf("%s\n", text.GetChars());
	}
}

void hw_RenderModelShadowMaps(HWDrawInfo *di, FRenderState &state)
{
	SceneShadowFrame &f = sceneShadowFrame;
	f.ModelShadowActive = false;
	for (int i = 0; i < 2; i++)
		f.ModelLight[i][3] = 0.f;

	if (!gl_model_shadow_map || !gl_scene_shadow || !f.Valid)
		return;

	// the lights of the map (not only the ones nearest to the camera: the model that casts can be far away from it)
	struct Lt { float x, y, z, radius, reach; };	// radius: which models the light affects, reach: how far their shadows go
	TArray<Lt> lights;
	{
		int group = (di->Viewpoint.camera && di->Viewpoint.camera->Sector) ? di->Viewpoint.camera->Sector->PortalGroup : 0;
		for (auto light = di->Level->lights; light; light = light->next)
		{
			if (!light->IsActive() || light->IsSubtractive() || light->IsSpot())
				continue;
			float radius = light->GetRadius();
			if (radius <= 0.f)
				continue;
			DVector3 lp = light->PosRelative(group);
			float height = (float)lp.Z;	// as the light is placed in the map
			Lt lt = { (float)lp.X, height, (float)lp.Y, radius, (gl_model_shadow_map_unlimited ? 8192.f : radius) };
			lights.Push(lt);
			if (lights.Size() >= 512)
				break;
		}
	}
	if (lights.Size() == 0)
	{
		if (gl_model_shadow_map_debug) { static bool said = false; if (!said) { said = true; Printf("Model shadows: no usable dynamic light in the map\n"); } }
		return;
	}

	// all models that can cast a shadow, near one of the lights (also those that are not on the screen: their shadow can be)
	struct Info
	{
		AActor *actor;
		FSpriteModelFrame *smf;
		float x, y, z;		// position as RenderModel wants it (map coordinates)
		float cx, cy, cz;	// centre in render space (x = X, y = Z up, z = Y)
		float r;		// radius of a sphere around the model
		double ticFrac;
	};
	TArray<Info> infos;
	int nWanted = 0, nFarPlayer = 0, nNoLight = 0, nNoModel = 0;
	{
		auto it = di->Level->GetThinkerIterator<AActor>();
		AActor *a;
		while ((a = it.Next()) != nullptr)
		{
			if ((a->ObjectFlags & OF_EuthanizeMe) || (a->renderflags & RF_INVISIBLE) || a->sprite == 0 || a->Sector == nullptr)
				continue;
			if (!WantsModelShadow(a))
				continue;
			nWanted++;
			DVector3 pos = a->InterpolatedPosition(di->Viewpoint.TicFrac);
			float r = (float)max(a->radius * 1.5, a->Height * 0.75) + 16.f;	// generous: arms and weapons stick out of the radius
			float cx = (float)pos.X, cy = (float)(pos.Z + a->Height * 0.5), cz = (float)pos.Y;
			bool near = false;
			for (unsigned li = 0; li < lights.Size() && !near; li++)
			{
				float dx = cx - lights[li].x, dy = cy - lights[li].y, dz = cz - lights[li].z;
				near = dx * dx + dy * dy + dz * dz < (lights[li].radius + r) * (lights[li].radius + r);
			}
			if (!near)
			{
				nNoLight++;
				continue;
			}
			int spritenum = a->sprite;
			DVector2 sprscale = a->InterpolatedScale(di->Viewpoint.TicFrac);
			if (a->player != nullptr)
				P_CheckPlayerSprite(a, spritenum, sprscale);
			FSpriteModelFrame *smf = FindModelFrame(a, spritenum, a->frame, !!(a->flags & MF_DROPPED));
			if (smf == nullptr)
			{
				nNoModel++;
				continue;
			}
			Info in;
			in.actor = a; in.smf = smf;
			in.x = (float)pos.X; in.y = (float)pos.Y; in.z = (float)pos.Z;
			in.cx = cx; in.cy = cy; in.cz = cz; in.r = r;
			in.ticFrac = Net_ModifyObjectFrac(a, di->Viewpoint.TicFrac);
			infos.Push(in);
			if (infos.Size() >= 128)
				break;
		}
	}
	if (infos.Size() == 0)
	{
		if (gl_model_shadow_map_debug)
		{
			FString t;
			t.Format("Model shadows: %d lights; models that want a shadow: %d (too far from the player: %d, out of reach of every light: %d, without 3D model: %d), none left", (int)lights.Size(), nWanted, nFarPlayer, nNoLight, nNoModel);
			ModelShadowReport(t);
		}
		return;
	}

	int maxMaps = clamp((int)gl_model_shadow_map_lights, 1, 2);
	int usedMaps = 0;
	int pickedCount[2] = { 0, 0 };

	// the lights that reach a model, the closest ones first (distance to the nearest model minus the radius of the light)
	TArray<int> order;
	TArray<float> scores;
	for (unsigned li = 0; li < lights.Size(); li++)
	{
		float best = 1e30f;
		for (unsigned i = 0; i < infos.Size(); i++)
		{
			float dx = infos[i].cx - lights[li].x, dy = infos[i].cy - lights[li].y, dz = infos[i].cz - lights[li].z;
			best = min(best, sqrtf(dx * dx + dy * dy + dz * dz) - lights[li].radius - infos[i].r);
		}
		if (best < 0.f)
		{
			// among the lights that reach a model: the one that is closest to the player first
			float cdx = lights[li].x - (float)di->Viewpoint.Pos.X, cdy = lights[li].y - (float)di->Viewpoint.Pos.Z, cdz = lights[li].z - (float)di->Viewpoint.Pos.Y;
			best = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz) - lights[li].radius;
			unsigned pos = order.Size();
			order.Push((int)li);
			scores.Push(best);
			while (pos > 0 && scores[pos - 1] > best)
			{
				scores[pos] = scores[pos - 1]; order[pos] = order[pos - 1];
				pos--;
				scores[pos] = best; order[pos] = (int)li;
			}
		}
	}

	for (unsigned oi = 0; oi < order.Size() && usedMaps < maxMaps; oi++)
	{
		const Lt &L = lights[order[oi]];
		const float lx = L.x, ly = L.y, lz = L.z, radius = L.radius, reach = L.reach;

		// the models this light reaches
		TArray<int> picked;
		for (unsigned i = 0; i < infos.Size(); i++)
		{
			float dx = infos[i].cx - lx, dy = infos[i].cy - ly, dz = infos[i].cz - lz;
			float d = sqrtf(dx * dx + dy * dy + dz * dz);
			if (d > radius + infos[i].r || d < infos[i].r * 0.5f)
				continue;	// out of reach, or the light is inside the model
			picked.Push(i);
		}
		if (picked.Size() == 0)
			continue;

		// axis: towards the middle of the models (the player's own model does not steer it, so the view does not jump
		// when the player walks around), field of view wide enough for all of them but not wider than maxFov:
		// models outside of it are left out
		bool haveOthers = false;
		for (unsigned k = 0; k < picked.Size(); k++)
			if (infos[picked[k]].actor->player == nullptr)
				haveOthers = true;
		float ax = 0, ay = 0, az = 0;
		for (unsigned k = 0; k < picked.Size(); k++)
		{
			const Info &in = infos[picked[k]];
			if (haveOthers && in.actor->player != nullptr)
				continue;
			float dx = in.cx - lx, dy = in.cy - ly, dz = in.cz - lz;
			float d = sqrtf(dx * dx + dy * dy + dz * dz);
			ax += dx / d; ay += dy / d; az += dz / d;
		}
		float alen = sqrtf(ax * ax + ay * ay + az * az);
		if (alen < 1e-3f)
			continue;
		ax /= alen; ay /= alen; az /= alen;
		const float maxFov = 130.f;
		float maxAngle = 0.f;
		for (unsigned k = 0; k < picked.Size(); k++)
		{
			const Info &in = infos[picked[k]];
			if (haveOthers && in.actor->player != nullptr)
				continue;
			float dx = in.cx - lx, dy = in.cy - ly, dz = in.cz - lz;
			float d = sqrtf(dx * dx + dy * dy + dz * dz);
			float cosA = clamp((dx * ax + dy * ay + dz * az) / d, -1.f, 1.f);
			float ang = acosf(cosA) + asinf(clamp(in.r / d, 0.f, 0.99f));
			maxAngle = max(maxAngle, ang);
		}
		float fov = clamp(2.f * maxAngle * 1.15f * (180.f / (float)M_PI) + 4.f, 10.f, maxFov);
		// the models outside of the field of view are not drawn (the player's model, if it is far off the others)
		{
			TArray<int> inside;
			float halfFov = fov * 0.5f * ((float)M_PI / 180.f);
			for (unsigned k = 0; k < picked.Size(); k++)
			{
				const Info &in = infos[picked[k]];
				float dx = in.cx - lx, dy = in.cy - ly, dz = in.cz - lz;
				float d = sqrtf(dx * dx + dy * dy + dz * dz);
				float cosA = clamp((dx * ax + dy * ay + dz * az) / d, -1.f, 1.f);
				if (acosf(cosA) - asinf(clamp(in.r / d, 0.f, 0.99f)) < halfFov)
					inside.Push(picked[k]);
			}
			picked = inside;
		}
		float nearp = 4.f, farp = reach + 128.f;

		bool upAxis = fabsf(ay) > 0.95f;
		VSMatrix view;
		view.loadIdentity();
		view.lookAt(lx, ly, lz, lx + ax, ly + ay, lz + az, upAxis ? 1.f : 0.f, upAxis ? 0.f : 1.f, 0.f);
		VSMatrix proj;
		proj.loadIdentity();
		proj.perspective(fov, 1.f, nearp, farp);

		if (!screen->BeginModelShadowMap(usedMaps))
			break;

		// the viewpoint of the light (the model shaders read their matrices from the viewpoint uniforms)
		HWViewpointUniforms vpu = di->VPUniforms;
		vpu.ProjectionMatrix = proj;
		vpu.ViewMatrix = view;
		vpu.CalcDependencies();
		vpu.mCameraPos = FVector4(lx, ly, lz, 1.f);
		vpu.mClipLine = FVector4(-10000001.f, 0.f, 0.f, 0.f);
		vpu.mClipHeight = 0.f;
		vpu.mClipHeightDirection = 0.f;
		screen->mViewpoints->SetViewpoint(state, &vpu);

		state.SetDepthMask(true);
		state.SetDepthFunc(DF_Less);
		state.SetColorMask(false);
		state.SetCulling(Cull_None);
		state.SetRenderStyle(STYLE_Normal);
		state.SetTextureMode(TM_NORMAL);
		state.SetObjectColor(0xffffffff);
		state.SetAddColor(0);
		state.EnableFog(false);
		state.SetLightIndex(-1);

		FHWModelRenderer renderer(di, state, -1);
		for (unsigned k = 0; k < picked.Size(); k++)
		{
			const Info &in = infos[picked[k]];
			RenderModelShadowCaster(&renderer, in.x, in.y, in.z, in.smf, in.actor, in.ticFrac);
		}
		state.SetVertexBuffer(screen->mVertexData);
		state.SetColorMask(true);
		state.SetCulling(Cull_None);
		state.SetDepthFunc(DF_Less);

		screen->EndModelShadowMap();

		f.ModelLight[usedMaps][0] = lx; f.ModelLight[usedMaps][1] = ly; f.ModelLight[usedMaps][2] = lz; f.ModelLight[usedMaps][3] = reach;
		f.ModelNearFar[usedMaps][0] = nearp; f.ModelNearFar[usedMaps][1] = farp;
		VSMatrix vp;
		vp.loadMatrix(proj.get());
		vp.multMatrix(view.get());
		memcpy(f.ModelVP[usedMaps], vp.get(), sizeof(float) * 16);
		pickedCount[usedMaps] = (int)picked.Size();
		usedMaps++;
	}

	f.ModelShadowActive = usedMaps > 0;
	if (gl_model_shadow_map_debug)
	{
		FString t;
		t.Format("Model shadows: %d lights, %d casting models (of %d that want one: %d too far from the player, %d out of reach, %d without 3D model), %d lights reach a model, %d maps rendered", (int)lights.Size(), (int)infos.Size(), nWanted, nFarPlayer, nNoLight, nNoModel, (int)order.Size(), usedMaps);
		for (int m = 0; m < usedMaps; m++)
		{
			FString u;
			u.Format("; map %d: light at %.0f %.0f %.0f radius %.0f, %d models", m, f.ModelLight[m][0], f.ModelLight[m][1], f.ModelLight[m][2], f.ModelLight[m][3], pickedCount[m]);
			t += u;
		}
		ModelShadowReport(t);
	}
	// the caller binds its own viewpoint again afterwards
}
