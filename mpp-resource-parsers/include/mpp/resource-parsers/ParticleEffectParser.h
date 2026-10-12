#pragma once

#include <string>

#include "Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/ParticleEffectSpecification.h"

namespace mpp::data { class StructuredData; }

namespace mpp::resource_parsers
{
	struct _MPPRESOURCEPARSERSAPI ParticleEffectParseResult
	{
		ParticleEffectSpecification specification;
		DiagnosticBag diagnostics;
		bool succeeded() const { return !diagnostics.hasErrors(); }
	};

	// Context-free parser for *.particle.yaml. All authoring failures are returned
	// as diagnostics; callers never need an exception boundary for bad documents.
	class _MPPRESOURCEPARSERSAPI ParticleEffectParser
	{
	public:
		static ParticleEffectParseResult fromFile(std::string const& filepath) noexcept;
		static ParticleEffectParseResult fromData(mpp::data::StructuredData const& data, std::string const& sourceName = {}) noexcept;
	};
}
