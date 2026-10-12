#if defined(_WIN32)
#	include <Windows.h>
#endif

#include "LegacyGpuValues.h"


#include "mpp/TextureParams.h"

using namespace std;

namespace mpp
{

	TextureParams::TextureParams()
		: minFilter(GL_NEAREST)
		, magFilter(GL_NEAREST)
		, wrap(GL_REPEAT)
		, useMipmaps(false)
		, lodBaseLevel(0)
		, lodMaxLevel(1000)
		, lodBias(0.0f)
		, maxAnisotropy(1.0f)
		, colourSpace(TextureColourSpace::Linear)
	{
	}

}