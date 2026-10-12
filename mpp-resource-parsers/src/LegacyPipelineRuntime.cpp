#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "mpp/BasicMaterial.h"
#include "mpp/RenderGraphTargets.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderTexture.h"
#include "mpp/ResourceManager.h"
#include "mpp/Texture.h"
#include "mpp/resource-parsers/FileLegacyPipelineStream.h"
#include "mpp/resource-parsers/LegacyPipelineRuntime.h"
#include "mpp/resource-parsers/LegacyPipelineResourceValidator.h"

namespace mpp::resource_parsers
{
	LegacyPipelineRuntime::LegacyPipelineRuntime(RenderSystem* renderSystem,ResourceManager* resourceManager):mRenderSystem(renderSystem),mResourceManager(resourceManager)
	{
		if(!mRenderSystem||!mResourceManager)throw std::invalid_argument("LegacyPipelineRuntime requires render and resource systems.");
	}
	LegacyPipelineRuntime::~LegacyPipelineRuntime(){clear();}

	ResourcePtr LegacyPipelineRuntime::resolve(std::string const& reference,std::string const& root)const
	{
		if(reference.empty())return {};if(auto owned=mResourceManager->getResource(root+"/"+reference,true))return owned;return mResourceManager->getResource(reference,true);
	}

	bool LegacyPipelineRuntime::rebuild(std::shared_ptr<LegacyPipelineDocument> document,uint32_t viewportWidth,uint32_t viewportHeight)
	{
		DiagnosticBag candidateDiagnostics;if(!document){candidateDiagnostics.error("MPP-LEGACY-PIPELINE-RUNTIME-001","Pipeline document is missing.");mDiagnostics=candidateDiagnostics;return false;}candidateDiagnostics=document->validate(mRenderSystem->getCaps(),glm::uvec2(viewportWidth,viewportHeight));candidateDiagnostics.append(document->validateOutputAntiAliasing(mRenderSystem->getOptions().antiAliasing,&mRenderSystem->getCaps()));for(auto const& output:document->outputs){auto effective=resolveAntiAliasing(mRenderSystem->getOptions().antiAliasing,output.antiAliasing);auto width=(uint64_t)ssaaDimension(viewportWidth,effective.ssaa),height=(uint64_t)ssaaDimension(viewportHeight,effective.ssaa);if(width>(uint64_t)mRenderSystem->getCaps().maxTextureSize||height>(uint64_t)mRenderSystem->getCaps().maxTextureSize)candidateDiagnostics.error("MPP-LEGACY-PIPELINE-RUNTIME-009","Output '"+output.name+"' SSAA requires "+std::to_string(width)+"x"+std::to_string(height)+", exceeding the GPU maximum texture size.",{document->sourcePath},output.name);}candidateDiagnostics.append(validateLegacyPipelineResourceDefinitions(*document));if(candidateDiagnostics.hasErrors()){mDiagnostics=candidateDiagnostics;return false;}auto suffix=std::to_string(++mGeneration),candidateRoot="LegacyPipelineRuntime."+suffix;std::map<std::string,ResourcePtr> candidateBindings;std::map<std::string,UniformCollection> candidateOverrides;std::map<std::string,RenderTargetPtr> candidateImports;RenderTargetPtr candidatePresentation;bool rootDeclared=false;
		try
		{
			auto stream=std::make_shared<FileLegacyPipelineStream>(mResourceManager,document,document->sourcePath);auto resource=mResourceManager->declareResource(candidateRoot,stream).first;rootDeclared=true;resource->load();resource->create();
			for(auto const& binding:document->previewBindings){auto material=resolve(binding.materialResource,candidateRoot);if(!material){candidateDiagnostics.warning("MPP-LEGACY-PIPELINE-RUNTIME-002","Preview material '"+binding.materialResource+"' is unavailable; the conspicuous neutral material will be used.",{document->sourcePath},binding.binding);continue;}if(!dynamic_cast<BasicMaterial*>(material.get())){candidateDiagnostics.error("MPP-LEGACY-PIPELINE-RUNTIME-003","Preview binding '"+binding.binding+"' does not resolve to BasicMaterial.",{document->sourcePath},binding.binding);continue;}material->load();candidateBindings[binding.binding]=material;}
			for(auto const& overrideValue:document->previewOverrides){auto material=candidateBindings.find(overrideValue.binding);if(material==candidateBindings.end()){candidateDiagnostics.warning("MPP-LEGACY-PIPELINE-RUNTIME-004","Preview override for model '"+overrideValue.modelId+"' is inactive because its material binding is unresolved.",{document->sourcePath},overrideValue.modelId);continue;}static_cast<BasicMaterial*>(material->second.get())->validateInstanceUniforms(overrideValue.values);candidateOverrides[overrideValue.modelId]=overrideValue.values;}
			for(auto const& import:document->imports){GraphImageDesc desc;desc.format=import.format;desc.usage=import.usage;desc.external=true;desc.transient=false;for(auto image:document->graph->getImportedImages()){auto info=document->graph->getImageInfo(image);if(info.importName==import.id||info.importName==import.semantic){desc=info.desc;break;}}auto width=desc.absoluteSize.x?desc.absoluteSize.x:std::max(1u,(uint32_t)(viewportWidth*desc.relativeSize.x));auto height=desc.absoluteSize.y?desc.absoluteSize.y:std::max(1u,(uint32_t)(viewportHeight*desc.relativeSize.y));auto target=mRenderSystem->createRenderTexture("LegacyPipelineRuntime.Import."+import.id+"."+suffix,width,height,makeGraphRenderTextureOptions(desc));candidateImports[import.id]=target;candidateImports[import.semantic]=target;if(import.id=="screen"||import.semantic=="presentation")candidatePresentation=target;}
			if(!document->outputs.empty())for(uint32_t image=0;image<document->graph->getImageCount();++image){auto info=document->graph->getImageInfo({image,0});if(info.name!=document->outputs.front().image||info.importName.empty())continue;auto found=candidateImports.find(info.importName);if(found!=candidateImports.end())candidatePresentation=found->second;break;}
			for(auto const& output:document->outputs)if(resolveAntiAliasing(mRenderSystem->getOptions().antiAliasing,output.antiAliasing).taa&&output.taaDepth.empty()){std::shared_ptr<RenderTexture> target;for(uint32_t image=0;image<document->graph->getImageCount();++image){auto info=document->graph->getImageInfo({image,0});if(info.name!=output.image||info.importName.empty())continue;auto found=candidateImports.find(info.importName);if(found!=candidateImports.end())target=std::dynamic_pointer_cast<RenderTexture>(found->second);break;}if(!target||target->getDepthTextureId()==0)candidateDiagnostics.error("MPP-LEGACY-PIPELINE-RUNTIME-008","TAA output '"+output.name+"' omits taaDepth, but its output target has no depth texture.",{document->sourcePath},output.name);}
			if(candidateDiagnostics.hasErrors())throw std::runtime_error("Pipeline runtime resource validation failed.");
		}
		catch(std::exception const& error){candidateDiagnostics.error("MPP-LEGACY-PIPELINE-RUNTIME-007",std::string("Pipeline candidate creation failed: ")+error.what(),{document->sourcePath},"pipeline");if(rootDeclared){try{mResourceManager->deleteResourceTree(candidateRoot);}catch(...){}}mDiagnostics=candidateDiagnostics;return false;}
		accept();mPreviousRootResource=std::move(mRootResource);mPreviousDocument=std::move(mDocument);mPreviousMaterialBindings=std::move(mMaterialBindings);mPreviousInstanceOverrides=std::move(mInstanceOverrides);mPreviousImports=std::move(mImports);mPreviousPresentationTarget=std::move(mPresentationTarget);mRootResource=candidateRoot;mDocument=std::move(document);mMaterialBindings=std::move(candidateBindings);mInstanceOverrides=std::move(candidateOverrides);mImports=std::move(candidateImports);mPresentationTarget=std::move(candidatePresentation);mDiagnostics=std::move(candidateDiagnostics);return true;
	}

