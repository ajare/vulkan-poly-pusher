#pragma once

#include <cstdint>

namespace mpp
{

	// Values intentionally match the rendering backend, but the public API does
	// not require clients to include that backend's headers.
	enum class BlendMode : std::uint32_t
	{
		Zero = 0,
		One = 1,
		SrcColour = 0x0300,
		OneMinusSrcColour = 0x0301,
		SrcAlpha = 0x0302,
		OneMinusSrcAlpha = 0x0303,
		DstAlpha = 0x0304,
		OneMinusDstAlpha = 0x0305,
		DstColour = 0x0306,
		OneMinusDstColour = 0x0307
	};

}