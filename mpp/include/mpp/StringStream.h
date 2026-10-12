#pragma once

#include <string>

#include "mpp/ResourceStream.h"

namespace mpp
{
	class _MPPAPI StringStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		std::string mData;
		std::string mFile;
		bool mIsFile{ false };

	public:
		explicit StringStream(ResourceManager* resourceMgr);
		std::string const& getString() const;
	};
}
