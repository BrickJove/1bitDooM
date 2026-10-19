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
#include "p_local.h"
#include "flatvertices.h"
#include "hw_models.h"
#include "models.h"
#include "actor.h"
#include "actorinlines.h"
#include "printf.h"
#include "g_levellocals.h"
#include "doom_levelmesh.h"
#include <algorithm>
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

#include <unordered_map>

// the models that got a shadow from a light map in the last frame: they need no flat drop shadow
// (with how strong it is: 1 = full, smaller at the edge of the radius of the light, where it fades into the flat one)
static std::unordered_map<const AActor *, float> g_coveredModels, g_buildModels;

float ModelMapShadowWeight(const AActor *actor)
{
	auto it = g_coveredModels.find(actor);
	return it == g_coveredModels.end() ? 0.f : it->second;
}

static void RenderModelShadowMapsImpl(HWDrawInfo *di, FRenderState &state);

void hw_RenderModelShadowMaps(HWDrawInfo *di, FRenderState &state)
{
	g_buildModels.clear();
	RenderModelShadowMapsImpl(di, state);
	g_coveredModels.swap(g_buildModels);
}

static void RenderModelShadowMapsImpl(HWDrawInfo *di, FRenderState &state)
{
	SceneShadowFrame &f = sceneShadowFrame;
	f.ModelShadowActive = false;
	f.ModelSunMap = -1;
	f.ModelSunHalf = 0.f;
	for (int i = 0; i < 4; i++)
	{
		f.ModelLight[i][3] = 0.f;
		f.ModelWorld[i] = false;
	}

	if (!gl_model_shadow_map || !gl_scene_shadow || !f.Valid)
		return;

	// the lights of the map (not only the ones nearest to the camera: the model that casts can be far away from it)
	struct Lt { float x, y, z, radius, reach; AActor *source; };	// radius: which models the light affects, reach: how far their shadows go
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
			Lt lt = { (float)lp.X, height, (float)lp.Y, radius, (gl_model_shadow_map_unlimited ? 8192.f : radius), light->target };
			lights.Push(lt);
			if (lights.Size() >= 512)
				break;
		}
	}
	// the sun: one more point light (above the centre of the level, as in the scene shadows) that reaches every model;
	// it always gets the first map. Its shadows only fall on pixels under an open sky (modelshadow.fp).
	int sunLight = -1;
	if (gl_model_shadow_map_sun && gl_scene_shadow_sun && f.SunPosValid)
	{
		// reach: the height of the sun plus the extent of a level (a sun at 8192 is further away than 8192 from everything)
		Lt lt = { f.SunPos[0], f.SunPos[1], f.SunPos[2], 16384.f + f.SunPos[1], f.SunPos[1] + 16384.f, nullptr };
		sunLight = (int)lights.Size();
		lights.Push(lt);
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
		float camD;		// squared distance to the camera (if there are more than the limit, the nearest ones are kept)
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
			{
				const float ddx = cx - (float)di->Viewpoint.Pos.X, ddy = cy - (float)di->Viewpoint.Pos.Z, ddz = cz - (float)di->Viewpoint.Pos.Y;
				in.camD = ddx * ddx + ddy * ddy + ddz * ddz;
			}
			infos.Push(in);
			if (infos.Size() >= 4096)
				break;
		}
		// the limit: the nearest ones to the camera (not the first ones in the list of the engine: with many decorations the
		// monsters would never be among them)
		if (infos.Size() > 128)
		{
			std::sort(infos.begin(), infos.end(), [](const Info &a, const Info &b) { return a.camD < b.camD; });
			infos.Resize(128);
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

	int maxMaps = clamp((int)gl_model_shadow_map_lights + (sunLight >= 0 ? 1 : 0), 1, 4);
	int usedMaps = 0;
	FString sunDbg, sunList;
	int pickedCount[4] = { 0, 0, 0, 0 };

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
			// among the lights that reach a model: the ones that reach the most models first (does not depend on the player)
			int count = 0;
			for (unsigned i = 0; i < infos.Size(); i++)
			{
				float dx = infos[i].cx - lights[li].x, dy = infos[i].cy - lights[li].y, dz = infos[i].cz - lights[li].z;
				if (sqrtf(dx * dx + dy * dy + dz * dz) < lights[li].radius + infos[i].r)
					count++;
			}
			best = -(float)count;
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

	// the sun first
	if (sunLight >= 0)
	{
		for (unsigned oi = 0; oi < order.Size(); oi++)
		{
			if (order[oi] == sunLight)
			{
				for (unsigned k = oi; k > 0; k--)
					order[k] = order[k - 1];
				order[0] = sunLight;
				break;
			}
		}
	}

	for (unsigned oi = 0; oi < order.Size() && usedMaps < maxMaps; oi++)
	{
		const bool isSun = order[oi] == sunLight;
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
			// a model behind a wall (seen from the light) casts nothing
			if (L.source != nullptr && L.source != infos[i].actor && !P_CheckSight(L.source, infos[i].actor, SF_IGNOREVISIBILITY | SF_IGNOREWATERBOUNDARY | SF_SEEPASTSHOOTABLELINES))
				continue;
			// the sun does not shine through a ceiling: only models in a sector with a sky ceiling cast its shadow
			if (isSun && (infos[i].actor->Sector == nullptr || infos[i].actor->Sector->GetTexture(sector_t::ceiling) != skyflatnum))
				continue;
			picked.Push(i);
		}
		if (picked.Size() == 0)
			continue;

		// The sun: a perspective depth map from the position of the sun (a point, as in the scene shadows), narrowed to the area
		// around the player (a 2 x 2 mosaic of the tiles of the atlas, 1536 x 1536 texels). A sun that is near casts shadows in
		// different directions on different places; a parallel light would not fit the map geometry. Models outside of the
		// area cast no sun shadow on walls (they keep the flat shadow on the floor).
		if (isSun)
		{
			const float H = clamp((float)gl_model_shadow_sun_size, 128.f, 4096.f);
			// centre of the area: a bit in front of the camera
			const DVector3 camPos = di->Viewpoint.Pos;
			const DVector2 fwd = di->Viewpoint.Angles.Yaw.ToVector();
			// The centre jumps in steps (a lattice): the map then keeps its direction while the player walks, and the texels of the
			// map stay on the same places on the walls (a centre that follows the player every frame makes the shadow edges flicker).
			const float snap = 128.f;
			const float cx = floorf((float)(camPos.X + fwd.X * H * 0.4) / snap + 0.5f) * snap;
			const float cy = floorf((float)camPos.Z / 64.f + 0.5f) * 64.f;
			const float cz = floorf((float)(camPos.Y + fwd.Y * H * 0.4) / snap + 0.5f) * snap;
			float fx = cx - lx, fy = cy - ly, fz = cz - lz;
			const float dist = sqrtf(fx * fx + fy * fy + fz * fz);
			if (dist < 16.f)
				continue;
			fx /= dist; fy /= dist; fz /= dist;	// viewing direction of the map: from the sun to the area
			float ux = 0.f, uy = 1.f, uz = 0.f;
			if (fabsf(fy) > 0.99f) { ux = 0.f; uy = 0.f; uz = 1.f; }	// as in modelshadow.fp
			float rx = fy * uz - fz * uy, ry = fz * ux - fx * uz, rz = fx * uy - fy * ux;
			{ const float rl = sqrtf(rx * rx + ry * ry + rz * rz); rx /= rl; ry /= rl; rz /= rl; }	// unit length, as lookAt() and the shader
			const float u2x = ry * fz - rz * fy, u2y = rz * fx - rx * fz, u2z = rx * fy - ry * fx;
			// half edge of the whole mosaic as a tangent (covers the corners of the square area)
			const float T = clamp(H * 1.45f / dist, 0.02f, 1.2f);
			const float sunNear = max(1.f, dist - 3000.f), sunFar = dist + 3000.f;

			if (!screen->BeginModelShadowMap(usedMaps))
				break;
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
			int nCast = 0;
			for (int q = 0; q < 4; q++)
			{
				const int qx = q & 1, qy = q >> 1;
				const float left = -T + qx * T, right = left + T, bottom = -T + qy * T, top = bottom + T;	// tangent ranges of the tile
				TArray<int> inTile;
				for (unsigned k = 0; k < picked.Size(); k++)
				{
					const Info &in = infos[picked[k]];
					float rx0 = in.cx - lx, ry0 = in.cy - ly, rz0 = in.cz - lz;
					float m = rx0 * fx + ry0 * fy + rz0 * fz;
					if (m < sunNear * 0.5f)
						continue;
					float a = (rx0 * rx + ry0 * ry + rz0 * rz) / m, b = (rx0 * u2x + ry0 * u2y + rz0 * u2z) / m;
					float rad = in.r * 1.05f / m;
					if (a + rad < left || a - rad > right || b + rad < bottom || b - rad > top)
						continue;
					inTile.Push(picked[k]);
				}
				if (inTile.Size() == 0)
					continue;

				screen->SelectModelShadowFace(qx + 3 * qy);
				VSMatrix proj;
				proj.loadIdentity();	// frustum() multiplies onto the matrix
				proj.frustum(left * sunNear, right * sunNear, bottom * sunNear, top * sunNear, sunNear, sunFar);
				VSMatrix view;
				view.loadIdentity();
				view.lookAt(lx, ly, lz, lx + fx, ly + fy, lz + fz, ux, uy, uz);
				HWViewpointUniforms vpu = di->VPUniforms;
				vpu.ProjectionMatrix = proj;
				vpu.ViewMatrix = view;
				vpu.CalcDependencies();
				vpu.mCameraPos = FVector4(lx, ly, lz, 1.f);
				vpu.mClipLine = FVector4(-10000001.f, 0.f, 0.f, 0.f);
				vpu.mClipHeight = 0.f;
				vpu.mClipHeightDirection = 0.f;
				screen->mViewpoints->SetViewpoint(state, &vpu);
				for (unsigned k = 0; k < inTile.Size(); k++)
				{
					const Info &in = infos[inTile[k]];
					RenderModelShadowCaster(&renderer, in.x, in.y, in.z, in.smf, in.actor, in.ticFrac, 1.f, in.z);
					nCast++;
					if (nCast <= 16)
					{
						FString c; c.Format(" [%s q%d d%.0f]", in.actor->GetClass()->TypeName.GetChars(), q, sqrtf((in.cx - cx) * (in.cx - cx) + (in.cz - cz) * (in.cz - cz)));
						sunList += c;
					}
				}
			}
			state.SetVertexBuffer(screen->mVertexData);
			state.SetColorMask(true);
			state.SetCulling(Cull_None);
			state.SetDepthFunc(DF_Less);
			screen->EndModelShadowMap();

			f.ModelWorld[usedMaps] = false;
			f.ModelLight[usedMaps][0] = lx; f.ModelLight[usedMaps][1] = ly; f.ModelLight[usedMaps][2] = lz; f.ModelLight[usedMaps][3] = 1000000.f;
			f.ModelNearFar[usedMaps][0] = sunNear; f.ModelNearFar[usedMaps][1] = sunFar;
			f.ModelSunDir[0] = fx; f.ModelSunDir[1] = fy; f.ModelSunDir[2] = fz;
			f.ModelSunHalf = T;	// tangent of the half edge of the mosaic
			f.ModelSunMap = usedMaps;
			pickedCount[usedMaps] = nCast;
			sunDbg.Format("; sun area: centre %.0f %.0f %.0f, distance %.0f, tangent %.3f, models per tile cast %d", cx, cy, cz, dist, T, nCast);
			usedMaps++;
			continue;
		}

		// A fixed map in the six directions around the light (like a cube map, a 3 x 2 atlas of 90 degree faces):
		// nothing in it depends on which models there are, so the shadows do not flicker when models come and go.
		// Every model is drawn into the faces it reaches only.
		// the sun is far away from everything: a larger near plane keeps the depth precision
		const float nearp = isSun ? clamp(ly - 4096.f, 4.f, ly * 0.5f) : 4.f;
		const float farp = reach + 128.f;
		static const float faceF[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		static const float faceU[6][3] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };	// as in modelshadow.fp

		// the level geometry seen from the light (depth only, rendered once per light position): walls end the shadows
		{
			VSMatrix wproj;
			wproj.loadIdentity();
			wproj.perspective(90.f, 1.f, nearp, farp);
			float faceVP[6 * 16];
			for (int face = 0; face < 6; face++)
			{
				VSMatrix wview;
				wview.loadIdentity();
				wview.lookAt(lx, ly, lz, lx + faceF[face][0], ly + faceF[face][1], lz + faceF[face][2], faceU[face][0], faceU[face][1], faceU[face][2]);
				VSMatrix wvp;
				wvp.loadMatrix(wproj.get());
				wvp.multMatrix(wview.get());
				memcpy(faceVP + face * 16, wvp.get(), sizeof(float) * 16);
			}
			auto *mesh = di->Level->levelMesh;
			if (mesh != nullptr && !isSun)
				screen->SetModelShadowWorld((const float *)mesh->MeshVertices.Data(), mesh->MeshVertices.Size(), (const unsigned int *)mesh->MeshElements.Data(), mesh->MeshElements.Size(), mesh);
			float lp[3] = { lx, ly, lz };
			// no world map for the sun: the roofs of the level (sky ceilings) would hide everything below them, and the
			// pixels under an open sky are chosen in modelshadow.fp
			f.ModelWorld[usedMaps] = isSun ? false : screen->RenderModelShadowWorld(usedMaps, lp, farp, faceVP);
		}

		if (!screen->BeginModelShadowMap(usedMaps))
			break;

		VSMatrix proj;
		proj.loadIdentity();
		proj.perspective(90.f, 1.f, nearp, farp);

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
		for (int face = 0; face < 6; face++)
		{
			const float *ff = faceF[face], *fu = faceU[face];
			const float rx = ff[1] * fu[2] - ff[2] * fu[1], ry = ff[2] * fu[0] - ff[0] * fu[2], rz = ff[0] * fu[1] - ff[1] * fu[0];
			const float ux = ry * ff[2] - rz * ff[1], uy = rz * ff[0] - rx * ff[2], uz = rx * ff[1] - ry * ff[0];

			// the models that reach into the pyramid of this face
			TArray<int> inFace;
			for (unsigned k = 0; k < picked.Size(); k++)
			{
				const Info &in = infos[picked[k]];
				float dx = in.cx - lx, dy = in.cy - ly, dz = in.cz - lz;
				float df = dx * ff[0] + dy * ff[1] + dz * ff[2];
				float dr = dx * rx + dy * ry + dz * rz;
				float du = dx * ux + dy * uy + dz * uz;
				float rad = in.r * 1.05f;
				if (df + rad <= nearp)
					continue;
				if ((df - dr) * 0.70711f < -rad || (df + dr) * 0.70711f < -rad || (df - du) * 0.70711f < -rad || (df + du) * 0.70711f < -rad)
					continue;
				inFace.Push(picked[k]);
			}
			if (inFace.Size() == 0)
				continue;

			screen->SelectModelShadowFace(face);
			VSMatrix view;
			view.loadIdentity();
			view.lookAt(lx, ly, lz, lx + ff[0], ly + ff[1], lz + ff[2], fu[0], fu[1], fu[2]);

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

			for (unsigned k = 0; k < inFace.Size(); k++)
			{
				const Info &in = infos[inFace[k]];
				// the length of the shadow is limited: a model is squashed towards its feet so that the whole shadow
				// (feet to head) stays within gl_model_shadow_max_length, as if the light were higher
				float squash = 1.f, fadeW = 1.f;
				const float maxLen = gl_model_shadow_max_length;
				if (maxLen > 1.f)
				{
					float h = ly - in.z;	// height of the light above the feet
					float hm = (float)in.actor->Height;
					float ddx = in.x - lx, ddy = in.y - lz;
					float D = sqrtf(ddx * ddx + ddy * ddy);
					if (h > 1.f && hm > 1.f)
						squash = clamp(h / (hm * (1.f + D / maxLen)), 0.15f, 1.f);
				}
				// at the edge of the radius of the light the shadow shrinks to nothing instead of vanishing at once
				{
					float fx = in.cx - lx, fy = in.cy - ly, fz = in.cz - lz;
					float fd = sqrtf(fx * fx + fy * fy + fz * fz);
					fadeW = clamp((radius - fd) / (radius * 0.3f), 0.f, 1.f);
					// towards the edge of the radius the model is squashed down to (almost) nothing: its shadow becomes the
					// flat one straight below it, which takes over when the model leaves the radius
					squash = max(squash * fadeW, 0.03f);
				}
				{
					if (!isSun)	// the flat shadow of the sun on the floor stays (it is sharper than the map of the sun)
					{
						float &w = g_buildModels[in.actor];
						w = max(w, fadeW);
					}
				}
				RenderModelShadowCaster(&renderer, in.x, in.y, in.z, in.smf, in.actor, in.ticFrac, squash, in.z);
			}
		}
		state.SetVertexBuffer(screen->mVertexData);
		state.SetColorMask(true);
		state.SetCulling(Cull_None);
		state.SetDepthFunc(DF_Less);

		screen->EndModelShadowMap();

		f.ModelLight[usedMaps][0] = lx; f.ModelLight[usedMaps][1] = ly; f.ModelLight[usedMaps][2] = lz; f.ModelLight[usedMaps][3] = reach;
		f.ModelNearFar[usedMaps][0] = nearp; f.ModelNearFar[usedMaps][1] = farp;
		pickedCount[usedMaps] = (int)picked.Size();
		if (isSun)
			f.ModelSunMap = usedMaps;
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
		t += sunDbg; t += "; sun models (class, tile, distance from the area centre):"; t += sunList;
		{ FString u; u.Format("; sun map: %d (sun light %s)", f.ModelSunMap, sunLight >= 0 ? "on" : "off: needs gl_model_shadow_map_sun, gl_scene_shadow_sun"); t += u; }
		ModelShadowReport(t);
	}
	// the caller binds its own viewpoint again afterwards
}
