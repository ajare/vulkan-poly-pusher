#include "utils/YamlReader.h"

#include "mpp/resource-parsers/YamlSerializer.h"
#include "mpp/resource-parsers/YamlWrapperCollapseTable.h"
#include "StructuredDataAdapter.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		YamlSerializer::YamlSerializer()
		{
		}

		void YamlSerializer::loadFromFile(string const& filepath)
		{
			auto reader = utils::YamlReader::fromFile(filepath, yamlWrapperCollapseTable());
			mData = detail::importStructuredData(reader->readTree());

			delete reader;
		}
	}
}
