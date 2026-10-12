#include "mpp/app/RenderSystemConfigTests.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include "mpp/app/RenderSystemConfig.h"

namespace mpp::app
{
	bool runRenderSystemConfigTests(std::string* failure)
	{
		try
		{
			{
				std::istringstream input("[Editor]\nvalue=ignored\n");
				auto options = parseRenderSystemOptions(input, "defaults.ini");
				if (options.antiAliasing.msaa != AntiAliasingSamples::Off ||
					options.antiAliasing.ssaa != AntiAliasingSamples::Off ||
					options.antiAliasing.taa || options.antiAliasing.fxaa ||
					options.particlePoolCapacity != DefaultParticlePoolCapacity)
					throw std::runtime_error("Missing [mpp] section did not produce the render-system defaults.");
			}
			{
				std::istringstream input(
					"[ MPP ]\n"
					" MSAA = 2X ; comment\n"
					"ssaa= 8x\n"
					"TAA = TrUe\n"
					"FxAa = FALSE # comment\n"
					"particlePoolCapacity = 524288\n");
				auto options = parseRenderSystemOptions(input, "valid.ini");
				if (options.antiAliasing.msaa != AntiAliasingSamples::X2 ||
					options.antiAliasing.ssaa != AntiAliasingSamples::X8 ||
					!options.antiAliasing.taa || options.antiAliasing.fxaa ||
					options.particlePoolCapacity != 524288)
					throw std::runtime_error("Valid case-insensitive [mpp] settings were parsed incorrectly.");
				if (std::abs(ssaaLinearScale(options.antiAliasing.ssaa) - std::sqrt(8.0f)) > 0.0001f || ssaaDimension(64,AntiAliasingSamples::X2)!=91 || ssaaDimension(64,AntiAliasingSamples::X4)!=128 || ssaaDimension(64,AntiAliasingSamples::X8)!=182)
					throw std::runtime_error("SSAA total-sample dimension scaling/rounding is incorrect.");
			}
			{
				AntiAliasingDefaults defaults{ AntiAliasingSamples::X4, AntiAliasingSamples::X2, true, true };
				AntiAliasingOverrides overrides;overrides.msaa=AntiAliasingSamples::Off;overrides.fxaa=false;
				auto effective=resolveAntiAliasing(defaults,overrides);
				if(effective.msaa!=AntiAliasingSamples::Off||effective.ssaa!=AntiAliasingSamples::X2||!effective.taa||effective.fxaa)
					throw std::runtime_error("Anti-aliasing override inheritance failed.");
			}

			auto expectFailure = [](std::string text, std::string const& expected)
			{
				std::istringstream input(std::move(text));
				try { parseRenderSystemOptions(input, "invalid.ini"); }
				catch (std::exception const& error)
				{
					if (std::string(error.what()).find(expected) != std::string::npos) return;
					throw std::runtime_error("Configuration error did not contain '" + expected + "': " + error.what());
				}
				throw std::runtime_error("Invalid configuration was accepted; expected '" + expected + "'.");
			};
			expectFailure("[mpp]\nmsaa=16x\n", "off, 2x, 4x, or 8x");
			expectFailure("[mpp]\ntaa=yes\n", "true or false");
			expectFailure("[mpp]\nquality=high\n", "unknown setting 'quality'");
			expectFailure("[mpp]\nparticlePoolCapacity=262143\n", "262144 to 1048576");
			expectFailure("[mpp]\nparticlePoolCapacity=1048577\n", "262144 to 1048576");
			expectFailure("[mpp]\nparticlePoolCapacity=many\n", "expected an integer");
			expectFailure("[mpp]\nfxaa=true\nFXAA=false\n", "duplicate setting 'fxaa'");
			expectFailure("[mpp]\nnot-an-entry\n", "expected key=value");

			if (failure) failure->clear();
			return true;
		}
		catch (std::exception const& error)
		{
			if (failure) *failure = error.what();
			return false;
		}
	}
}
