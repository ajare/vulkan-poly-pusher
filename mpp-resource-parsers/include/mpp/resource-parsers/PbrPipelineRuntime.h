#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/PbrPipelineDocument.h"
#include "mpp/RenderPipeline.h"
#include "mpp/RenderTarget.h"
#include "mpp/Resource.h"

namespace mpp::resource_parsers
{
	// Transactional runtime resolution for one complete PbrPipeline document.
	// Candidate resources, reflection, imports, bindings, overrides, and the
	// environment are validated before replacing the active generation.
	class _MPPRESOURCEPARSERSAPI PbrPipelineRuntime
	{
		RenderSystem* mRenderSystem;
		ResourceManager* mResourceManager;
		uint64_t mGeneration{0};
		std::string mRootResource;
		std::shared_ptr<PbrPipelineDocument> mDocument;
		std::map<std::string,ResourcePtr> mMaterialBindings;
		std::map<std::string,UniformCollection> mInstanceOverrides;
		std::map<std::string,RenderTargetPtr> mImports;
		RenderTargetPtr mPresentationTarget;
		PbrEnvironmentPtr mEnvironment;
		DiagnosticBag mDiagnostics;
		std::string mPreviousRootResource;
		std::shared_ptr<PbrPipelineDocument> mPreviousDocument;
		std::map<std::string,ResourcePtr> mPreviousMaterialBindings;
		std::map<std::string,UniformCollection> mPreviousInstanceOverrides;
		std::map<std::string,RenderTargetPtr> mPreviousImports;
		RenderTargetPtr mPreviousPresentationTarget;
		PbrEnvironmentPtr mPreviousEnvironment;
		std::vector<std::string> mRetiredRootResources;
		void retireRoot(std::string root);
		void cleanupRetiredRoots();
		ResourcePtr resolve(std::string const& reference,std::string const& root)const;
	public:
		PbrPipelineRuntime(RenderSystem* renderSystem,ResourceManager* resourceManager);
		~PbrPipelineRuntime();
		bool rebuild(std::shared_ptr<PbrPipelineDocument> document,uint32_t viewportWidth,uint32_t viewportHeight);
		void accept();
		void rollback();
		void clear();
		void resize(uint32_t width,uint32_t height);
		std::shared_ptr<PbrPipelineDocument> const& getDocument()const;
		std::map<std::string,ResourcePtr> const& getMaterialBindings()const;
		std::map<std::string,UniformCollection> const& getInstanceOverrides()const;
		std::map<std::string,RenderTargetPtr> const& getImports()const;
		RenderTargetPtr const& getPresentationTarget()const;
		PbrEnvironmentPtr const& getEnvironment()const;
		ResourcePtr getResolvedResource(std::string const& reference)const;
		// The current generation's declared resource root (e.g. "PbrPipelineRuntime.3")
		// that LocalResources/PbrPipelineDocument-authored resources are children
		// of. RenderPipelineOptions::resourceRoot needs this so a graph pass's
		// MPP.FullscreenEffect programResource (authored as a bare local resource
		// name, since the document can't predict its own dynamic root) resolves
		// against the right generation at execute time.
		std::string const& getRootResource()const;
		DiagnosticBag const& getDiagnostics()const;
	};
}
