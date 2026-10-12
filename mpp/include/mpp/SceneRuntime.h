#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "mpp/Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/Resource.h"
#include "mpp/Scene.h"
#include "mpp/SceneDocument.h"
#include "mpp/PbrLight.h"
#include "mpp/ParticleSystem.h"

namespace mpp
{
	// Transactionally instantiates a SceneDocument. Failed candidates leave the
	// previous scene active; missing models become diagnosed placeholder boxes.
	class _MPPAPI SceneRuntime
	{
		RenderSystem* mRenderSystem;
		ResourceManager* mResourceManager;
		ScenePtr mScene;
		std::vector<std::string> mResourceNames;
		uint64_t mGeneration{0};
		DiagnosticBag mDiagnostics;
		std::map<std::string,uint64_t> mModelTriangles;
		std::map<std::string,SceneModel3dPtr> mModelInstances;
		std::map<std::string,ParticleEffectHandle> mParticleEffects;
		std::vector<PbrLight> mLights;
		std::string mEnvironmentBinding;
		uint64_t mUniqueTriangles{0};
		void clearResources(ScenePtr& scene,std::vector<std::string>& names);
	public:
		SceneRuntime(RenderSystem* renderSystem,ResourceManager* resourceManager);
		~SceneRuntime();
		// When supplied, the domain is configured only after the scene candidate is
		// fully instantiated, so invalid documents and failed imports leave both
		// the active scene and its shadow setup untouched.
		bool rebuild(SceneDocument const& document,std::map<std::string,ResourcePtr> const& materialBindings={},std::map<std::string,UniformCollection> const& instanceOverrides={},std::string const& expectedEnvironmentBinding={},std::string const& shadowDomain={},std::map<std::string,ResourcePtr> const& particleEffectBindings={});
		void clear();
		ScenePtr const& getScene()const;
		DiagnosticBag const& getDiagnostics()const;
		uint64_t getGeneration()const;
		uint64_t getUniqueTriangleCount()const;
		uint64_t getModelTriangleCount(std::string const& modelId)const;
		SceneModel3dPtr getModelInstance(std::string const& modelId)const;
		ParticleEffectHandle getParticleEffect(std::string const& effectId)const;
		std::string getModelId(SceneModel3d const* instance)const;
		std::vector<PbrLight> const& getLights()const;
		std::string const& getEnvironmentBinding()const;
	};
}
