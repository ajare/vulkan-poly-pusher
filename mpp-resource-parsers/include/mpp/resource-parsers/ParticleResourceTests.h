#pragma once

#include <string>

#include "mpp/resource-parsers/Config.h"

namespace mpp::resource_parsers
{
	// Context-free coverage for particle data consumed by authored resources.
	_MPPRESOURCEPARSERSAPI bool runParticleResourceTests(std::string* failure = nullptr);
}
