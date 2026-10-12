#include <format>
#include "utils/StringUtils.h"

#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/Resource.h"
#include "mpp/StaticLogger.h"

using namespace std;

namespace mpp
{
	/*
	 * Constructor.
	 *
	 */
	Resource::Resource(string const& name, string const& type, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: ResourceWrangler(name)
		, mName(name)
		, mType(type)
		, mRefCount(0)
		, mCreated(false)
		, mLoaded(false)
		, mId(0)
		, mwRenderSystem(renderSystem)
		, mwResourceMgr(resourceMgr)
		, mResourceStream(resourceStream)
	{
		//mResourceStream->mwResource = this;
		//static_log_message(MPP_RESOURCE_LOGFILE, "Construct " + getType() + ": '" + getName() + "'");
	}

	Resource::~Resource()
	{
		//static_log_message(MPP_RESOURCE_LOGFILE, "Destruct " + getType() + ": '" + getName() + "'");
	}

	/*
	 * Set resource id.
	 *
	 */
	void Resource::setId(uint32_t id)
	{
		mId = id;
	}
	
	/*
	* Get the resource id.
	*
	*/
	uint32_t Resource::getId() const
	{
		return mId;
	}

	/*
	 * Get resource name.
	 *
	 */
	string const& Resource::getName() const
	{
		return mName;
	}

	/*
	 * Get resource type.
	 *
	 */
	string const& Resource::getType() const
	{
		return mType;
	}

	/*
	 * Is this resource referenced?
	 *
	 */
	bool Resource::isReferenced() const
	{
		return mRefCount > 0;
	}

	/*
	 * How many GL names does this resource manage?
	 *
	 */
	int Resource::getIdCount() const
	{
		return 0;
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Resource::getLiveIdCount() const
	{
		return 0;
	}

	/*
	 * Get number of references to this resource
	 *
	 */
	int Resource::getRefCount() const
	{
		return mRefCount;
	}

	/*
	 * Get number of objects still acquiring this resource.
	 *
	 */
	int Resource::getDependentResourceCount() const
	{
		return (int)mDependentResources.size();
	}

	/*
	 * Get number of objects still acquiring this resource.
	 *
	 */
	int Resource::getDependingObjectCount() const
	{
		return (int)mDependingResources.size();
	}

	set<shared_ptr<Resource>> const& Resource::getDependentResources() const
	{
		return mDependentResources;
	}

	set<ResourceWrangler*> const& Resource::getDependingResources() const
	{
		return mDependingResources;
	}


	/*
	 * Has the resource been created?
	 *
	 */
	bool Resource::isCreated() const
	{
		return mCreated;
	}

	/*
	 * Has the resource been loaded?
	 *
	 */
	bool Resource::isLoaded() const
	{
		return mLoaded;
	}

	uint64_t Resource::getLifecycleRevision() const
	{
		return mLifecycleRevision;
	}

	/*
	 * Get the associated RenderSystem.
	 *
	 */
	RenderSystem* Resource::getRenderSystem()
	{
		return mwRenderSystem;
	}

	/*
	 * Get Resource Manager.
	 *
	 */
	ResourceManager* Resource::getResourceManager()
	{
		return mwResourceMgr;
	}

	/*
	 * Get the resource stream used to create this resource.
	 *
	 */
	ResourceStreamPtr Resource::getResourceStream()
	{
		return mResourceStream;
	}

	/*
	 * Add a tag to the resource.
	 *
	 */
	void Resource::addTag(string const& tag)
	{
		mTags.insert(tag);
	}

	/*
	 * Remove a tag from the resource.
	 *
	 */
	void Resource::removeTag(string const& tag)
	{
		mTags.erase(tag);
	}

	/*
	 * Does the resource have a particular tag?
	 *
	 */
	bool Resource::hasTag(string const& tag)
	{
		return mTags.find(tag) != mTags.end();
	}

	/*
	 * Create the resource.
	 *
	 */
	void Resource::create()
	{
		// Create child resources
		if (mResourceStream)
		{
			mResourceStream->createChildResources(getName());
		}

		if (!isCreated())
		{
			//static_log_message(MPP_RESOURCE_LOGFILE, "Create " + getType () + ": '" + getName() + "'");
			try{createImpl();mCreated=true;++mLifecycleRevision;}
			catch(...){releaseDependentResources();if(mResourceStream)mResourceStream->destroyChildResources(getName());throw;}
		}
	}

	/*
	 * Destroy the resource.
	 *
	 */
	void Resource::destroy()
	{
		if (isLoaded())
		{
			unload();
		}

		if (isCreated())
		{
			//static_log_message(MPP_RESOURCE_LOGFILE, "Destroy " + getType() + ": '" + getName() + "'");
			destroyImpl();
			releaseDependentResources();

			// Destroy child resources
			if (mResourceStream)
			{
				mResourceStream->destroyChildResources(getName());
			}

			mCreated = false;
			++mLifecycleRevision;
		}

	}

	/*
 	 * Load the resource.
 	 *
 	 */
	void Resource::load(bool unloadStreamsAfterwards)
	{
		if (!isCreated())
		{
			create();
		}

		// Load child resources
		if (mResourceStream)
		{
			mResourceStream->loadChildResources(getName());
		}

		if (!isLoaded())
		{
			//static_log_message(MPP_RESOURCE_LOGFILE, "Load " + getType() + ": '" + getName() + "'");
			loadImpl();
			mLoaded = true;
			++mLifecycleRevision;

			if (mResourceStream && unloadStreamsAfterwards)
			{
				mResourceStream->unload();
			}
		}
	}

	/*
	 * Unload the resource. 
	 *
	 */
	void Resource::unload()
	{
		if (isLoaded())
		{
			//static_log_message(MPP_RESOURCE_LOGFILE, "Unload " + getType() + ": '" + getName() + "'");
			unloadImpl();

			// Unload child resources
			if (mResourceStream)
			{
				mResourceStream->unloadChildResources(getName());
			}

			mLoaded = false;
			++mLifecycleRevision;
		}
	}

	void Resource::acquire(ResourceWrangler* acquirer)
	{
		auto it = mDependingResources.insert(acquirer);
		if (it.second)
		{
			mRefCount++;

			string msg = std::format("Object '{}' acquired '{}' for {} references.", acquirer->getWranglerName(), getName(), mRefCount);
			static_log_message(MPP_RESOURCE_LOGFILE, msg);
		}
		else
		{
			string errMsg = std::format("Object {} tried to acquire resource {} more than once.", acquirer->getWranglerName(), getName());
			throw MppException(errMsg);
		}
	}

	void Resource::release(ResourceWrangler* releaser)
	{
		auto it = mDependingResources.find(releaser);
		if (it != mDependingResources.end())
		{
			mRefCount--;
			assert(mRefCount >= 0 && "Resource ref-count dropped below zero.");

			mDependingResources.erase(it);

			string msg = std::format("Object '{}' released {} to {} references.", releaser->getWranglerName(), getName(), mRefCount);
			static_log_message(MPP_RESOURCE_LOGFILE, msg);

			if (mRefCount == 0)
			{
				releaseDependentResources();
			}
		}
		else
		{
			throw MppException("Object '" + releaser->getWranglerName() + "' tried to release resource '" + getName() + "' without acquiring it.");
		}
	}

	void Resource::acquireDependentResource(ResourcePtr resource)
	{
		auto inserted = mDependentResources.insert(resource);
		if (inserted.second)
		{
			resource->acquire(this);
		}
	}

	void Resource::releaseDependentResources()
	{
		for (auto res : mDependentResources)
		{
			res->release(this);
		}

		mDependentResources.clear();
	}

}