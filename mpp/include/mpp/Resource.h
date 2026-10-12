#pragma once

#include <string>
#include <set>
#include <cstdint>
#include <memory>

#include "mpp/Config.h"
#include "mpp/ResourceStream.h"
#include "mpp/ResourceWrangler.h"

namespace mpp
{
	class RenderSystem;
	class ResourceManager;
	
	class _MPPAPI Resource : public ResourceWrangler
	{
		std::string mName;

		std::string mType;

		int mRefCount;

		std::set<std::string> mTags;

		bool mCreated;

		bool mLoaded;
		uint64_t mLifecycleRevision{ 1 };

		uint32_t mId;

		RenderSystem* mwRenderSystem;

		ResourceManager* mwResourceMgr;

		ResourceStreamPtr mResourceStream;

		std::set<std::shared_ptr<Resource>> mDependentResources;

		std::set<ResourceWrangler*> mDependingResources;

	protected:

		virtual void createImpl() = 0;

		virtual void destroyImpl() = 0;

		virtual void loadImpl() = 0;

		virtual void unloadImpl() = 0;

		void setId(uint32_t id);

		void acquireDependentResource(std::shared_ptr<Resource> resource);

		void releaseDependentResources();

	public:

		Resource(std::string const& name, std::string const& type, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		virtual ~Resource();

		std::string const& getName() const;

		virtual std::string const& getType() const;

		bool isCreated() const;

		bool isLoaded() const;

		uint64_t getLifecycleRevision() const;

		bool isReferenced() const;
		
		uint32_t getId() const;

		virtual int getIdCount() const;

		virtual int getLiveIdCount() const;

		int getRefCount() const;

		int getDependentResourceCount() const;

		int getDependingObjectCount() const;

		std::set<std::shared_ptr<Resource>> const& getDependentResources() const;

		std::set<ResourceWrangler*> const& getDependingResources() const;

		RenderSystem* getRenderSystem();

		ResourceManager* getResourceManager();

		ResourceStreamPtr getResourceStream();

		void addTag(std::string const& tag);

		void removeTag(std::string const& tag);

		bool hasTag(std::string const& tag);

		void create();

		void destroy();

		void load(bool unloadStreamsAfterwards = false);

		void unload();

		void acquire(ResourceWrangler* acquirer);

		void release(ResourceWrangler* releaser);
	};

	typedef std::shared_ptr<Resource> ResourcePtr;
}