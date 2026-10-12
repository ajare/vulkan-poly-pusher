#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include "mpp/AntiAliasing.h"

namespace mpp::app
{
	// Parses only the optional [mpp] section. Other INI sections are ignored.
	// Unknown [mpp] keys, duplicate keys, malformed entries and invalid values
	// throw std::runtime_error with source/line context.
	RenderSystemOptions parseRenderSystemOptions(std::istream& input, std::string const& sourceName);
	RenderSystemOptions loadRenderSystemOptions(std::filesystem::path const& iniPath);
}
