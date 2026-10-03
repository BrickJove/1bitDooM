/*
** hw_models.cpp
**
** hardware renderer model handling code
**
**---------------------------------------------------------------------------
**
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "filesystem.h"
#include "g_game.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_state.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "i_time.h"
#include "cmdlib.h"
#include "hw_material.h"
#include "hwrenderer/data/buffers.h"
#include "flatvertices.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hw_renderstate.h"
#include "hwrenderer/scene/hw_portal.h"
#include "hw_bonebuffer.h"
#include "hw_models.h"
#include "actor.h"

CVAR(Bool, gl_light_models, true, CVAR_ARCHIVE)

VSMatrix FHWModelRenderer::GetViewToWorldMatrix()
{
	VSMatrix objectToWorldMatrix;
	di->VPUniforms.ViewMatrix.inverseMatrix(objectToWorldMatrix);
	return objectToWorldMatrix;
}

void FHWModelRenderer::BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix &objectToWorldMatrix, bool mirrored)
{
	state.SetDepthFunc(DF_LEqual);
	state.EnableTexture(true);
	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// [Nash] Don't do back face culling if explicitly specified in MODELDEF
	// TO-DO: Implement proper depth sorting.
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
	{
		state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CCW : Cull_CW);
	}

	state.mModelMatrix = objectToWorldMatrix;
	state.EnableModelMatrix(true);
	state.SetNoOutline(true);
}

void FHWModelRenderer::EndDrawModel(FRenderStyle style, int smf_flags)
{
	state.SetBoneIndexBase(-1);
	state.EnableModelMatrix(false);
	state.SetNoOutline(false);
	state.SetDepthFunc(DF_Less);
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
		state.SetCulling(Cull_None);
}

void FHWModelRenderer::BeginDrawHUDModel(FRenderStyle style, const VSMatrix &objectToWorldMatrix, bool mirrored, int smf_flags)
{
	state.SetDepthFunc(DF_LEqual);
	state.SetDepthClamp(true);

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
	{
		state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CW : Cull_CCW);
	}

	state.mModelMatrix = objectToWorldMatrix;
	state.EnableModelMatrix(true);
	state.SetNoOutline(true);
}

void FHWModelRenderer::EndDrawHUDModel(FRenderStyle style, int smf_flags)
{
	state.SetBoneIndexBase(-1);
	state.EnableModelMatrix(false);
	state.SetNoOutline(false);

	state.SetDepthFunc(DF_Less);
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
		state.SetCulling(Cull_None);
}

IModelVertexBuffer *FHWModelRenderer::CreateVertexBuffer(bool needindex, bool singleframe)
{
	return new FModelVertexBuffer(needindex, singleframe);
}

void FHWModelRenderer::SetInterpolation(double inter)
{
	state.SetInterpolationFactor((float)inter);
}

void FHWModelRenderer::SetMaterial(FGameTexture *skin, bool clampNoFilter, FTranslationID translation, AActor * act)
{
	state.SetMaterial(skin, UF_Skin, 0, clampNoFilter ? CLAMP_NOFILTER : CLAMP_NONE, translation, -1, act->GetClass());
	state.SetLightIndex(modellightindex);

	if (outlineWidth > 0.f)
	{
		// Outline pass: a negative alpha threshold tells main.vp to push the vertices
		// out along their normals by that many map units, and main.fp to output a flat colour.
		state.SetTextureMode(TM_STENCIL);
		state.SetObjectColor(PalEntry(outlineColor));
		state.SetOutlineHull(outlineWidth);
	}
	else if (shadowActive)
	{
		// Shadow pass: flat black, keeps the texture alpha so cut-out parts stay cut out.
		state.SetTextureMode(TM_STENCIL);
		state.SetObjectColor(0xff000000);
	}
}

// draws the model squashed onto the floor in flat black (hard silhouette shadow)
void FHWModelRenderer::BeginShadow(const VSMatrix& flatMatrix)
{
	shadowActive = true;
	state.mModelMatrix = flatMatrix;
	state.SetCulling(Cull_None);
	state.SetDepthBias(-1.f, -128.f);
}

void FHWModelRenderer::EndShadow(const VSMatrix& objectToWorldMatrix, FRenderStyle style, int smf_flags, bool mirrored)
{
	shadowActive = false;
	state.ClearDepthBias();
	state.SetObjectColor(0xffffffff);
	state.SetTextureMode(TM_NORMAL);
	state.mModelMatrix = objectToWorldMatrix;

	// restore culling as BeginDrawModel left it
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
		state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CCW : Cull_CW);
}

// draws the model a second time, inflated along its normals with front faces culled,
// so that only a rim of back faces is left visible around the silhouette.
void FHWModelRenderer::BeginOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud, float width, uint32_t color)
{
	outlineWidth = width;
	outlineColor = color;
	savedAlphaThreshold = state.GetAlphaThreshold();
	state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CW : Cull_CCW);	// cull front faces
}

void FHWModelRenderer::EndOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud)
{
	outlineWidth = 0.f;
	state.SetOutlineHull(0.f, savedAlphaThreshold);
	state.SetObjectColor(0xffffffff);
	state.SetTextureMode(TM_NORMAL);

	// restore culling as BeginDrawModel / BeginDrawHUDModel left it
	bool cull = hud ? ((smf_flags & MDL_FORCECULLBACKFACES) || !(style == DefaultRenderStyle()))
	                : ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)));
	if (!cull) state.SetCulling(Cull_None);
	else if (hud) state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CW : Cull_CCW);
	else state.SetCulling((mirrored ^ portalState.isMirrored()) ? Cull_CCW : Cull_CW);
}

void FHWModelRenderer::DrawArrays(int start, int count)
{
	state.Draw(DT_Triangles, start, count);
}

void FHWModelRenderer::DrawElements(int numIndices, size_t offset)
{
	state.DrawIndexed(DT_Triangles, int(offset / sizeof(unsigned int)), numIndices);
}

//===========================================================================
//
//
//
//===========================================================================

void FHWModelRenderer::SetupFrame(FModel *model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex)
{
	auto mdbuff = static_cast<FModelVertexBuffer*>(model->GetVertexBuffer(GetType()));
	//boneIndexBase = boneStartIndex;//boneStartIndex >= 0 ? boneStartIndex : screen->mBones->UploadBones(bones);
	state.SetBoneIndexBase(boneStartIndex);
	if (mdbuff)
	{
		state.SetVertexBuffer(mdbuff->vertexBuffer(), frame1, frame2);
		if (mdbuff->indexBuffer()) state.SetIndexBuffer(mdbuff->indexBuffer());
	}
}
