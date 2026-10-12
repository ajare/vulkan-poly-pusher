		#pragma once

#include "mpp/Config.h"

namespace mpp
{
	struct _MPPAPI SamplerParams
	{
		enum class MinFilter
		{
			Nearest,
			Linear,
			NearestMipmapNearest,
			LinearMipmapNearest,
			NearestMipmapLinear,
			LinearMipmapLinear
		};

		enum class MagFilter
		{
			Nearest,
			Linear
		};

		enum class Wrapping
		{
			Repeat,
			MirroredRepeat,
			ClampToEdge,
			ClampToBorder
		};

	public:

		uint32_t minFilter;
		uint32_t magFilter;
		uint32_t wrap;

		float lodMinLevel, lodMaxLevel;
		float lodBias;
		float maxAnisotropy;

	public:

		SamplerParams();
	};

}
