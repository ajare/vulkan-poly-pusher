#include "mpp/resource-parsers/FileLegacyPipelineStream.h"
#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FilePostEffectMaterialStream.h"
#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/LegacyPipelineParser.h"

namespace mpp::resource_parsers
{
	FileLegacyPipelineStream::FileLegacyPipelineStream(ResourceManager* manager,std::string filepath):LegacyPipelineStream(manager),mFilepath(std::move(filepath)){}
	FileLegacyPipelineStream::FileLegacyPipelineStream(ResourceManager* manager,std::shared_ptr<LegacyPipelineDocument> document,std::string sourcePath):LegacyPipelineStream(manager),mFilepath(std::move(sourcePath)),mSuppliedDocument(std::move(document)){setDocument(mSuppliedDocument);}

	void FileLegacyPipelineStream::createChildResourceStreamsImpl()
	{
		if(!getDocument())setDocument(std::make_shared<LegacyPipelineDocument>(LegacyPipelineParser::fromFile(mFilepath)));
		auto addResource=[&](LegacyPipelineResourceDocument const& resource,std::string const& filepath,std::string const& runtimeName)
		{
			ResourceStreamPtr stream;switch(resource.kind)
			{
			case LegacyPipelineResourceKind::BasicMaterial:stream=std::make_shared<FileBasicMaterialStream>(getResourceMgr(),filepath,resource.definition);break;
			case LegacyPipelineResourceKind::Program:stream=std::make_shared<FileProgramStream>(getResourceMgr(),filepath,resource.definition);break;
			case LegacyPipelineResourceKind::Texture:stream=std::make_shared<FileTextureStream>(getResourceMgr(),filepath,resource.definition);break;
			case LegacyPipelineResourceKind::Sampler:stream=std::make_shared<FileSamplerStream>(getResourceMgr(),filepath,resource.definition);break;
			case LegacyPipelineResourceKind::PostEffectMaterial:stream=std::make_shared<FilePostEffectMaterialStream>(getResourceMgr(),filepath,resource.definition);break;
			case LegacyPipelineResourceKind::ParticleEffect:stream=std::make_shared<FileParticleEffectStream>(getResourceMgr(),filepath,resource.definition);break;
			}addChild(runtimeName,stream);
		};
		for(auto const& resource:getDocument()->localResources)addResource(resource,mFilepath,resource.name);
		for(auto const& external:getDocument()->externalResources)addResource(external.resource,external.libraryPath,external.libraryName+"::"+external.resource.name);
	}

	void FileLegacyPipelineStream::loadImpl()
	{
		if(!getDocument())setDocument(std::make_shared<LegacyPipelineDocument>(LegacyPipelineParser::fromFile(mFilepath)));
	}
}
