#include <stdexcept>
#include "mpp/ResourceStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/StaticLogger.h"

namespace mpp
{
	using namespace std;

	/*
	 * Constructor.
	 *
	 */
	ResourceStream::ResourceStream(ResourceManager* resourceMgr, string const& type)
		: ResourceWrangler("ResourceStream_" + type)
		, mType(type)
		, mLoaded(false)
		, mChildrenCreated(false)
		, mChildResourcesCreated(false)
		, mChildResourcesLoaded(false)
		, mResourceMgr(resourceMgr)
		, mwResource(nullptr)
	{
		//static_log_message(MPP_RESOURCE_LOGFILE, "Construct-stream " + getType() + ": " + (mwResource ? ("'" + mwResource->getName() + "'") : "(unattached)"));
	}

	/*
	 * Destructor.
	 *
	 */
	ResourceStream::~ResourceStream()
	{
		unload();
		//static_log_message(MPP_RESOURCE_LOGFILE, "Destruct-stream " + getType() + ": " + (mwResource ? ("'" + mwResource->getName() + "'") : "(unattached)"));
	}

	string const& ResourceStream::getType() const
	{
		return mType;
	}

	ResourceManager* ResourceStream::getResourceMgr()
	{
		return mResourceMgr;
	}

	void ResourceStream::createChildResourceStreams()
	{
		if (!mChildrenCreated)
		{
			createChildResourceStreamsImpl();
			mChildrenCreated = true;
		}
	}

	void ResourceStream::destroyChildResourceStreams()
	{
		if (mChildrenCreated)
		{
			mChildren.clear();
			mChildrenCreated = false;
		}
	}

	/*
	 * Load resource stream data.
	 *
	 */
	void ResourceStream::load()
	{
		createChildResourceStreams();

		if (!mLoaded)
		{
			for (auto& child: mChildren)
			{
				child.second->load();
			}

			//static_log_message(MPP_RESOURCE_LOGFILE, "Load-stream " + getType() + ": " + (mwResource ? ("'" + mwResource->getName() + "'") : "(unattached)"));
			loadImpl();
			mLoaded = true;
		}
	}

	/*
	* Unload resource stream data.
	*
	*/
	void ResourceStream::unload()
	{
		if (mLoaded)
		{
			//static_log_message(MPP_RESOURCE_LOGFILE, "Unload-stream " + getType() + ": " + (mwResource ? ("'" + mwResource->getName() + "'") : "(unattached)"));
			unloadImpl();
			mLoaded = false;

			for (auto& child: mChildren)
			{
				child.second->unload();
			}
		}

		destroyChildResourceStreams();
	}

	void ResourceStream::setFlags(uint32_t flags)
	{
		mFlags = flags;
	}

	uint32_t ResourceStream::getFlags() const
	{
		return mFlags;
	}

	void ResourceStream::addChild(string const& name, ResourceStreamPtr child)
	{
		mChildren[name] = child;
	}

	void ResourceStream::_markChildrenCreated()
	{
		mChildrenCreated = true;
	}

	map<string, ResourceStreamPtr> const& ResourceStream::getChildren() const
	{
		return mChildren;
	}

	// Child-resource realization through the resource manager: retained
	// reference behaviour now that the manager is delivered (renderer build;
	// the CPU-only build keeps an explicit declareResource failure).
	void ResourceStream::createChildResources(string const& parentName)
	{
		if (!mChildResourcesCreated)
		{
			for (auto child : mChildren)
			{
				string name = parentName + "/" + child.first;
				auto resStream = child.second;

				resStream->createChildResources(name);
				auto childRes = mResourceMgr->declareResource(name, resStream).first;
				childRes->acquire(this);
			}

			mChildResourcesCreated = true;
		}
	}

	void ResourceStream::destroyChildResources(string const& parentName)
	{
		if (mChildResourcesLoaded)
		{
			unloadChildResources(parentName);
		}

		if (mChildResourcesCreated)
		{
			for (auto child : mChildren)
			{
				string name = parentName + "/" + child.first;
				auto childRes = mResourceMgr->getResource(name);

				child.second->destroyChildResources(name);
			}

			mChildResourcesCreated = false;
		}
	}

	void ResourceStream::loadChildResources(string const& parentName)
	{
		if (!mChildResourcesCreated)
		{
			createChildResources(parentName);
		}

		if (!mChildResourcesLoaded)
		{
			for (auto child : mChildren)
			{
				string name = parentName + "/" + child.first;
				child.second->loadChildResources(name);

				auto res = mResourceMgr->getResource(name);
				res->load();
			}

			mChildResourcesLoaded = true;
		}
	}

	void ResourceStream::unloadChildResources(string const& parentName)
	{
		if (mChildResourcesLoaded)
		{
			for (auto child : mChildren)
			{
				string name = parentName + "/" + child.first;
				auto childRes = mResourceMgr->getResource(name);
				childRes->release(this);

				child.second->unloadChildResources(name);
			}

			mChildResourcesLoaded = false;
		}
	}
}
