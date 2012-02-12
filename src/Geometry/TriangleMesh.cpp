#include <malloc.h>
#include <string.h>
#include "Math/float3.h"
#include "Geometry/Triangle.h"
#include "Geometry/Ray.h"
#include "Math/MathFwd.h"
#include "Math/MathConstants.h"
#include "TriangleMesh.h"
#include <cassert>
#include <intrin.h>
#include <emmintrin.h>
#include <smmintrin.h>
#include <xmmintrin.h>

// If defined, we preprocess our TriangleMesh data structure to contain (v0, v1-v0, v2-v0)
// instead of (v0, v1, v2) triplets for faster ray-triangle mesh intersection.
#define SOA_HAS_EDGES

MATH_BEGIN_NAMESPACE

enum SIMDCapability
{
	SIMD_NONE,
//	SIMD_SSE,
	SIMD_SSE2,
//	SIMD_SSE3,
//	SIMD_SSSE3,
//	SIMD_SSE4,
	SIMD_SSE41,
//	SIMD_SSE42,
	SIMD_AVX
};

SIMDCapability DetectSIMDCapability()
{
	int CPUInfo[4] = {-1};

	unsigned    nIds, nExIds, i;
	int nFeatureInfo = 0;
	bool    bSSE3Instructions = false;
    bool    bSupplementalSSE3 = false;
    bool    bCMPXCHG16B = false;
    bool    bSSE41Extensions = false;
    bool    bSSE42Extensions = false;
    bool    bPOPCNT = false;

    bool    bLAHF_SAHFAvailable = false;
    bool    bCmpLegacy = false;
    bool    bLZCNT = false;
    bool    bSSE4A = false;
    bool    bMisalignedSSE = false;
    bool    bPREFETCH = false;
    bool    bMMXExtensions = false;
    bool    b3DNowExt = false;
    bool    b3DNow = false;
    bool    bFP128 = false;
	bool    hasAVX = false;
    bool    bMOVOptimization = false;

	__cpuid(CPUInfo, 0);
    nIds = CPUInfo[0];

    // Get the information associated with each valid Id
    for (i=0; i<=nIds; ++i)
    {
        __cpuid(CPUInfo, i);

		// Interpret CPU feature information.
        if  (i == 1)
        {
            bSSE3Instructions = (CPUInfo[2] & 0x1) || false;
            bSupplementalSSE3 = (CPUInfo[2] & 0x200) || false;
            bCMPXCHG16B= (CPUInfo[2] & 0x2000) || false;
            bSSE41Extensions = (CPUInfo[2] & 0x80000) || false;
            bSSE42Extensions = (CPUInfo[2] & 0x100000) || false;
            bPOPCNT= (CPUInfo[2] & 0x800000) || false;
			hasAVX = (CPUInfo[2] & 0x10000000) || false;
			nFeatureInfo = CPUInfo[3];
        }
    }

	const bool hasMMX = (nFeatureInfo & (1 << 23)) != 0;
	const bool hasSSE = (nFeatureInfo & (1 << 25)) != 0;
	const bool hasSSE2 = (nFeatureInfo & (1 << 26)) != 0;

    // Calling __cpuid with 0x80000000 as the InfoType argument
    // gets the number of valid extended IDs.
    __cpuid(CPUInfo, 0x80000000);
    nExIds = CPUInfo[0];

    // Get the information associated with each extended ID.
    for (i=0x80000000; i<=nExIds; ++i)
    {
        __cpuid(CPUInfo, i);

        if  (i == 0x80000001)
        {
            bLAHF_SAHFAvailable = (CPUInfo[2] & 0x1) || false;
            bCmpLegacy = (CPUInfo[2] & 0x2) || false;
            bLZCNT = (CPUInfo[2] & 0x20) || false;
            bSSE4A = (CPUInfo[2] & 0x40) || false;
            bMisalignedSSE = (CPUInfo[2] & 0x80) || false;
            bPREFETCH = (CPUInfo[2] & 0x100) || false;
            bMMXExtensions = (CPUInfo[3] & 0x40000) || false;
            b3DNowExt = (CPUInfo[3] & 0x40000000) || false;
            b3DNow = (CPUInfo[3] & 0x80000000) || false;
        }

        if  (i == 0x8000001A)
        {
            bFP128 = (CPUInfo[0] & 0x1) || false;
            bMOVOptimization = (CPUInfo[0] & 0x2) || false;
        }
    }

	if (hasAVX)
		return SIMD_AVX;
	if (bSSE41Extensions)
		return SIMD_SSE41;
	if (hasSSE2)
		return SIMD_SSE2;
	return SIMD_NONE;
}

