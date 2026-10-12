#pragma once

#include <map>
#include <string>

#include "mpp/StringStream.h"
#include "mpp/ResourceManager.h"

#include "Config.h"
#include "FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FileStringStream : public mpp::StringStream, public FileStream
		{
			void loadImpl();

			void parseDefinition(mpp::data::StructuredData const& data);

		public:

			FileStringStream(ResourceManager* resourceMgr, std::string const& filepath);

			FileStringStream(ResourceManager* resourceMgr, std::string const& filepath, mpp::data::StructuredData const& data);
		};

	}
}