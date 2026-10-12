#pragma once

#include <string>

#include "Config.h"
#include "Serializer.h"

namespace mpp
{
	namespace resource_parsers
	{

		class XmlSerializer : public Serializer
		{
		public:

			XmlSerializer();

			void loadFromFile(std::string const& filepath);
		};

	}
}