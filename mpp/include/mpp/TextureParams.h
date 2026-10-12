#pragma once

#include "Config.h"

namespace mpp
{
	enum class TextureTarget
	{
		Texture1D,
		Texture2D,
		Texture3D,
		CubeMap
	};

	enum class TextureInternalType
	{
		Auto,
		UnsignedInteger,
		SignedInteger,
		Float
	};

	enum class TextureColourSpace
	{
		Linear,
		Srgb
	};

	struct _MPPAPI TextureParams
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

		bool useMipmaps;
		int32_t lodBaseLevel, lodMaxLevel;
		float lodBias;
		float maxAnisotropy;
		TextureColourSpace colourSpace;

	public:

		TextureParams();
	};
}