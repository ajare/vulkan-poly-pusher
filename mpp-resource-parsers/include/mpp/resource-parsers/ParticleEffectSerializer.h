#pragma once

#include <string>

#include "Config.h"
#include "mpp/ParticleEffectSpecification.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI ParticleEffectSerializer
	{
	public:
		static void toFile(ParticleEffectSpecification const& specification, std::string const& filepath);
	};
}
