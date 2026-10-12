#pragma once

#include "Config.h"
#include "FileStream.h"
#include "mpp/Diagnostic.h"
#include "mpp/ParticleEffectStream.h"

namespace mpp::resource_parsers
{
	class _MPPRESOURCEPARSERSAPI FileParticleEffectStream : public ParticleEffectStream, public FileStream
	{
		DiagnosticBag mDiagnostics;
		void loadImpl() override;

	public:
		FileParticleEffectStream(ResourceManager* resourceManager, std::string const& filepath);
		FileParticleEffectStream(ResourceManager* resourceManager, std::string const& filepath, mpp::data::StructuredData const& data);
		DiagnosticBag const& getDiagnostics() const { return mDiagnostics; }
	};
}
