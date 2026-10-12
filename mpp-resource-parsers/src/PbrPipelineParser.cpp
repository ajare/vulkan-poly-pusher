#include <filesystem>
#include <memory>
#include <set>
#include <sstream>

#include "utils/StringUtils.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"
#include "mpp/resource-parsers/PbrPipelineParser.h"
#include "mpp/resource-parsers/RenderGraphParser.h"
#include "StructuredDataAdapter.h"

using namespace std;

namespace mpp::resource_parsers
{
	namespace
	{
		GraphImageFormat importFormat(std::string value)
		{
			utils::StringUtils::toUpper(value);
			if(value=="R8")return GraphImageFormat::R8;if(value=="RG8")return GraphImageFormat::Rg8;if(value=="RGBA8")return GraphImageFormat::Rgba8;if(value=="SRGB8_ALPHA8")return GraphImageFormat::Srgb8Alpha8;
			if(value=="R16F")return GraphImageFormat::R16f;if(value=="RG16F")return GraphImageFormat::Rg16f;if(value=="RGBA16F")return GraphImageFormat::Rgba16f;if(value=="R32F")return GraphImageFormat::R32f;if(value=="RG32F")return GraphImageFormat::Rg32f;if(value=="RGBA32F")return GraphImageFormat::Rgba32f;
			if(value=="R11G11B10F")return GraphImageFormat::R11g11b10f;if(value=="RGB10_A2")return GraphImageFormat::Rgb10a2;if(value=="DEPTH16")return GraphImageFormat::Depth16;if(value=="DEPTH24")return GraphImageFormat::Depth24;if(value=="DEPTH32F")return GraphImageFormat::Depth32f;if(value=="DEPTH24_STENCIL8")return GraphImageFormat::Depth24Stencil8;if(value=="DEPTH32F_STENCIL8")return GraphImageFormat::Depth32fStencil8;
			THROW_MPP_RESOURCE_PARSERS("Unknown PbrPipeline import format '"+value+"'.",__LINE__,__FILE__,__func__);
		}
		GraphImageUsage importUsage(std::string value)
		{
			GraphImageUsage usage=GraphImageUsage::None;std::stringstream stream(value);std::string token;
			while(std::getline(stream,token,',')){utils::StringUtils::toUpper(token);if(token=="SAMPLED")usage=usage|GraphImageUsage::Sampled;else if(token=="COLOURATTACHMENT")usage=usage|GraphImageUsage::ColourAttachment;else if(token=="DEPTHATTACHMENT")usage=usage|GraphImageUsage::DepthAttachment;else if(token=="PRESENTATION")usage=usage|GraphImageUsage::Presentation;else THROW_MPP_RESOURCE_PARSERS("Unknown PbrPipeline import usage '"+token+"'.",__LINE__,__FILE__,__func__);}
			return usage;
		}
		bool boolean(std::string value){utils::StringUtils::toUpper(value);if(value=="TRUE"||value=="1"||value=="YES")return true;if(value=="FALSE"||value=="0"||value=="NO")return false;THROW_MPP_RESOURCE_PARSERS("Invalid PbrPipeline boolean '"+value+"'.",__LINE__,__FILE__,__func__);}
		AmbientOcclusionMethod ambientOcclusionMethod(std::string value){utils::StringUtils::toUpper(value);if(value=="NONE")return AmbientOcclusionMethod::None;if(value=="SSAO")return AmbientOcclusionMethod::Ssao;if(value=="GTAO")return AmbientOcclusionMethod::Gtao;THROW_MPP_RESOURCE_PARSERS("Invalid ambient-occlusion method '"+value+"'; expected none, ssao, or gtao.",__LINE__,__FILE__,__func__);}
		GTAONormalSource gtaoNormalSource(std::string value){utils::StringUtils::toUpper(value);if(value=="DEPTH")return GTAONormalSource::Depth;if(value=="MRT")return GTAONormalSource::Mrt;THROW_MPP_RESOURCE_PARSERS("Invalid GTAO normal source '"+value+"'; expected depth or mrt.",__LINE__,__FILE__,__func__);}
		std::optional<AntiAliasingSamples> antiAliasingSamples(std::string value)
		{
			utils::StringUtils::toUpper(value);if(value=="INHERIT")return std::nullopt;if(value=="OFF")return AntiAliasingSamples::Off;if(value=="2X")return AntiAliasingSamples::X2;if(value=="4X")return AntiAliasingSamples::X4;if(value=="8X")return AntiAliasingSamples::X8;THROW_MPP_RESOURCE_PARSERS("Invalid output anti-aliasing sample value '"+value+"'; expected inherit, off, 2x, 4x, or 8x.",__LINE__,__FILE__,__func__);
		}
		std::optional<bool> antiAliasingBoolean(std::string value)
		{
			utils::StringUtils::toUpper(value);if(value=="INHERIT")return std::nullopt;return boolean(value);
		}
		void rejectUnknown(mpp::data::StructuredData const&,std::set<std::string> const&,std::string const&);
		void parseUniforms(mpp::data::StructuredData const& data,UniformCollection& uniforms)
		{
			for(auto const& entry:data){auto const& value=entry.second;rejectUnknown(value,{"name","value"},"PreviewOverrides/Override/Values");auto name=value.getEntry("name").getValue();auto text=value.getEntry("value").getValue();std::istringstream input(text);
				if(entry.first=="Float")uniforms.setUniform(name,utils::StringUtils::parseFloat(text));else if(entry.first=="Int")uniforms.setUniform(name,(int32_t)utils::StringUtils::parseInt(text));else if(entry.first=="Bool"){int32_t current=boolean(text)?1:0;uniforms.setUniform(name,program::GLSLType::Bool,1,1,reinterpret_cast<char const*>(&current));}else if(entry.first=="Vec2"){glm::vec2 current;if(!(input>>current.x>>current.y))THROW_MPP_RESOURCE_PARSERS("Invalid Vec2 preview override.",__LINE__,__FILE__,__func__);uniforms.setUniform(name,current);}else if(entry.first=="Vec3"){glm::vec3 current;if(!(input>>current.x>>current.y>>current.z))THROW_MPP_RESOURCE_PARSERS("Invalid Vec3 preview override.",__LINE__,__FILE__,__func__);uniforms.setUniform(name,current);}else if(entry.first=="Vec4"){glm::vec4 current;if(!(input>>current.x>>current.y>>current.z>>current.w))THROW_MPP_RESOURCE_PARSERS("Invalid Vec4 preview override.",__LINE__,__FILE__,__func__);uniforms.setUniform(name,current);}else THROW_MPP_RESOURCE_PARSERS("Unknown preview override value type '"+entry.first+"'.",__LINE__,__FILE__,__func__);}
		}
		void rejectUnknown(mpp::data::StructuredData const& data,std::set<std::string> const& allowed,std::string const& context){for(auto const& entry:data)if(!allowed.contains(entry.first))THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in "+context+".",__LINE__,__FILE__,__func__);}
		PbrPipelineResourceKind resourceKind(std::string const& value){if(value=="PbrMaterial")return PbrPipelineResourceKind::PbrMaterial;if(value=="Program")return PbrPipelineResourceKind::Program;if(value=="Texture")return PbrPipelineResourceKind::Texture;if(value=="Sampler")return PbrPipelineResourceKind::Sampler;if(value=="PostEffectMaterial")return PbrPipelineResourceKind::PostEffectMaterial;if(value=="ParticleEffect")return PbrPipelineResourceKind::ParticleEffect;THROW_MPP_RESOURCE_PARSERS("Unknown pipeline resource type '"+value+"'.",__LINE__,__FILE__,__func__);}
		void resolveLibrary(PbrPipelineDocument& document,std::string const& path)
		{
			auto resolved=std::filesystem::path(path);if(!resolved.is_absolute())resolved=std::filesystem::path(document.sourcePath).parent_path()/resolved;if(!std::filesystem::exists(resolved))return;auto data=detail::readDocumentRoot(resolved.string());if(data.getName()!="ResourceLibrary")THROW_MPP_RESOURCE_PARSERS("Resource library root must be ResourceLibrary: "+resolved.string(),__LINE__,__FILE__,__func__);rejectUnknown(data,{"version","name","Resources"},"ResourceLibrary");if(!data.hasEntry("version")||utils::StringUtils::parseUInt(data.getEntry("version").getValue())!=1)THROW_MPP_RESOURCE_PARSERS("Unsupported ResourceLibrary version: "+resolved.string(),__LINE__,__FILE__,__func__);if(!data.hasEntry("name"))THROW_MPP_RESOURCE_PARSERS("Resource library requires a name: "+resolved.string(),__LINE__,__FILE__,__func__);auto libraryName=data.getEntry("name").getValue();for(auto const& existing:document.externalResources)if(existing.libraryName==libraryName&&existing.libraryPath!=resolved.string())THROW_MPP_RESOURCE_PARSERS("Duplicate ResourceLibrary name '"+libraryName+"'.",__LINE__,__FILE__,__func__);if(data.hasEntry("Resources"))for(auto const& entry:data.getEntry("Resources")){PbrPipelineExternalResourceDocument external;external.libraryName=libraryName;external.libraryPath=resolved.string();external.resource.kind=resourceKind(entry.first);external.resource.definition=entry.second;if(!entry.second.hasEntry("name"))THROW_MPP_RESOURCE_PARSERS("External resource requires a name in "+resolved.string(),__LINE__,__FILE__,__func__);external.resource.name=entry.second.getEntry("name").getValue();document.externalResources.push_back(external);}
		}
	}
	PbrPipelineDocument PbrPipelineParser::fromFile(string const& filepath)
	{
		auto data = detail::readDocumentRoot(filepath);
		if (data.getName() != "PbrPipeline") THROW_MPP_RESOURCE_PARSERS("Pipeline root must be PbrPipeline: " + filepath, __LINE__, __FILE__, __func__);
		rejectUnknown(data,{"version","name","PreviewScene","ResourceLibraries","LocalResources","Imports","Outputs","Environment","Bloom","AmbientOcclusion","SSAO","PreviewBindings","PreviewOverrides","Extensions","RenderGraph"},"PbrPipeline");
		PbrPipelineDocument document;
		document.sourcePath = filepath;
		document.version = data.hasEntry("version") ? utils::StringUtils::parseUInt(data.getEntry("version").getValue()) : 1;
		document.name = data.hasEntry("name") ? data.getEntry("name").getValue() : "";
		if (data.hasEntry("PreviewScene")) { auto const& value=data.getEntry("PreviewScene");rejectUnknown(value,{"file"},"PreviewScene");document.previewScene = value.getEntry("file").getValue(); }
		if (data.hasEntry("ResourceLibraries"))
			for (auto const& entry : data.getEntry("ResourceLibraries")) { if(entry.first!="Library")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in ResourceLibraries.",__LINE__,__FILE__,__func__);rejectUnknown(entry.second,{"file"},"ResourceLibraries/Library");document.resourceLibraries.push_back(entry.second.getEntry("file").getValue()); }
		for(auto const& library:document.resourceLibraries)resolveLibrary(document,library);
		if(data.hasEntry("LocalResources"))for(auto const& entry:data.getEntry("LocalResources"))
		{
			PbrPipelineResourceDocument resource;resource.kind=resourceKind(entry.first);resource.definition=entry.second;if(!entry.second.hasEntry("name"))THROW_MPP_RESOURCE_PARSERS("Local resource '"+entry.first+"' requires a name.",__LINE__,__FILE__,__func__);resource.name=entry.second.getEntry("name").getValue();document.localResources.push_back(resource);
		}
		if (data.hasEntry("Imports")) for(auto const& entry:data.getEntry("Imports"))
		{
			if(entry.first!="Import")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in Imports.",__LINE__,__FILE__,__func__);
			auto const& value=entry.second;rejectUnknown(value,{"id","semantic","format","usage","required","fallback"},"Imports/Import"); PbrPipelineImportDocument import; import.id=value.getEntry("id").getValue(); import.semantic=value.getEntry("semantic").getValue(); import.format=importFormat(value.getEntry("format").getValue()); import.usage=importUsage(value.getEntry("usage").getValue()); if(value.hasEntry("required"))import.required=boolean(value.getEntry("required").getValue()); if(value.hasEntry("fallback"))import.fallback=value.getEntry("fallback").getValue(); document.imports.push_back(import);
		}
		if(data.hasEntry("Outputs"))for(auto const& entry:data.getEntry("Outputs"))
		{
			if(entry.first!="Output")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in Outputs.",__LINE__,__FILE__,__func__);
			auto const& value=entry.second;rejectUnknown(value,{"name","image","taaDepth","AntiAliasing"},"Outputs/Output");RenderPipelineOutput output;output.name=value.getEntry("name").getValue();output.image=value.getEntry("image").getValue();if(value.hasEntry("taaDepth"))output.taaDepth=value.getEntry("taaDepth").getValue();if(value.hasEntry("AntiAliasing")){auto const& aa=value.getEntry("AntiAliasing");rejectUnknown(aa,{"msaa","ssaa","taa","fxaa"},"Outputs/Output/AntiAliasing");if(aa.hasEntry("msaa"))output.antiAliasing.msaa=antiAliasingSamples(aa.getEntry("msaa").getValue());if(aa.hasEntry("ssaa"))output.antiAliasing.ssaa=antiAliasingSamples(aa.getEntry("ssaa").getValue());if(aa.hasEntry("taa"))output.antiAliasing.taa=antiAliasingBoolean(aa.getEntry("taa").getValue());if(aa.hasEntry("fxaa"))output.antiAliasing.fxaa=antiAliasingBoolean(aa.getEntry("fxaa").getValue());}document.outputs.push_back(std::move(output));
		}
		if (data.hasEntry("Environment"))
		{
			auto const& environment = data.getEntry("Environment");
			rejectUnknown(environment,{"binding","irradiance","prefilteredSpecular","brdfLut","background","hdrEquirectangular","environmentResolution","irradianceResolution","prefilterResolution"},"Environment");
			auto read = [&](char const* key) { return environment.hasEntry(key) ? environment.getEntry(key).getValue() : string(); };
			document.environment.binding = read("binding"); document.environment.irradiance = read("irradiance");
			document.environment.prefilteredSpecular = read("prefilteredSpecular"); document.environment.brdfLut = read("brdfLut"); document.environment.background = read("background"); document.environment.hdrEquirectangular = read("hdrEquirectangular"); if(environment.hasEntry("environmentResolution"))document.environment.environmentResolution=utils::StringUtils::parseUInt(environment.getEntry("environmentResolution").getValue()); if(environment.hasEntry("irradianceResolution"))document.environment.irradianceResolution=utils::StringUtils::parseUInt(environment.getEntry("irradianceResolution").getValue()); if(environment.hasEntry("prefilterResolution"))document.environment.prefilterResolution=utils::StringUtils::parseUInt(environment.getEntry("prefilterResolution").getValue());
		}
		if (data.hasEntry("Bloom"))
		{
			auto const& bloom=data.getEntry("Bloom");rejectUnknown(bloom,{"enabled","blurPasses"},"Bloom");if(bloom.hasEntry("enabled"))document.bloom.enabled=boolean(bloom.getEntry("enabled").getValue());if(bloom.hasEntry("blurPasses"))document.bloom.blurPasses=utils::StringUtils::parseUInt(bloom.getEntry("blurPasses").getValue());
		}
		if (data.hasEntry("AmbientOcclusion"))
		{
			auto const& ao=data.getEntry("AmbientOcclusion");rejectUnknown(ao,{"method","SSAO","GTAO"},"AmbientOcclusion");if(ao.hasEntry("method"))document.ambientOcclusion.method=ambientOcclusionMethod(ao.getEntry("method").getValue());
			if(ao.hasEntry("SSAO")){auto const& value=ao.getEntry("SSAO");rejectUnknown(value,{"radius","intensity","bias","power","sampleCount","blurRadius"},"AmbientOcclusion/SSAO");auto& options=document.ambientOcclusion.ssao;if(value.hasEntry("radius"))options.radius=utils::StringUtils::parseFloat(value.getEntry("radius").getValue());if(value.hasEntry("intensity"))options.intensity=utils::StringUtils::parseFloat(value.getEntry("intensity").getValue());if(value.hasEntry("bias"))options.bias=utils::StringUtils::parseFloat(value.getEntry("bias").getValue());if(value.hasEntry("power"))options.power=utils::StringUtils::parseFloat(value.getEntry("power").getValue());if(value.hasEntry("sampleCount"))options.sampleCount=utils::StringUtils::parseInt(value.getEntry("sampleCount").getValue());if(value.hasEntry("blurRadius"))options.blurRadius=utils::StringUtils::parseInt(value.getEntry("blurRadius").getValue());}
			if(ao.hasEntry("GTAO")){auto const& value=ao.getEntry("GTAO");rejectUnknown(value,{"radius","intensity","thickness","horizonBias","falloffStart","falloffEnd","sliceCount","stepsPerSlice","power","blurRadius","normalSource"},"AmbientOcclusion/GTAO");auto& options=document.ambientOcclusion.gtao;if(value.hasEntry("radius"))options.radius=utils::StringUtils::parseFloat(value.getEntry("radius").getValue());if(value.hasEntry("intensity"))options.intensity=utils::StringUtils::parseFloat(value.getEntry("intensity").getValue());if(value.hasEntry("thickness"))options.thickness=utils::StringUtils::parseFloat(value.getEntry("thickness").getValue());if(value.hasEntry("horizonBias"))options.horizonBias=utils::StringUtils::parseFloat(value.getEntry("horizonBias").getValue());if(value.hasEntry("falloffStart"))options.falloffStart=utils::StringUtils::parseFloat(value.getEntry("falloffStart").getValue());if(value.hasEntry("falloffEnd"))options.falloffEnd=utils::StringUtils::parseFloat(value.getEntry("falloffEnd").getValue());if(value.hasEntry("sliceCount"))options.sliceCount=utils::StringUtils::parseInt(value.getEntry("sliceCount").getValue());if(value.hasEntry("stepsPerSlice"))options.stepsPerSlice=utils::StringUtils::parseInt(value.getEntry("stepsPerSlice").getValue());if(value.hasEntry("power"))options.power=utils::StringUtils::parseFloat(value.getEntry("power").getValue());if(value.hasEntry("blurRadius"))options.blurRadius=utils::StringUtils::parseInt(value.getEntry("blurRadius").getValue());if(value.hasEntry("normalSource"))options.normalSource=gtaoNormalSource(value.getEntry("normalSource").getValue());}
		}
		else if (data.hasEntry("SSAO"))
		{
			auto const& value=data.getEntry("SSAO");rejectUnknown(value,{"enabled","radius","intensity","bias","power","sampleCount","blurRadius"},"SSAO");auto& options=document.ambientOcclusion.ssao;if(value.hasEntry("enabled")&&boolean(value.getEntry("enabled").getValue()))document.ambientOcclusion.method=AmbientOcclusionMethod::Ssao;if(value.hasEntry("radius"))options.radius=utils::StringUtils::parseFloat(value.getEntry("radius").getValue());if(value.hasEntry("intensity"))options.intensity=utils::StringUtils::parseFloat(value.getEntry("intensity").getValue());if(value.hasEntry("bias"))options.bias=utils::StringUtils::parseFloat(value.getEntry("bias").getValue());if(value.hasEntry("power"))options.power=utils::StringUtils::parseFloat(value.getEntry("power").getValue());if(value.hasEntry("sampleCount"))options.sampleCount=utils::StringUtils::parseInt(value.getEntry("sampleCount").getValue());if(value.hasEntry("blurRadius"))options.blurRadius=utils::StringUtils::parseInt(value.getEntry("blurRadius").getValue());
		}
		if (data.hasEntry("PreviewBindings"))
			for (auto const& entry : data.getEntry("PreviewBindings")) { if(entry.first!="Material")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in PreviewBindings.",__LINE__,__FILE__,__func__);rejectUnknown(entry.second,{"binding","resource"},"PreviewBindings/Material");document.previewBindings.push_back({ entry.second.getEntry("binding").getValue(), entry.second.getEntry("resource").getValue() }); }
		if(data.hasEntry("PreviewOverrides"))for(auto const& entry:data.getEntry("PreviewOverrides"))
		{
			if(entry.first!="Override")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in PreviewOverrides.",__LINE__,__FILE__,__func__);auto const& value=entry.second;rejectUnknown(value,{"model","binding","Values"},"PreviewOverrides/Override");PbrPreviewOverride result;result.modelId=value.getEntry("model").getValue();result.binding=value.getEntry("binding").getValue();if(value.hasEntry("Values"))parseUniforms(value.getEntry("Values"),result.values);document.previewOverrides.push_back(result);
		}
		if(data.hasEntry("Extensions"))for(auto const& entry:data.getEntry("Extensions"))
		{
			if(entry.first!="Extension")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in Extensions.",__LINE__,__FILE__,__func__);rejectUnknown(entry.second,{"namespace","Payload"},"Extensions/Extension");if(!entry.second.hasEntry("namespace")||!entry.second.hasEntry("Payload"))THROW_MPP_RESOURCE_PARSERS("Pipeline extension requires namespace and Payload.",__LINE__,__FILE__,__func__);PbrPipelineExtensionDocument extension;extension.nameSpace=entry.second.getEntry("namespace").getValue();extension.payload=entry.second.getEntry("Payload");document.extensions.push_back(extension);
		}
		if (!data.hasEntry("RenderGraph")) THROW_MPP_RESOURCE_PARSERS("PbrPipeline has no embedded RenderGraph: " + filepath, __LINE__, __FILE__, __func__);
		document.graph = make_shared<RenderGraph>(RenderGraphParser::fromData(data.getEntry("RenderGraph"), filepath));
		// Named outputs are consumed by the host-side output processor even when
		// they are not swapchain images. Mark them as compiler roots so dead-pass
		// elimination cannot discard their producers.
		auto exportImage = [&](string const& name)
		{
			if (name.empty()) return;
			for (uint32_t image = 0; image < document.graph->getImageCount(); ++image)
			{
				auto handle = GraphImageHandle{ image, 0 }; auto info = document.graph->getImageInfo(handle);
				if (info.name != name) continue;
				info.desc.usage = info.desc.usage | GraphImageUsage::Exported;
				document.graph->setImageDesc(handle, info.desc); return;
			}
		};
		for (auto const& output : document.outputs) { exportImage(output.image); exportImage(output.taaDepth); }
		document.setBloomEnabled(document.bloom.enabled);
		document.setAmbientOcclusionMethod(document.ambientOcclusion.method);
		return document;
	}
}
