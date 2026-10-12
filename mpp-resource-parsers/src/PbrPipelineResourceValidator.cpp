#include "mpp/resource-parsers/FilePbrMaterialStream.h"
#include "mpp/resource-parsers/FilePostEffectMaterialStream.h"
#include "mpp/resource-parsers/FileParticleEffectStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/PbrPipelineResourceValidator.h"
#include "mpp/resource-parsers/ParticleEffectParser.h"
namespace mpp::resource_parsers
{
	DiagnosticBag validatePbrPipelineResourceDefinitions(PbrPipelineDocument const& document)
	{
		DiagnosticBag diagnostics;auto validate=[&](PbrPipelineResourceDocument const& resource,std::string const& filepath,std::string const& object){try{if(resource.kind==PbrPipelineResourceKind::ParticleEffect){diagnostics.append(ParticleEffectParser::fromData(resource.definition,filepath).diagnostics);return;}ResourceStreamPtr stream;switch(resource.kind){case PbrPipelineResourceKind::PbrMaterial:stream=std::make_shared<FilePbrMaterialStream>(nullptr,filepath,resource.definition);break;case PbrPipelineResourceKind::Program:stream=std::make_shared<FileProgramStream>(nullptr,filepath,resource.definition);break;case PbrPipelineResourceKind::Texture:stream=std::make_shared<FileTextureStream>(nullptr,filepath,resource.definition);break;case PbrPipelineResourceKind::Sampler:stream=std::make_shared<FileSamplerStream>(nullptr,filepath,resource.definition);break;case PbrPipelineResourceKind::PostEffectMaterial:stream=std::make_shared<FilePostEffectMaterialStream>(nullptr,filepath,resource.definition);break;case PbrPipelineResourceKind::ParticleEffect:stream=std::make_shared<FileParticleEffectStream>(nullptr,filepath,resource.definition);break;}stream->load();stream->unload();}catch(std::exception const& error){diagnostics.error("MPP-PIPELINE-RESOURCE-001","Resource '"+object+"' is invalid: "+error.what(),{filepath},object);}};for(auto const& resource:document.localResources)validate(resource,document.sourcePath,resource.name);for(auto const& external:document.externalResources)validate(external.resource,external.libraryPath,external.libraryName+"::"+external.resource.name);return diagnostics;
	}
}
