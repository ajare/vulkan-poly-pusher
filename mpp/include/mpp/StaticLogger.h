#pragma once

#include "mpp/Config.h"

namespace mpp
{
	void _MPPAPI enable_static_log(std::string const& file, bool enabled);

	bool _MPPAPI is_static_log_enabled(std::string const& file);

	void static_log_message(std::string const& file, std::string const& message);
}