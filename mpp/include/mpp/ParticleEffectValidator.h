#pragma once

#include <string>

#include "mpp/Diagnostic.h"
#include "mpp/ParticleEffectSpecification.h"

namespace mpp
{
	// Context-free semantic validation for authored particle effects. This path
	// operates only on in-memory authoring data and does not create live emitters
	// or GPU resources.
	class _MPPAPI ParticleEffectValidator
	{
	public:
		static DiagnosticBag validate(ParticleEffectSpecification const& specification, std::string const& sourceName = {});
	};
}
