#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	// Context-free regression suite for CI/test hosts. It creates no GL objects.
	_MPPAPI bool runRenderGraphTopologyTests(std::string* failure = nullptr);
}
