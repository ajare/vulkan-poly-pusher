#pragma once

#include <string>

#include "mpp/resource-parsers/Config.h"

namespace mpp::resource_parsers
{
	// Context-free XML parse/serialize regression suite for CI/test hosts.
	_MPPRESOURCEPARSERSAPI bool runRenderGraphResourceTests(std::string* failure = nullptr);
}
