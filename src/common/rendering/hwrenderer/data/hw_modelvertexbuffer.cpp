/*
** hw_modelvertexbuffer.cpp
**
** hardware renderer model handling code
**
**---------------------------------------------------------------------------
**
** Copyright 2005-2020 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/


#include "v_video.h"
#include "cmdlib.h"
#include "hw_modelvertexbuffer.h"
#include <unordered_map>
#include <cmath>

//===========================================================================
//
//
//
//===========================================================================

FModelVertexBuffer::FModelVertexBuffer(bool needindex, bool singleframe)
{
	mVertexBuffer = screen->CreateVertexBuffer();
	mIndexBuffer = needindex ? screen->CreateIndexBuffer() : nullptr;

	static const FVertexBufferAttribute format[] = {
		{ 0, VATTR_VERTEX, VFmt_Float3, (int)myoffsetof(FModelVertex, x) },
		{ 0, VATTR_TEXCOORD, VFmt_Float2, (int)myoffsetof(FModelVertex, u) },
		{ 0, VATTR_NORMAL, VFmt_Packed_A2R10G10B10, (int)myoffsetof(FModelVertex, packedNormal) },
		{ 0, VATTR_LIGHTMAP, VFmt_Float3, (int)myoffsetof(FModelVertex, lu) },
		{ 0, VATTR_BONESELECTOR, VFmt_Byte4_UInt, (int)myoffsetof(FModelVertex, boneselector[0])},
		{ 0, VATTR_BONEWEIGHT, VFmt_Byte4, (int)myoffsetof(FModelVertex, boneweight[0]) },
		{ 1, VATTR_VERTEX2, VFmt_Float3, (int)myoffsetof(FModelVertex, x) },
		{ 1, VATTR_NORMAL2, VFmt_Packed_A2R10G10B10, (int)myoffsetof(FModelVertex, packedNormal) }
	};
	mVertexBuffer->SetFormat(2, 8, sizeof(FModelVertex), format);
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertexBuffer::~FModelVertexBuffer()
{
	if (mIndexBuffer) delete mIndexBuffer;
	delete mVertexBuffer;
}

//===========================================================================
//
//
//
//===========================================================================

FModelVertex *FModelVertexBuffer::LockVertexBuffer(unsigned int size)
{
	mLockedVertices = static_cast<FModelVertex*>(mVertexBuffer->Lock(size * sizeof(FModelVertex)));
	mLockedCount = size;
	return mLockedVertices;
}

// Jupiter3D: smoothed normals for the outline hull.
// Hard edged meshes (a cube) have a separate vertex per face, and inflating each of them along its
// own face normal tears the hull apart at the edges. So every vertex gets the normal averaged over
// all vertices at the same position, stored octahedral encoded in the (for models unused) lightmap
// coordinates lu/lv. lindex stays -1, so the lightmap is not used.
static void EncodeHullNormal(float x, float y, float z, float &ou, float &ov)
{
	float l = fabsf(x) + fabsf(y) + fabsf(z);
	if (l < 1e-6f) { ou = 0.f; ov = 0.f; return; }
	x /= l; y /= l; z /= l;
	if (z < 0.f)
	{
		float nx = (1.f - fabsf(y)) * (x >= 0.f ? 1.f : -1.f);
		float ny = (1.f - fabsf(x)) * (y >= 0.f ? 1.f : -1.f);
		x = nx; y = ny;
	}
	ou = x; ov = y;
}

static void SmoothHullNormals(FModelVertex *v, unsigned int count)
{
	struct Key { int x, y, z; bool operator==(const Key &o) const { return x == o.x && y == o.y && z == o.z; } };
	struct KeyHash { size_t operator()(const Key &k) const { return (size_t)(k.x * 73856093) ^ (size_t)(k.y * 19349663) ^ (size_t)(k.z * 83492791); } };
	struct Sum { float x = 0, y = 0, z = 0; };
	std::unordered_map<Key, Sum, KeyHash> sums;
	sums.reserve(count);

	auto keyOf = [](const FModelVertex &m) { return Key{ (int)lrintf(m.x * 64.f), (int)lrintf(m.y * 64.f), (int)lrintf(m.z * 64.f) }; };
	auto unpack = [](unsigned p, float &nx, float &ny, float &nz)
	{
		auto sx = [](unsigned b) { int s = (int)(b & 1023); return s >= 512 ? s - 1024 : s; };
		nx = sx(p) / 512.f; ny = sx(p >> 10) / 512.f; nz = sx(p >> 20) / 512.f;
	};

	for (unsigned int i = 0; i < count; i++)
	{
		float nx, ny, nz;
		unpack(v[i].packedNormal, nx, ny, nz);
		Sum &s = sums[keyOf(v[i])];
		s.x += nx; s.y += ny; s.z += nz;
	}
	for (unsigned int i = 0; i < count; i++)
	{
		const Sum &s = sums[keyOf(v[i])];
		float u, w;
		EncodeHullNormal(s.x, s.y, s.z, u, w);
		v[i].lu = u;
		v[i].lv = w;
	}
}

//===========================================================================
//
//
//
//===========================================================================

void FModelVertexBuffer::UnlockVertexBuffer()
{
	if (mLockedVertices && mLockedCount > 0)
		SmoothHullNormals(mLockedVertices, mLockedCount);
	mLockedVertices = nullptr;
	mLockedCount = 0;
	mVertexBuffer->Unlock();
}

//===========================================================================
//
//
//
//===========================================================================

unsigned int *FModelVertexBuffer::LockIndexBuffer(unsigned int size)
{
	if (mIndexBuffer) return static_cast<unsigned int*>(mIndexBuffer->Lock(size * sizeof(unsigned int)));
	else return nullptr;
}

//===========================================================================
//
//
//
//===========================================================================

void FModelVertexBuffer::UnlockIndexBuffer()
{
	if (mIndexBuffer) mIndexBuffer->Unlock();
}
