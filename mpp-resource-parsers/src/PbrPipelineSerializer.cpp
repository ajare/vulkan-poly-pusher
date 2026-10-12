#include <cstring>
#include <sstream>
#include "mpp/MppException.h"
#include "mpp/resource-parsers/DocumentWriter.h"
#include "mpp/resource-parsers/PbrPipelineSerializer.h"
#include "mpp/resource-parsers/RenderGraphSerializer.h"
#include "mpp/resource-parsers/StructuredDataWriteNode.h"

namespace mpp::resource_parsers
{
	namespace
	{
		std::string usage(GraphImageUsage value){std::string result;auto add=[&](GraphImageUsage flag,char const*name){if(hasGraphImageUsage(value,flag)){if(!result.empty())result+=",";result+=name;}};add(GraphImageUsage::Sampled,"sampled");add(GraphImageUsage::ColourAttachment,"colourAttachment");add(GraphImageUsage::DepthAttachment,"depthAttachment");add(GraphImageUsage::Presentation,"presentation");return result;}
		std::string samples(std::optional<AntiAliasingSamples> const& value){return value?antiAliasingSamplesName(*value):"inherit";}
		std::string boolean(std::optional<bool> const& value){return value?(*value?"true":"false"):"inherit";}
		void writeData(mpp::data::StructuredData const& data,StructuredDataWriteNode* parent){for(auto const& entry:data){auto child=parent->createChild(entry.first);if(entry.second.isValue())child->setValue(entry.second.getValue());else writeData(entry.second,child);}}
		void writeUniforms(UniformCollection const& values,StructuredDataWriteNode* parent)
		{
			for(auto const& entry:values.getUniformData()){auto const& value=entry.second;if(value.count!=1)continue;std::string type;if(value.type==program::GLSLType::Int)type="Int";else if(value.type==program::GLSLType::Bool)type="Bool";else if(value.type==program::GLSLType::Float&&value.numElements>=1&&value.numElements<=4)type=value.numElements==1?"Float":value.numElements==2?"Vec2":value.numElements==3?"Vec3":"Vec4";else continue;auto node=parent->createChild(type);node->createChild("name")->setValue(entry.first);std::ostringstream text;if(value.type==program::GLSLType::Int||value.type==program::GLSLType::Bool){int32_t current;memcpy(&current,value.data,sizeof(current));if(value.type==program::GLSLType::Bool)text<<(current?"true":"false");else text<<current;}else{auto current=reinterpret_cast<float const*>(value.data);for(size_t i=0;i<value.numElements;++i){if(i)text<<' ';text<<current[i];}}node->createChild("value")->setValue(text.str());}
		}
	}
	void PbrPipelineSerializer::toFile(PbrPipelineDocument const& document, std::string const& filepath)
	{
		if (!document.graph) THROW_MPP("Cannot serialize PbrPipeline without a RenderGraph.", __LINE__, __FILE__, __func__);
		StructuredDataWriteNode writer("PbrPipeline");
		auto root = &writer;
		root->createChild("version")->setValue(document.version);
		root->createChild("name")->setValue(document.name);
		if (!document.previewScene.empty()) root->createChild("PreviewScene")->createChild("file")->setValue(document.previewScene);
		if (!document.resourceLibraries.empty())
		{
			auto libraries = root->createChild("ResourceLibraries");
			for (auto const& path : document.resourceLibraries) libraries->createChild("Library")->createChild("file")->setValue(path);
		}
		if(!document.localResources.empty())
		{
			auto resources=root->createChild("LocalResources");for(auto const& value:document.localResources){auto node=resources->createChild(value.definition.getName());writeData(value.definition,node);}
		}
		if(!document.imports.empty())
		{
			auto imports=root->createChild("Imports"); for(auto const& value:document.imports){auto node=imports->createChild("Import");node->createChild("id")->setValue(value.id);node->createChild("semantic")->setValue(value.semantic);node->createChild("format")->setValue(std::string(graphImageFormatName(value.format)));node->createChild("usage")->setValue(usage(value.usage));node->createChild("required")->setValue(value.required);if(!value.fallback.empty())node->createChild("fallback")->setValue(value.fallback);}
		}
		if(!document.outputs.empty())
		{
			auto outputs=root->createChild("Outputs");for(auto const& value:document.outputs){auto output=outputs->createChild("Output");output->createChild("name")->setValue(value.name);output->createChild("image")->setValue(value.image);if(!value.taaDepth.empty())output->createChild("taaDepth")->setValue(value.taaDepth);auto aa=output->createChild("AntiAliasing");aa->createChild("msaa")->setValue(samples(value.antiAliasing.msaa));aa->createChild("ssaa")->setValue(samples(value.antiAliasing.ssaa));aa->createChild("taa")->setValue(boolean(value.antiAliasing.taa));aa->createChild("fxaa")->setValue(boolean(value.antiAliasing.fxaa));}
		}
		auto environment = root->createChild("Environment");
		environment->createChild("binding")->setValue(document.environment.binding);
		environment->createChild("irradiance")->setValue(document.environment.irradiance);
		environment->createChild("prefilteredSpecular")->setValue(document.environment.prefilteredSpecular);
		environment->createChild("brdfLut")->setValue(document.environment.brdfLut);
		environment->createChild("background")->setValue(document.environment.background);
		if(!document.environment.hdrEquirectangular.empty()){environment->createChild("hdrEquirectangular")->setValue(document.environment.hdrEquirectangular);environment->createChild("environmentResolution")->setValue(document.environment.environmentResolution);environment->createChild("irradianceResolution")->setValue(document.environment.irradianceResolution);environment->createChild("prefilterResolution")->setValue(document.environment.prefilterResolution);}
		auto bloom=root->createChild("Bloom");bloom->createChild("enabled")->setValue(document.bloom.enabled);bloom->createChild("blurPasses")->setValue(document.bloom.blurPasses);
		auto ao=root->createChild("AmbientOcclusion");ao->createChild("method")->setValue(document.ambientOcclusion.method==AmbientOcclusionMethod::Ssao?"ssao":document.ambientOcclusion.method==AmbientOcclusionMethod::Gtao?"gtao":"none");
		auto ssao=ao->createChild("SSAO");ssao->createChild("radius")->setValue(document.ambientOcclusion.ssao.radius);ssao->createChild("intensity")->setValue(document.ambientOcclusion.ssao.intensity);ssao->createChild("bias")->setValue(document.ambientOcclusion.ssao.bias);ssao->createChild("power")->setValue(document.ambientOcclusion.ssao.power);ssao->createChild("sampleCount")->setValue(document.ambientOcclusion.ssao.sampleCount);ssao->createChild("blurRadius")->setValue(document.ambientOcclusion.ssao.blurRadius);
		auto gtao=ao->createChild("GTAO");gtao->createChild("radius")->setValue(document.ambientOcclusion.gtao.radius);gtao->createChild("intensity")->setValue(document.ambientOcclusion.gtao.intensity);gtao->createChild("thickness")->setValue(document.ambientOcclusion.gtao.thickness);gtao->createChild("horizonBias")->setValue(document.ambientOcclusion.gtao.horizonBias);gtao->createChild("falloffStart")->setValue(document.ambientOcclusion.gtao.falloffStart);gtao->createChild("falloffEnd")->setValue(document.ambientOcclusion.gtao.falloffEnd);gtao->createChild("sliceCount")->setValue(document.ambientOcclusion.gtao.sliceCount);gtao->createChild("stepsPerSlice")->setValue(document.ambientOcclusion.gtao.stepsPerSlice);gtao->createChild("power")->setValue(document.ambientOcclusion.gtao.power);gtao->createChild("blurRadius")->setValue(document.ambientOcclusion.gtao.blurRadius);gtao->createChild("normalSource")->setValue(document.ambientOcclusion.gtao.normalSource==GTAONormalSource::Mrt?"mrt":"depth");
		if (!document.previewBindings.empty())
		{
			auto bindings = root->createChild("PreviewBindings");
			for (auto const& value : document.previewBindings)
			{
				auto binding = bindings->createChild("Material");
				binding->createChild("binding")->setValue(value.binding);
				binding->createChild("resource")->setValue(value.materialResource);
			}
		}
		if(!document.previewOverrides.empty())
		{
			auto overrides=root->createChild("PreviewOverrides");for(auto const& value:document.previewOverrides){auto node=overrides->createChild("Override");node->createChild("model")->setValue(value.modelId);node->createChild("binding")->setValue(value.binding);if(!value.values.getUniformData().empty())writeUniforms(value.values,node->createChild("Values"));}
		}
		if(!document.extensions.empty())
		{
			auto extensions=root->createChild("Extensions");for(auto const& value:document.extensions){auto node=extensions->createChild("Extension");node->createChild("namespace")->setValue(value.nameSpace);writeData(value.payload,node->createChild("Payload"));}
		}
		auto graph = root->createChild("RenderGraph");
		RenderGraphSerializer::toNode(*document.graph, graph);
		detail::writeDocument(writer.toStructuredData(), filepath);
	}
}