const int simdCapability = DetectSIMDCapability();
void TriangleMesh::Set(const float *triangleMesh, int numTriangles)
{
	if (simdCapability == SIMD_AVX)
		SetSoA8(triangleMesh, numTriangles);
	if (simdCapability == SIMD_SSE41 || simdCapability == SIMD_SSE2)
		SetSoA4(triangleMesh, numTriangles);
	else
		SetAoS(triangleMesh, numTriangles);
}

float TriangleMesh::IntersectRay(const Ray &ray) const
{
	if (simdCapability == SIMD_AVX)
		return IntersectRay_AVX(ray);
	if (simdCapability == SIMD_SSE41)
		return IntersectRay_SSE41(ray);
	if (simdCapability == SIMD_SSE2)
		return IntersectRay_SSE2(ray);
	else
	{
		int triangleIndex;
		float u, v;
		return IntersectRay_TriangleIndex_UV_CPP(ray, triangleIndex, u, v);
	}
}

float TriangleMesh::IntersectRay_TriangleIndex(const Ray &ray, int &outTriangleIndex) const
{
	if (simdCapability == SIMD_AVX)
		return IntersectRay_TriangleIndex_AVX(ray, outTriangleIndex);
	if (simdCapability == SIMD_SSE41)
		return IntersectRay_TriangleIndex_SSE41(ray, outTriangleIndex);
	if (simdCapability == SIMD_SSE2)
		return IntersectRay_TriangleIndex_SSE2(ray, outTriangleIndex);
	else
	{
		float u, v;
		return IntersectRay_TriangleIndex_UV_CPP(ray, outTriangleIndex, u, v);
	}
}

float TriangleMesh::IntersectRay_TriangleIndex_UV(const Ray &ray, int &outTriangleIndex, float &outU, float &outV) const
{
	if (simdCapability == SIMD_AVX)
		return IntersectRay_TriangleIndex_UV_AVX(ray, outTriangleIndex, outU, outV);
	if (simdCapability == SIMD_SSE41)
		return IntersectRay_TriangleIndex_UV_SSE41(ray, outTriangleIndex, outU, outV);
	if (simdCapability == SIMD_SSE2)
		return IntersectRay_TriangleIndex_UV_SSE2(ray, outTriangleIndex, outU, outV);
	else
		return IntersectRay_TriangleIndex_UV_CPP(ray, outTriangleIndex, outU, outV);
}

void TriangleMesh::ReallocVertexBuffer(int numTris)
{
	_aligned_free(data);
	data = (float*)_aligned_malloc(numTris*3*3*4, 32); // http://msdn.microsoft.com/en-us/library/8z34s9c6.aspx
	numTriangles = numTris;
}

void TriangleMesh::SetAoS(const float *vertexData, int numTriangles)
{
	ReallocVertexBuffer(numTriangles);
#ifdef _DEBUG
	vertexDataLayout = 0; // AoS
#endif

	memcpy(data, vertexData, numTriangles*3*3*4);
}