	void LegacyPipelineRuntime::retireRoot(std::string root)
	{
		if(root.empty())return;
		try
		{
			if(auto rootResource=mResourceManager->getResource(root,true)){rootResource->destroy();mResourceManager->deleteResource(root);}
			bool referenced=false;for(auto const& name:mResourceManager->getResourceNamesWithPrefix(root+"/"))if(!mResourceManager->isResourceAlias(name))if(auto resource=mResourceManager->getResource(name,true))if(resource->getRefCount()!=0||resource->getDependingObjectCount()!=0||resource->getDependentResourceCount()!=0){referenced=true;break;}
			if(referenced){if(std::find(mRetiredRootResources.begin(),mRetiredRootResources.end(),root)==mRetiredRootResources.end())mRetiredRootResources.push_back(std::move(root));return;}
			mResourceManager->deleteResourceTree(root);
		}
		catch(...){if(std::find(mRetiredRootResources.begin(),mRetiredRootResources.end(),root)==mRetiredRootResources.end())mRetiredRootResources.push_back(std::move(root));}
	}
	void LegacyPipelineRuntime::cleanupRetiredRoots()
	{
		auto roots=std::move(mRetiredRootResources);mRetiredRootResources.clear();for(auto& root:roots)retireRoot(std::move(root));
	}
	void LegacyPipelineRuntime::accept(){auto previousRoot=std::move(mPreviousRootResource);mPreviousDocument.reset();mPreviousMaterialBindings.clear();mPreviousInstanceOverrides.clear();mPreviousImports.clear();mPreviousPresentationTarget.reset();cleanupRetiredRoots();retireRoot(std::move(previousRoot));}
	void LegacyPipelineRuntime::rollback()
	{
		auto candidateRoot=std::move(mRootResource);mDocument.reset();mMaterialBindings.clear();mInstanceOverrides.clear();mImports.clear();mPresentationTarget.reset();retireRoot(std::move(candidateRoot));
		if(mPreviousDocument){mRootResource=std::move(mPreviousRootResource);mDocument=std::move(mPreviousDocument);mMaterialBindings=std::move(mPreviousMaterialBindings);mInstanceOverrides=std::move(mPreviousInstanceOverrides);mImports=std::move(mPreviousImports);mPresentationTarget=std::move(mPreviousPresentationTarget);}else mPreviousRootResource.clear();cleanupRetiredRoots();
	}
	void LegacyPipelineRuntime::clear()
	{
		auto currentRoot=std::move(mRootResource);mDocument.reset();mMaterialBindings.clear();mInstanceOverrides.clear();mImports.clear();mPresentationTarget.reset();retireRoot(std::move(currentRoot));accept();cleanupRetiredRoots();mDiagnostics.clear();
	}
	void LegacyPipelineRuntime::resize(uint32_t width,uint32_t height){if(width==0||height==0)throw std::runtime_error("Pipeline output resize requires non-zero dimensions.");if(mDocument)for(auto const& output:mDocument->outputs){auto effective=resolveAntiAliasing(mRenderSystem->getOptions().antiAliasing,output.antiAliasing);auto physicalWidth=(uint64_t)ssaaDimension(width,effective.ssaa),physicalHeight=(uint64_t)ssaaDimension(height,effective.ssaa);if(physicalWidth>(uint64_t)mRenderSystem->getCaps().maxTextureSize||physicalHeight>(uint64_t)mRenderSystem->getCaps().maxTextureSize)throw std::runtime_error("Output '"+output.name+"' resize would require "+std::to_string(physicalWidth)+"x"+std::to_string(physicalHeight)+", exceeding the GPU maximum texture size; prior resources were retained.");}if(mPresentationTarget&&!mPresentationTarget->resize(width,height))throw std::runtime_error("Pipeline output resize failed; prior resources were retained.");}
	std::shared_ptr<LegacyPipelineDocument> const& LegacyPipelineRuntime::getDocument()const{return mDocument;}
	std::map<std::string,ResourcePtr> const& LegacyPipelineRuntime::getMaterialBindings()const{return mMaterialBindings;}
	std::map<std::string,UniformCollection> const& LegacyPipelineRuntime::getInstanceOverrides()const{return mInstanceOverrides;}
	std::map<std::string,RenderTargetPtr> const& LegacyPipelineRuntime::getImports()const{return mImports;}
	RenderTargetPtr const& LegacyPipelineRuntime::getPresentationTarget()const{return mPresentationTarget;}
	ResourcePtr LegacyPipelineRuntime::getResolvedResource(std::string const& reference)const{return resolve(reference,mRootResource);}
	std::string const& LegacyPipelineRuntime::getRootResource()const{return mRootResource;}
	DiagnosticBag const& LegacyPipelineRuntime::getDiagnostics()const{return mDiagnostics;}
}
