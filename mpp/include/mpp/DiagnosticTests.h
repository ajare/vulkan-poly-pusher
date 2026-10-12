#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	// CPU-only structured diagnostic contract tests.
	_MPPAPI bool runDiagnosticTests(std::string* failure = nullptr);
}
