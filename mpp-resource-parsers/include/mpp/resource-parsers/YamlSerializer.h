#pragma once

#include <string>

#include "Config.h"
#include "Serializer.h"

namespace mpp
{
	namespace resource_parsers
	{

		class YamlSerializer : public Serializer
		{
		public:

			YamlSerializer();

			void loadFromFile(std::string const& filepath);
		};

	}
}
