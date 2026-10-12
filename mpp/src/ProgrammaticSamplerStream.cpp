#if defined(_WIN32)
#	include <Windows.h>
#endif

#include "LegacyGpuValues.h"


#include <cassert>

#include "mpp/ProgrammaticSamplerStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{

	ProgrammaticSamplerStream::ProgrammaticSamplerStream(ResourceManager* resourceMgr)
		: SamplerStream(resourceMgr)
	{
	}

	void ProgrammaticSamplerStream::setFiltering(SamplerParams::MinFilter minFilter, SamplerParams::MagFilter magFilter)
	{
		switch (minFilter)
		{
		case SamplerParams::MinFilter::Nearest:
			mParams.minFilter = GL_NEAREST;
			break;

		case SamplerParams::MinFilter::Linear:
			mParams.minFilter = GL_LINEAR;
			break;

		case SamplerParams::MinFilter::NearestMipmapNearest:
			mParams.minFilter = GL_NEAREST_MIPMAP_NEAREST;
			break;

		case SamplerParams::MinFilter::LinearMipmapNearest:
			mParams.minFilter = GL_LINEAR_MIPMAP_NEAREST;
			break;

		case SamplerParams::MinFilter::NearestMipmapLinear:
			mParams.minFilter = GL_NEAREST_MIPMAP_LINEAR;
			break;

		case SamplerParams::MinFilter::LinearMipmapLinear:
			mParams.minFilter = GL_LINEAR_MIPMAP_LINEAR;
			break;

		default:
			THROW_MPP("Unknown texture filter setting.", __LINE__, __FILE__, __func__);
		}

		switch (magFilter)
		{
		case SamplerParams::MagFilter::Nearest:
			mParams.magFilter = GL_NEAREST;
			break;

		case SamplerParams::MagFilter::Linear:
			mParams.magFilter = GL_LINEAR;
			break;

		default:
			THROW_MPP("Unknown texture filter setting.", __LINE__, __FILE__, __func__);
		}
	}

	void ProgrammaticSamplerStream::setWrapping(SamplerParams::Wrapping wrapping)
	{
		switch (wrapping)
		{
		case SamplerParams::Wrapping::Repeat:
			mParams.wrap = GL_REPEAT;
			break;

		case SamplerParams::Wrapping::MirroredRepeat:
			mParams.wrap = GL_MIRRORED_REPEAT;
			break;

		case SamplerParams::Wrapping::ClampToEdge:
			mParams.wrap = GL_CLAMP_TO_EDGE;
			break;

		case SamplerParams::Wrapping::ClampToBorder:
			mParams.wrap = GL_CLAMP_TO_BORDER;
			break;

		default:
			THROW_MPP("Unknown texture wrap setting.", __LINE__, __FILE__, __func__);
		}
	}

	void ProgrammaticSamplerStream::setLodMinLevel(float level)
	{
		mParams.lodMinLevel = level;
	}

	void ProgrammaticSamplerStream::setLodMaxLevel(float level)
	{
		mParams.lodMaxLevel = level;
	}

	void ProgrammaticSamplerStream::setLodBias(float bias)
	{
		mParams.lodBias = bias;
	}

	void ProgrammaticSamplerStream::setMaxAnisotropy(float maxAnisotropy)
	{
		mParams.maxAnisotropy = maxAnisotropy;
	}
}