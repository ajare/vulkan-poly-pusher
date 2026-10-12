#pragma once

#include <string>
#include <map>
#include <functional>

#include "Config.h"
#include "Serializer.h"

namespace mpp
{
	namespace resource_parsers
	{

		class _MPPRESOURCEPARSERSAPI FileStream
		{
			std::string mFilepath;

			mutable mpp::data::StructuredData mData;

			typedef std::function<Serializer*()> SerializerFactory;

			std::map<std::string, SerializerFactory> mFactories;

		protected:

			SerializerPtr getSerializer(std::string const& type) const;

			static std::string readTextFile(std::string const& filepath, std::string const& root);

		public:

			explicit FileStream(std::string const& filepath);

			FileStream(std::string const& filepath, mpp::data::StructuredData const& data);

			std::string const& getFilepath() const;

			mpp::data::StructuredData const& getStructuredData() const;
		};

	}
}