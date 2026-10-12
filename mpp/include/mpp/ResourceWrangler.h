#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	class _MPPAPI ResourceWrangler
	{
		std::string mWranglerName;

	public:

		explicit ResourceWrangler(std::string const& name);

		virtual ~ResourceWrangler() = default;

		std::string const& getWranglerName() const;
	};
}