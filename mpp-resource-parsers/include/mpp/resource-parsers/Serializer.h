#pragma once

#include <string>
#include <memory>

#include "mpp/data/StructuredData.h"

#include "Config.h"

namespace mpp
{
	namespace resource_parsers
	{

		class Serializer
		{
		protected:

			mpp::data::StructuredData mData;

		public:

			Serializer();

			virtual void loadFromFile(std::string const& filepath) = 0;

			mpp::data::StructuredData const& getData() const;
		};

		typedef std::shared_ptr<Serializer> SerializerPtr;

	}
}