void TriangleMesh::SetSoA4(const float *vertexData, int numTriangles)
{
	ReallocVertexBuffer(numTriangles);
#ifdef _DEBUG
	vertexDataLayout = 1; // SoA4
#endif

	// From (xyz xyz xyz) (xyz xyz xyz) (xyz xyz xyz) (xyz xyz xyz)
	// To xxxx yyyy zzzz xxxx yyyy zzzz xxxx yyyy zzzz

	float *o = data;
	for(int i = 0; i + 4 <= numTriangles; i += 4)
	{
		for(int j = 0; j < 9; ++j)
		{
			*o++ = vertexData[0];
			*o++ = vertexData[9];
			*o++ = vertexData[18];
			*o++ = vertexData[27];
			++vertexData;
		}
		vertexData += 9 * 3;
	}

#ifdef SOA_HAS_EDGES
	o = data;
	for(int i = 0; i + 4 <= numTriangles; i += 4)
	{
		for(int j = 12; j < 24; ++j)
			o[j] -= o[j-12];
		for(int j = 24; j < 36; ++j)
			o[j] -= o[j-24];
		o += 36;
	}
#endif
}

void TriangleMesh::SetSoA8(const float *vertexData, int numTriangles)
{
	ReallocVertexBuffer(numTriangles);
#ifdef _DEBUG
	vertexDataLayout = 2; // SoA8
#endif

	// From (xyz xyz xyz) (xyz xyz xyz) (xyz xyz xyz) (xyz xyz xyz)
	// To xxxxxxxx yyyyyyyy zzzzzzzz xxxxxxxx yyyyyyyy zzzzzzzz xxxxxxxx yyyyyyyy zzzzzzzz

	float *o = data;
	for(int i = 0; i + 8 <= numTriangles; i += 8)
	{
		for(int j = 0; j < 9; ++j)
		{
			*o++ = vertexData[0];
			*o++ = vertexData[9];
			*o++ = vertexData[18];
			*o++ = vertexData[27];
			*o++ = vertexData[36];
			*o++ = vertexData[45];
			*o++ = vertexData[54];
			*o++ = vertexData[63];
			++vertexData;
		}
		vertexData += 9 * 7;
	}

#ifdef SOA_HAS_EDGES
	o = data;
	for(int i = 0; i + 8 <= numTriangles; i += 8)
	{
		for(int j = 24; j < 48; ++j)
			o[j] -= o[j-24];
		for(int j = 48; j < 72; ++j)
			o[j] -= o[j-48];
		o += 72;
	}
#endif
}

float TriangleMesh::IntersectRay_TriangleIndex_UV_CPP(const Ray &ray, int &outTriangleIndex, float &outU, float &outV) const
{
	assert(sizeof(float3) == 3*sizeof(float));
	assert(sizeof(Triangle) == 3*sizeof(float3));
	assert(vertexDataLayout == 0); // Must be AoS structured!

	float nearestD = FLOAT_INF;
	float u, v, d;

	const Triangle *tris = reinterpret_cast<const Triangle*>(data);
	for(int i = 0; i < numTriangles; ++i)
	{
		d = Triangle::IntersectLineTri(ray.pos, ray.dir, tris->a, tris->b, tris->c, u, v);
		if (d >= 0.f && d < nearestD)
		{
			nearestD = d;
			outU = u;
			outV = v;
			outTriangleIndex = i;
		}
		++tris;
	}

	return nearestD;
}

#define MATH_GEN_SSE2
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_SSE2
#define MATH_GEN_TRIANGLEINDEX
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_SSE2
#define MATH_GEN_TRIANGLEINDEX
#define MATH_GEN_UV
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_SSE41
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_SSE41
#define MATH_GEN_TRIANGLEINDEX
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_SSE41
#define MATH_GEN_TRIANGLEINDEX
#define MATH_GEN_UV
#include "TriangleMesh_IntersectRay_SSE.inl"

#define MATH_GEN_AVX
#include "TriangleMesh_IntersectRay_AVX.inl"

#define MATH_GEN_AVX
#define MATH_GEN_TRIANGLEINDEX
#include "TriangleMesh_IntersectRay_AVX.inl"

#define MATH_GEN_AVX
#define MATH_GEN_TRIANGLEINDEX
#define MATH_GEN_UV
#include "TriangleMesh_IntersectRay_AVX.inl"

MATH_END_NAMESPACE
