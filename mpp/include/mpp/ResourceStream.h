#pragma once

#include <memory>
#include <string>
#include <map>

#include "mpp/Config.h"
#include "mpp/ResourceWrangler.h"

#define MPP_RESOURCE_LOGFILE "mpp-resources.log"

namespace mpp
{
	class ResourceManager;
	class Resource;

	class _MPPAPI ResourceStream : ResourceWrangler
	{
		friend class Resource;
		friend class ResourceStreamSerializer;

	private:

		std::string mType;
		
		bool mLoaded;

		uint32_t mFlags{ 0 };

		std::map<std::string, std::shared_ptr<ResourceStream>> mChildren;

		bool mChildrenCreated, mChildResourcesCreated, mChildResourcesLoaded;

		ResourceManager* mResourceMgr;

		Resource* mwResource;

	private:

		virtual void createChildResourceStreamsImpl() {};

		void createChildResourceStreams();

		void destroyChildResourceStreams();

	protected:

		virtual void loadImpl() = 0;

		virtual void unloadImpl() {}

		ResourceManager* getResourceMgr();

	public:

		ResourceStream(ResourceManager* resourceMgr, std::string const& type);

		virtual ~ResourceStream();

		void addChild(std::string const& name, std::shared_ptr<ResourceStream> child);

		void _markChildrenCreated();

		std::string const& getType() const;

		void load();

		void unload();

		void setFlags(uint32_t flags);

		uint32_t getFlags() const;

		std::map<std::string, std::shared_ptr<ResourceStream>> const& getChildren() const;

		void createChildResources(std::string const& parentName);

		void destroyChildResources(std::string const& parentName);

		void loadChildResources(std::string const& parentName);

		void unloadChildResources(std::string const& parentName);

		virtual void setFileBasePaths(std::string const& basepath)
		{
			MPP_UNUSED(basepath);
		}
	};

	typedef std::shared_ptr<ResourceStream> ResourceStreamPtr;
}