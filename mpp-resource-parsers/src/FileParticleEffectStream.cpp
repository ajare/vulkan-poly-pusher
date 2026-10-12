#include <exception>

#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/ParticleEffectParser.h"

namespace mpp::resource_parsers
{
	FileParticleEffectStream::FileParticleEffectStream(ResourceManager* resourceManager, std::string const& filepath)
		: ParticleEffectStream(resourceManager), FileStream(filepath)
	{
	}

	FileParticleEffectStream::FileParticleEffectStream(ResourceManager* resourceManager, std::string const& filepath, mpp::data::StructuredData const& data)
		: ParticleEffectStream(resourceManager), FileStream(filepath, data)
	{
	}

	void FileParticleEffectStream::loadImpl()
	{
		ParticleEffectParseResult result;
		try { result = ParticleEffectParser::fromData(getStructuredData(), getFilepath()); }
		catch (std::exception const& error)
		{
			result.diagnostics.error("MPP-PARTICLE-000", "Could not read particle effect: " + std::string(error.what()), { getFilepath(), "/" });
		}
		catch (...) { result.diagnostics.error("MPP-PARTICLE-000", "Could not read particle effect.", { getFilepath(), "/" }); }
		mDiagnostics = result.diagnostics;
		mSpecification = std::move(result.specification);
		rebuildEmitterTemplates();
	}
}
