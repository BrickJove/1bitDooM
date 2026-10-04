/*
** modelrenderer.h
**
**
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
#include "renderstyle.h"
#include "matrix.h"
#include "model.h"

class FModelRenderer
{
public:
	virtual ~FModelRenderer() = default;

	virtual ModelRendererType GetType() const = 0;

	virtual void BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix &objectToWorldMatrix, bool mirrored) = 0;
	virtual void EndDrawModel(FRenderStyle style, int smf_flags) = 0;

	virtual IModelVertexBuffer *CreateVertexBuffer(bool needindex, bool singleframe) = 0;

	virtual VSMatrix GetViewToWorldMatrix() = 0;

	virtual void BeginDrawHUDModel(FRenderStyle style, const VSMatrix &objectToWorldMatrix, bool mirrored, int smf_flags) = 0;
	virtual void EndDrawHUDModel(FRenderStyle style, int smf_flags) = 0;

	virtual void SetInterpolation(double interpolation) = 0;
	virtual void SetMaterial(FGameTexture *skin, bool clampNoFilter, FTranslationID translation, AActor * act) = 0;
	virtual void DrawArrays(int start, int count) = 0;
	virtual void DrawElements(int numIndices, size_t offset) = 0;
	virtual void SetupFrame(FModel* model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex) {};

	// inverted hull outline pass. Called between BeginDrawModel/EndDrawModel.
	virtual void BeginOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud, float width, uint32_t color) {}
	virtual void EndOutline(FRenderStyle style, int smf_flags, bool mirrored, bool hud) {}

	// writes alpha 0 into the scene for every pixel of the HUD weapon model (mask for the weapon pixelation post process).
	// Returns false when not wanted. Call between BeginDrawHUDModel/EndDrawHUDModel.
	virtual bool BeginWeaponMask(FRenderStyle style) { return false; }
	virtual void EndWeaponMask(FRenderStyle style) {}

	// flat black drop shadow pass. Called between BeginDrawModel/EndDrawModel.
	virtual void BeginShadow(const VSMatrix& flatMatrix) {}
	virtual void EndShadow(const VSMatrix& objectToWorldMatrix, FRenderStyle style, int smf_flags, bool mirrored) {}
};
