#pragma once

#include <string>

#include "mpp/DataStream.h"

namespace mpp
{
	class _MPPAPI FileDataStream
	{
		std::string mFileData;

	public:

		explicit FileDataStream(std::string const& filename);

		int getDataSize() const;

		int8_t const* getData() const;
	};
}