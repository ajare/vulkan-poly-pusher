#include <cassert>

#include "mpp/Config.h"
#include "mpp/ResourceWrangler.h"

using namespace std;

namespace mpp
{
	using namespace std;

	/*
	 * Constructor.
	 *
	 */
	ResourceWrangler::ResourceWrangler(string const& name)
		: mWranglerName(name)
	{
	}

	string const& ResourceWrangler::getWranglerName() const
	{
		return mWranglerName;
	}

}