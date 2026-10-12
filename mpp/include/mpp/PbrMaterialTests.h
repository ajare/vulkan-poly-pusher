#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	// Context-free specialization derivation and source-generation tests.
	_MPPAPI bool runPbrMaterialSpecializationTests(std::string* failure = nullptr);
}
