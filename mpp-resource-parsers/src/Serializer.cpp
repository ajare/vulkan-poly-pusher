#if defined(_MSC_VER) && _MSC_VER < 1930
#  include <vld.h> // Memory tracking
#endif

#include "mpp/resource-parsers/Serializer.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		Serializer::Serializer()
			: mData("")
		{
		}

		mpp::data::StructuredData const& Serializer::getData() const
		{
			return mData;
		}

	}
}