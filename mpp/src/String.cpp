#include "mpp/String.h"
#include "mpp/StringStream.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	/*
	* Constructor.
	*
	*/
	String::String(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "String", renderSystem, resourceMgr, resourceStream)
	{
	}

	/*
	 * Destructor
	 *
	 */
	String::~String()
	{
		destroy();
	}

	/*
	 * Create string.
	 *
	 */
	void String::loadImpl()
	{
		StringStream* sStr = dynamic_cast<StringStream*>(getResourceStream().get());
		if (!sStr)
		{
			THROW_MPP("Could not cast to type 'StringStream'", __LINE__, __FILE__, __func__);
		}

		mData = sStr->getString();
	}

	/*
	 * Destroy string.
	 *
	 */
	void String::unloadImpl()
	{
		mData.clear();
	}

	/*
	 * Get data.
	 *
	 */
	string const& String::getData()
	{
		load();
		return mData;
	}

}