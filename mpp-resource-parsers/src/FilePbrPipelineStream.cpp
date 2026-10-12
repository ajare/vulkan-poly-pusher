#include "mpp/resource-parsers/FilePbrPipelineStream.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"
#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/FilePostEffectMaterialStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/PbrPipelineParser.h"

namespace mpp::resource_parsers
{
	FilePbrPipelineStream::FilePbrPipelineStream(ResourceManager* manager,std::string filepath):PbrPipelineStream(manager),mFilepath(std::move(filepath)){}
	FilePbrPipelineStream::FilePbrPipelineStream(ResourceManager* manager,std::shared_ptr<PbrPipelineDocument> document,std::string sourcePath):PbrPipelineStream(manager),mFilepath(std::move(sourcePath)),mSuppliedDocument(std::move(document)){setDocument(mSuppliedDocument);}

	void FilePbrPipelineStream::createChildResourceStreamsImpl()
	{
		if(!getDocument())setDocument(std::make_shared<PbrPipelineDocument>(PbrPipelineParser::fromFile(mFilepath)));
		auto addResource=[&](PbrPipelineResourceDocument const& resource,std::string const& filepath,std::string const& runtimeName)
		{
			ResourceStreamPtr stream;switch(resource.kind)
			{
			case PbrPipelineResourceKind::PbrMaterial:stream=std::make_shared<FilePbrMaterialStream>(getResourceMgr(),filepath,resource.definition);break;
			case PbrPipelineResourceKind::Program:stream=std::make_shared<FileProgramStream>(getResourceMgr(),filepath,resource.definition);break;
			case PbrPipelineResourceKind::Texture:stream=std::make_shared<FileTextureStream>(getResourceMgr(),filepath,resource.definition);break;
			case PbrPipelineResourceKind::Sampler:stream=std::make_shared<FileSamplerStream>(getResourceMgr(),filepath,resource.definition);break;
			case PbrPipelineResourceKind::PostEffectMaterial:stream=std::make_shared<FilePostEffectMaterialStream>(getResourceMgr(),filepath,resource.definition);break;
			case PbrPipelineResourceKind::ParticleEffect:stream=std::make_shared<FileParticleEffectStream>(getResourceMgr(),filepath,resource.definition);break;
			}addChild(runtimeName,stream);
		};
		for(auto const& resource:getDocument()->localResources)addResource(resource,mFilepath,resource.name);
		for(auto const& external:getDocument()->externalResources)addResource(external.resource,external.libraryPath,external.libraryName+"::"+external.resource.name);
	}

	void FilePbrPipelineStream::loadImpl()
	{
		if(!getDocument())setDocument(std::make_shared<PbrPipelineDocument>(PbrPipelineParser::fromFile(mFilepath)));
	}
}
