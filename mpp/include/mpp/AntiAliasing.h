#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace mpp
{
	enum class AntiAliasingSamples : uint8_t
	{
		Off,
		X2,
		X4,
		X8
	};

	inline uint32_t antiAliasingSampleCount(AntiAliasingSamples value)
	{
		switch (value)
		{
		case AntiAliasingSamples::Off: return 1;
		case AntiAliasingSamples::X2: return 2;
		case AntiAliasingSamples::X4: return 4;
		case AntiAliasingSamples::X8: return 8;
		}
		throw std::invalid_argument("Unknown anti-aliasing sample setting.");
	}

	inline float ssaaLinearScale(AntiAliasingSamples value)
	{
		return std::sqrt(static_cast<float>(antiAliasingSampleCount(value)));
	}

	inline uint32_t ssaaDimension(uint32_t logicalSize, AntiAliasingSamples value)
	{
		if(logicalSize==0)return 0;
		double scale=1.0;switch(value){case AntiAliasingSamples::Off:break;case AntiAliasingSamples::X2:scale=1.4142135623730951;break;case AntiAliasingSamples::X4:scale=2.0;break;case AntiAliasingSamples::X8:scale=2.8284271247461903;break;default:throw std::invalid_argument("Unknown SSAA sample setting.");}
		return static_cast<uint32_t>(std::ceil(static_cast<double>(logicalSize)*scale));
	}

	inline std::string antiAliasingSamplesName(AntiAliasingSamples value)
	{
		switch (value)
		{
		case AntiAliasingSamples::Off: return "off";
		case AntiAliasingSamples::X2: return "2x";
		case AntiAliasingSamples::X4: return "4x";
		case AntiAliasingSamples::X8: return "8x";
		}
		throw std::invalid_argument("Unknown anti-aliasing sample setting.");
	}

	struct AntiAliasingDefaults
	{
		AntiAliasingSamples msaa{ AntiAliasingSamples::Off };
		AntiAliasingSamples ssaa{ AntiAliasingSamples::Off };
		bool taa{ false };
		bool fxaa{ false };
	};

	struct AntiAliasingOverrides
	{
		std::optional<AntiAliasingSamples> msaa;
		std::optional<AntiAliasingSamples> ssaa;
		std::optional<bool> taa;
		std::optional<bool> fxaa;
	};

	inline AntiAliasingDefaults resolveAntiAliasing(
		AntiAliasingDefaults const& defaults,
		AntiAliasingOverrides const& overrides)
	{
		return {
			overrides.msaa.value_or(defaults.msaa),
			overrides.ssaa.value_or(defaults.ssaa),
			overrides.taa.value_or(defaults.taa),
			overrides.fxaa.value_or(defaults.fxaa)
		};
	}

	inline constexpr uint32_t MinimumParticlePoolCapacity = 262144;
	inline constexpr uint32_t MaximumParticlePoolCapacity = 1048576;
	inline constexpr uint32_t DefaultParticlePoolCapacity = 262144;

	struct RenderSystemOptions
	{
		AntiAliasingDefaults antiAliasing;
		uint32_t particlePoolCapacity{ DefaultParticlePoolCapacity };
	};
}
