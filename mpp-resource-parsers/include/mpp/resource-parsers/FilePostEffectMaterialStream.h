#pragma once

#include <string>

#include "mpp/PostEffectMaterialStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{
		// XML authoring for PostEffectMaterial, following FilePbrMaterialStream's
		// pattern: <Program> references an existing engine/authored Program by
		// <Ref> (or embeds one via <Resource>), <SamplerSlots> declares the named
		// sampler inputs the post-effect chain binds by name, and <Uniforms>
		// authors default tunable parameter values (threshold, intensity, ...).
		class _MPPRESOURCEPARSERSAPI FilePostEffectMaterialStream : public mpp::PostEffectMaterialStream, public FileStream
		{
			void createChildResourceStreamsImpl();

		protected:
			void loadImpl();

		public:
			FilePostEffectMaterialStream(ResourceManager* resourceMgr, std::string const& filepath);
			FilePostEffectMaterialStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data);

			static std::pair<std::string, PostEffectMaterialSpecification> parseDefinition(mpp::data::StructuredData const& data, std::string const& filepath);
		};
	}
}
