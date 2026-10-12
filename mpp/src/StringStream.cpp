#include "mpp/StringStream.h"

using namespace std;

namespace mpp
{
	StringStream::StringStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "String")
	{
	}

	string const& StringStream::getString() const { return mData; }
}
