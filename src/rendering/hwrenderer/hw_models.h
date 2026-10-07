/*
** hw_models.h
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

#pragma once

#include "tarray.h"
#include "p_pspr.h"
#include "voxels.h"
#include "models.h"
#include "hwrenderer/data/buffers.h"
#include "hw_modelvertexbuffer.h"
#include "modelrenderer.h"

class HWSprite;
struct HWDrawInfo;
class FRenderState;


class FHWModelRenderer : public FModelRenderer
{
	friend class FModelVertexBuffer;
	int modellightindex = -1;
	int boneIndexBase = -1;
	HWDrawInfo *di;
	FRenderState &state;
public:
	FHWModelRenderer(HWDrawInfo *d, FRenderState &st, int mli) : modellightindex(mli), di(d), state(st)
	{}
	ModelRendererType GetType() const override { return GLModelRendererType; }
	void BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix &objectToWorldMatrix, bool mirrored) override;
	void EndDrawModel(FRenderStyle style, int smf_flags) override;
	IModelVertexBuffer *CreateVertexBuffer(bool needindex, bool singleframe) override;
	VSMatrix GetViewToWorldMatrix() override;
	void BeginDrawHUDModel(FRenderStyle style, const VSMatrix &objectToWorldMatrix, bool mirrored, int smf_flags) override;
	void EndDrawHUDModel(FRenderStyle style, int smf_flags) override;
	void SetInterpolation(double interpolation) override;
	void SetMaterial(FGameTexture *skin, bool clampNoFilter, FTranslationID translation, AActor * act) override;
	void DrawArrays(int start, int count) override;
	void DrawElements(int numIndices, size_t offset) override;
	void SetupFrame(FModel *model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex) override;
	void BeginOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud, float width, uint32_t color, bool pixels) override;
	void EndOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud) override;
	void BeginBlack() override { blackActive = true; }
	void EndBlack() override;
	void BeginShadow(const VSMatrix& flatMatrix) override;
	void EndShadow(const VSMatrix& objectToWorldMatrix, FRenderStyle style, int smf_flags, bool mirrored) override;

private:
	bool shadowActive = false;
	bool blackActive = false;
	float outlineWidth = 0.f;
	bool outlinePixels = false;	// outlineWidth is in screen pixels, not map units
	uint32_t outlineColor = 0xff000000;
	float savedAlphaThreshold = 0.5f;

};
