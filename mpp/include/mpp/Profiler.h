#pragma once

#ifdef MPP_PROFILE_BUILD

#include <map>

#include "mpp/Config.h"

namespace mpp
{
	class Profiler
	{
		std::vector<std::string> mCounterNames;

	public:

		Profiler();

		~Profiler();

		void sample();

		std::map<std::string, uint64_t> getSamples();
	};
}

#endif