#pragma once

#include "mpp/Config.h"

namespace mpp
{
	class _MPPAPI DataStream
	{
	public:

		virtual int getDataSize() const = 0;

		virtual int8_t const* getData() const = 0;
	};
}