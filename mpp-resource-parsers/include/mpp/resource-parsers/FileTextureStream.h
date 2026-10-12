#pragma once

#include <map>
#include <string>

#include "mpp/TextureStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FileTextureStream : public mpp::TextureStream, public FileStream
		{
			bool mRelativisePaths;

		private:

			void setup();

			void loadImpl();

			static uint32_t parseInternalFormat(std::string const& value, std::string const& filepath);

			static uint32_t parseMinFilter(std::string const& value, std::string const& filepath);

			static uint32_t parseMagFilter(std::string const& value, std::string const& filepath);

			static uint32_t parseWrapping(std::string const& value, std::string const& filepath);

			static uint32_t parseTarget(std::string const& value, std::string const& filepath);

		public:

			FileTextureStream(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);

			FileTextureStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths = true);
		
			static std::pair<std::string, Definition> parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths = true);
		};

	}
}