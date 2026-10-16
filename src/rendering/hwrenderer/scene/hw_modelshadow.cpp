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
#include "v_video.h"
#include "matrix.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hwrenderer/postprocessing/hw_postprocess_cvars.h"

void hw_RenderModelShadowMaps(HWDrawInfo *di, FRenderState &state)
{
	SceneShadowFrame &f = sceneShadowFrame;
	f.ModelShadowActive = false;
	for (int i = 0; i < 2; i++)
		f.ModelLight[i][3] = 0.f;

	if (!gl_model_shadow_map || !gl_scene_shadow || !f.Valid || modelShadowCasters.Size() == 0)
	{
		modelShadowCasters.Clear();
		return;
	}

	struct Info
	{
		AActor *actor;
		float cx, cy, cz;	// centre in render space (x = X, y = Z up, z = Y)
		float r;		// radius of a sphere around the model
	};
	TArray<Info> infos;
	for (auto &c : modelShadowCasters)
	{
		AActor *a = c.actor;
		if (a == nullptr || (a->ObjectFlags & OF_EuthanizeMe))
			continue;
		Info in;
		in.actor = a;
		in.cx = (float)a->X();
		in.cy = (float)(a->Z() + a->Height * 0.5);
		in.cz = (float)a->Y();
		in.r = (float)max(a->radius, a->Height * 0.5) + 8.f;
		infos.Push(in);
	}

	int maxMaps = clamp((int)gl_model_shadow_map_lights, 1, 2);
	int usedMaps = 0;
	for (int li = 0; li < 3 && usedMaps < maxMaps; li++)
	{
		if (f.Light[li][3] <= 0.f)
			continue;
		const float lx = f.Light[li][0], ly = f.Light[li][1], lz = f.Light[li][2], radius = f.Light[li][3];

		// the models this light reaches
		TArray<int> picked;
		float ax = 0, ay = 0, az = 0;
		for (unsigned i = 0; i < infos.Size(); i++)
		{
			float dx = infos[i].cx - lx, dy = infos[i].cy - ly, dz = infos[i].cz - lz;
			float d = sqrtf(dx * dx + dy * dy + dz * dz);
			if (d > radius + infos[i].r || d < infos[i].r * 0.5f)
				continue;	// out of reach, or the light is inside the model
			picked.Push(i);
			ax += dx; ay += dy; az += dz;
		}
		if (picked.Size() == 0)
			continue;

		// axis: towards the middle of the models, field of view wide enough for all of them
		float alen = sqrtf(ax * ax + ay * ay + az * az);
		if (alen < 1e-3f)
			continue;
		ax /= alen; ay /= alen; az /= alen;
		float maxAngle = 0.f;
		for (unsigned k = 0; k < picked.Size(); k++)
		{
			const Info &in = infos[picked[k]];
			float dx = in.cx - lx, dy = in.cy - ly, dz = in.cz - lz;
			float d = sqrtf(dx * dx + dy * dy + dz * dz);
			float cosA = clamp((dx * ax + dy * ay + dz * az) / d, -1.f, 1.f);
			float ang = acosf(cosA) + asinf(clamp(in.r / d, 0.f, 0.99f));
			maxAngle = max(maxAngle, ang);
		}
		float fov = clamp(2.f * maxAngle * 1.1f * (180.f / (float)M_PI) + 4.f, 10.f, 140.f);
		float nearp = 4.f, farp = radius + 128.f;

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
			for (auto &c : modelShadowCasters)
			{
				if (c.actor == infos[picked[k]].actor)
				{
					RenderModelShadowCaster(&renderer, c.x, c.y, c.z, c.smf, c.actor, c.ticFrac);
					break;
				}
			}
		}
		state.SetVertexBuffer(screen->mVertexData);
		state.SetColorMask(true);
		state.SetCulling(Cull_None);
		state.SetDepthFunc(DF_Less);

		screen->EndModelShadowMap();

		f.ModelLight[usedMaps][0] = lx; f.ModelLight[usedMaps][1] = ly; f.ModelLight[usedMaps][2] = lz; f.ModelLight[usedMaps][3] = radius;
		f.ModelNearFar[usedMaps][0] = nearp; f.ModelNearFar[usedMaps][1] = farp;
		VSMatrix vp;
		vp.loadMatrix(proj.get());
		vp.multMatrix(view.get());
		memcpy(f.ModelVP[usedMaps], vp.get(), sizeof(float) * 16);
		usedMaps++;
	}

	f.ModelShadowActive = usedMaps > 0;
	modelShadowCasters.Clear();
	// the caller binds its own viewpoint again afterwards
}
