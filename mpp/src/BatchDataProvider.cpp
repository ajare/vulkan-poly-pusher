#include "mpp/BatchDataProvider.h"

namespace mpp
{
	bool BatchDataProvider::update(float frameTime)
	{
		MPP_UNUSED(frameTime);
		return false;
	}

	void BatchDataProvider::setNumPrimitives(size_t numPrimitives)
	{
		mNumPrimitives = numPrimitives;
	}

	size_t BatchDataProvider::getNumPrimitives() const
	{
		return mNumPrimitives;
	}

}