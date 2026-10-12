#include <cassert>

#include "mpp/TextureStreamBase.h"

using namespace std;

namespace mpp
{

	TextureStreamBase::TextureStreamBase()
		: mInternalFormat(0)
		, mTarget(0)
	{
	}

	uint32_t TextureStreamBase::getInternalFormat() const
	{
		return mInternalFormat;
	}

	uint32_t TextureStreamBase::getTarget() const
	{
		return mTarget;
	}

}