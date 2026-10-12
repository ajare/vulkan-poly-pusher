#include "utils/XmlWriter.h"
#include "utils/XmlReader.h"

#include "mpp/resource-parsers/XmlSerializer.h"
#include "StructuredDataAdapter.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		XmlSerializer::XmlSerializer()
		{
		}

		void XmlSerializer::loadFromFile(string const& filepath)
		{
			// Open file and read in data
			auto reader = utils::XmlReader::fromFile(filepath);
			mData = detail::importStructuredData(reader->readTree());

			delete reader;
		}
	}
}
