#if defined(_WIN32)
#	include <Windows.h>
#endif

#include "LegacyGpuValues.h"


#include "mpp/SamplerParams.h"

using namespace std;

namespace mpp
{

	SamplerParams::SamplerParams()
		: minFilter(GL_NEAREST)
		, magFilter(GL_NEAREST)
		, wrap(GL_REPEAT)
		, lodMinLevel(-1000.0f)
		, lodMaxLevel(1000.0f)
		, lodBias(0.0f)
		, maxAnisotropy(1.0f)
	{
	}

}