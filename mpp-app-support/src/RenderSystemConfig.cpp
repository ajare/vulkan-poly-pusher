#include "mpp/app/RenderSystemConfig.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mpp::app
{
	namespace
	{
		std::string trim(std::string value)
		{
			auto first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) return {};
			auto last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		std::string lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}

		AntiAliasingSamples parseSamples(std::string const& value, std::string const& location)
		{
			auto normalised = lower(trim(value));
			if (normalised == "off") return AntiAliasingSamples::Off;
			if (normalised == "2x") return AntiAliasingSamples::X2;
			if (normalised == "4x") return AntiAliasingSamples::X4;
			if (normalised == "8x") return AntiAliasingSamples::X8;
			throw std::runtime_error(location + ": expected off, 2x, 4x, or 8x, but found '" + trim(value) + "'.");
		}

		bool parseBool(std::string const& value, std::string const& location)
		{
			auto normalised = lower(trim(value));
			if (normalised == "true") return true;
			if (normalised == "false") return false;
			throw std::runtime_error(location + ": expected true or false, but found '" + trim(value) + "'.");
		}

		uint32_t parseParticlePoolCapacity(std::string const& value, std::string const& location)
		{
			auto normalised = trim(value);
			uint32_t capacity = 0;
			auto result = std::from_chars(normalised.data(), normalised.data() + normalised.size(), capacity);
			if (normalised.empty() || result.ec != std::errc{} || result.ptr != normalised.data() + normalised.size() ||
				capacity < MinimumParticlePoolCapacity || capacity > MaximumParticlePoolCapacity)
			{
				throw std::runtime_error(location + ": expected an integer from " +
					std::to_string(MinimumParticlePoolCapacity) + " to " + std::to_string(MaximumParticlePoolCapacity) +
					", but found '" + normalised + "'.");
			}
			return capacity;
		}
	}

	RenderSystemOptions parseRenderSystemOptions(std::istream& input, std::string const& sourceName)
	{
		RenderSystemOptions options;
		std::string section;
		std::set<std::string> encountered;
		std::string line;
		for (size_t lineNumber = 1; std::getline(input, line); ++lineNumber)
		{
			auto content = trim(line);
			if (content.empty() || content.front() == ';' || content.front() == '#') continue;
			if (content.front() == '[' && content.back() == ']')
			{
				section = lower(trim(content.substr(1, content.size() - 2)));
				continue;
			}
			if (section != "mpp") continue;

			auto comment = content.find_first_of(";#");
			if (comment != std::string::npos) content = trim(content.substr(0, comment));
			auto separator = content.find('=');
			auto location = sourceName + ":" + std::to_string(lineNumber) + " [mpp]";
			if (separator == std::string::npos)
			{
				throw std::runtime_error(location + ": expected key=value.");
			}
			auto key = lower(trim(content.substr(0, separator)));
			auto value = trim(content.substr(separator + 1));
			if (key.empty()) throw std::runtime_error(location + ": setting name is empty.");
			if (!encountered.insert(key).second) throw std::runtime_error(location + ": duplicate setting '" + key + "'.");

			if (key == "msaa") options.antiAliasing.msaa = parseSamples(value, location + " msaa");
			else if (key == "ssaa") options.antiAliasing.ssaa = parseSamples(value, location + " ssaa");
			else if (key == "taa") options.antiAliasing.taa = parseBool(value, location + " taa");
			else if (key == "fxaa") options.antiAliasing.fxaa = parseBool(value, location + " fxaa");
			else if (key == "particlepoolcapacity") options.particlePoolCapacity = parseParticlePoolCapacity(value, location + " particlePoolCapacity");
			else throw std::runtime_error(location + ": unknown setting '" + key + "'. Expected msaa, ssaa, taa, fxaa, or particlePoolCapacity.");
		}
		return options;
	}

	RenderSystemOptions loadRenderSystemOptions(std::filesystem::path const& iniPath)
	{
		std::ifstream input(iniPath);
		if (!input) throw std::runtime_error("Could not open MassivePolyPusher configuration file '" + iniPath.string() + "'.");
		return parseRenderSystemOptions(input, iniPath.string());
	}
}
