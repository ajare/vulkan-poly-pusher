#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/LegacyPipelineDocument.h"
#include "mpp/RenderPipeline.h"
#include "mpp/RenderTarget.h"
#include "mpp/Resource.h"

namespace mpp::resource_parsers
{
	// Transactional runtime resolution for one complete LegacyPipeline
	// document. Mirrors PbrPipelineRuntime minus the PBR environment/IBL
	// concept, which has no legacy equivalent.
	class _MPPRESOURCEPARSERSAPI LegacyPipelineRuntime
	{
		RenderSystem* mRenderSystem;
		ResourceManager* mResourceManager;
		uint64_t mGeneration{0};
		std::string mRootResource;
		std::shared_ptr<LegacyPipelineDocument> mDocument;
		std::map<std::string,ResourcePtr> mMaterialBindings;
		std::map<std::string,UniformCollection> mInstanceOverrides;
		std::map<std::string,RenderTargetPtr> mImports;
		RenderTargetPtr mPresentationTarget;
		DiagnosticBag mDiagnostics;
		std::string mPreviousRootResource;
		std::shared_ptr<LegacyPipelineDocument> mPreviousDocument;
		std::map<std::string,ResourcePtr> mPreviousMaterialBindings;
		std::map<std::string,UniformCollection> mPreviousInstanceOverrides;
		std::map<std::string,RenderTargetPtr> mPreviousImports;
		RenderTargetPtr mPreviousPresentationTarget;
		std::vector<std::string> mRetiredRootResources;
		void retireRoot(std::string root);
		void cleanupRetiredRoots();
		ResourcePtr resolve(std::string const& reference,std::string const& root)const;
	public:
		LegacyPipelineRuntime(RenderSystem* renderSystem,ResourceManager* resourceManager);
		~LegacyPipelineRuntime();
		bool rebuild(std::shared_ptr<LegacyPipelineDocument> document,uint32_t viewportWidth,uint32_t viewportHeight);
		void accept();
		void rollback();
		void clear();
		void resize(uint32_t width,uint32_t height);
		std::shared_ptr<LegacyPipelineDocument> const& getDocument()const;
		std::map<std::string,ResourcePtr> const& getMaterialBindings()const;
		std::map<std::string,UniformCollection> const& getInstanceOverrides()const;
		std::map<std::string,RenderTargetPtr> const& getImports()const;
		RenderTargetPtr const& getPresentationTarget()const;
		ResourcePtr getResolvedResource(std::string const& reference)const;
		std::string const& getRootResource()const;
		DiagnosticBag const& getDiagnostics()const;
	};
}
