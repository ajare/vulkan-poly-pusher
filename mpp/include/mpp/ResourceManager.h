#pragma once

#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <iterator>
#include <cctype>
#include <locale>
#include <functional>

#include "mpp/Config.h"
#include "mpp/Resource.h"
#include "mpp/ResourceStream.h"
#include "mpp/ResourceWrangler.h"
#include "mpp/RenderSystem.h"
#include "mpp/MeshSortFlags.h"
#include "mpp/MppException.h"
#include "mpp/ProgramStream.h"
#include "mpp/TextureStream.h"
#include "mpp/Logger.h"

#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{

	typedef std::function<ResourcePtr(std::string const&, ResourceStreamPtr)> ResourceFactory;

	class _MPPAPI ResourceManager : public ResourceWrangler
	{
		std::map<std::string, ResourceFactory> mResourceFactories;

		std::map<std::string, ResourcePtr> mResources;
		// Requested child names which reuse a cached resource with another owner name.
		std::map<std::string, ResourcePtr> mResourceAliases;

		std::map<std::string, ResourcePtr> mProgramCache;

		RenderSystem* mwRenderSystem;

		// Sort IDs are local to the RenderSystem/ResourceManager which resolves
		// them. Slots are never shifted or reused, so queued render keys remain
		// stable when an unrelated resource is removed.
		uint32_t mNextSortableTextureId{ 1 };

		uint32_t mNextSortableProgramId{ 1 };

		std::vector<ResourcePtr> mSortableTextures;

		std::vector<ResourcePtr> mSortablePrograms;

		ImageLoadFunction mImageLoadFunction;

		bool mLogResourceEvents{ false };

		size_t mProgramIdCounter{ 0 };

		Logger* mLogger;

	private:

		bool validateForRemoval(ResourcePtr resource);

		void insertResource(ResourcePtr resource, ResourceStreamPtr resourceStream);

		void removeResource(ResourcePtr resource);

	public:

		ResourceManager(RenderSystem* renderSystem, Logger* logger);

		~ResourceManager();

		void setImageLoadFunction(ImageLoadFunction function);

		ImageLoadFunction getImageLoadFunction();

		void deleteResource(std::string const& name);

		void createAllResources();

		void loadAllResources();

		void unloadAllUnreferencedResources();

		void destroyAllUnreferencedResources();

		std::pair<ResourcePtr, bool> declareResource(std::string const& name, ResourceStreamPtr resourceStream, bool loadStream = true);

		ResourcePtr acquireResource(ResourceWrangler* wrangler, std::string const& name);

		ResourcePtr getResource(std::string const& name, bool nullIfNotFound = false);
		std::vector<std::string> getResourceNamesWithPrefix(std::string const& prefix) const;
		bool isResourceAlias(std::string const& name) const;
		void deleteResourceTree(std::string const& rootName);

		ResourcePtr getDefault2dProgram(mesh::MeshSpecification const& spec, uint32_t flags, bool load, std::string descriptor = "");

		ResourcePtr getDefault2dProgram(std::string const& defaultVertexShader, std::string const& defaultFragmentShader, mesh::MeshSpecification const& spec, uint32_t flags, bool load, std::string descriptor = "");

		ResourcePtr getDefault3dProgram(mesh::MeshSpecification const& spec, uint32_t flags, bool load, std::string descriptor = "");

		ResourcePtr getDefault3dProgram(std::string const& defaultVertexShader, std::string const& defaultFragmentShader, mesh::MeshSpecification const& spec, uint32_t flags, bool load, std::string descriptor = "");

		std::vector<ResourcePtr> getAllReferencedResources() const;

		std::vector<ResourcePtr> getAllUnreferencedResources() const;

		void getResourceCounts(uint32_t& numResources, uint32_t& numDeclared, uint32_t& numCreated, uint32_t& numLoaded) const;

		ResourcePtr getTextureBySortId(uint32_t id);

		ResourcePtr getProgramBySortId(uint32_t id);

		std::set<std::string> getProgramAttributes(mesh::MeshSpecification const& spec, uint32_t flags) const;

		void dumpResources(std::string const& filepath);

		void debugMessage(std::string const& message);

		void infoMessage(std::string const& message);

		void warnMessage(std::string const& message);

		void errorMessage(std::string const& message);
	};

}
