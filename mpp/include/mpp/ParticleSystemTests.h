#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	// Context-free coverage for handles, effect grouping and CPU lifetime rules.
	_MPPAPI bool runParticleSystemCpuTests(std::string* failure = nullptr);
}
