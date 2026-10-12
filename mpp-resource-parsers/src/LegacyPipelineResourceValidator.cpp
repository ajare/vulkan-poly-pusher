#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FilePostEffectMaterialStream.h"
#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/LegacyPipelineResourceValidator.h"
#include "mpp/resource-parsers/ParticleEffectParser.h"
namespace mpp::resource_parsers
{
	DiagnosticBag validateLegacyPipelineResourceDefinitions(LegacyPipelineDocument const& document)
	{
		DiagnosticBag diagnostics;auto validate=[&](LegacyPipelineResourceDocument const& resource,std::string const& filepath,std::string const& object){try{if(resource.kind==LegacyPipelineResourceKind::ParticleEffect){diagnostics.append(ParticleEffectParser::fromData(resource.definition,filepath).diagnostics);return;}ResourceStreamPtr stream;switch(resource.kind){case LegacyPipelineResourceKind::BasicMaterial:stream=std::make_shared<FileBasicMaterialStream>(nullptr,filepath,resource.definition);break;case LegacyPipelineResourceKind::Program:stream=std::make_shared<FileProgramStream>(nullptr,filepath,resource.definition);break;case LegacyPipelineResourceKind::Texture:stream=std::make_shared<FileTextureStream>(nullptr,filepath,resource.definition);break;case LegacyPipelineResourceKind::Sampler:stream=std::make_shared<FileSamplerStream>(nullptr,filepath,resource.definition);break;case LegacyPipelineResourceKind::PostEffectMaterial:stream=std::make_shared<FilePostEffectMaterialStream>(nullptr,filepath,resource.definition);break;case LegacyPipelineResourceKind::ParticleEffect:stream=std::make_shared<FileParticleEffectStream>(nullptr,filepath,resource.definition);break;}stream->load();stream->unload();}catch(std::exception const& error){diagnostics.error("MPP-LEGACY-PIPELINE-RESOURCE-001","Resource '"+object+"' is invalid: "+error.what(),{filepath},object);}};for(auto const& resource:document.localResources)validate(resource,document.sourcePath,resource.name);for(auto const& external:document.externalResources)validate(external.resource,external.libraryPath,external.libraryName+"::"+external.resource.name);return diagnostics;
	}
}
