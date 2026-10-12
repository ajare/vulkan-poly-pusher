#pragma once

#include <map>
#include <string>

#include "mpp/SamplerStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FileSamplerStream : public mpp::SamplerStream, public FileStream
		{
			std::map<std::string, uint32_t> mMinFilters, mMagFilters, mWrapping;

		private:

			void setup();

			void loadImpl();

			uint32_t parseMinFilter(std::string const& value);

			uint32_t parseMagFilter(std::string const& value);

			uint32_t parseWrapping(std::string const& value);

			void parseDefinition(mpp::data::StructuredData const& data);

		public:

			FileSamplerStream(ResourceManager* resourceMgr, std::string const& filepath);

			FileSamplerStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data);
		};

	}
}