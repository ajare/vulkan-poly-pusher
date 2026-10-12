#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

#include "mpp/Caps.h"
#include "mpp/LegacyPipelineDocument.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"

using namespace std;

namespace mpp
{
	void LegacyPipelineDocument::setAmbientOcclusionMethod(AmbientOcclusionMethod method)
	{
		ambientOcclusion.method = method;
		if (!graph) return;

		auto findImage = [&](string const& name)
		{
			for (uint32_t image = 0; image < graph->getImageCount(); ++image)
				if (graph->getImageInfo({ image, 0 }).name == name)
					return GraphImageHandle{ image, (uint32_t)graph->getImageVersionCount(image) - 1 };
			return GraphImageHandle{};
		};
		auto removeGeneratedMrtNormals = [&]
		{
			auto normals = findImage("LegacyGtaoMrtNormals");
			auto reserved = findImage("LegacyGtaoMrtReserved1");
			for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
			{
				auto info = graph->getPassInfo({ pass });
				if (info.callbackFactory != "MPP.LegacyScene") continue;
				for (uint32_t output = (uint32_t)info.colourOutputs.size(); output-- > 0; )
				{
					auto image = info.colourOutputs[output].image;
					if ((normals.isValid() && image.id == normals.id) || (reserved.isValid() && image.id == reserved.id))
						graph->removeColourOutput({ pass }, output);
				}
			}
			for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
			{
				auto info = graph->getPassInfo({ pass });
				if (info.callbackFactory != "MPP.GTAORaw") continue;
				for (size_t binding = info.samplerBindings.size(); binding-- > 0; )
					if (info.samplerBindings[binding].sampler == "NORMALS" && normals.isValid() && info.samplerBindings[binding].image.id == normals.id)
						graph->removeSamplerBinding({ pass }, binding);
			}
			if (normals.isValid()) graph->removeImage({ normals.id, 0 });
			if (reserved.isValid()) graph->removeImage({ reserved.id, 0 });
		};

		removeGeneratedMrtNormals();
		if (method != AmbientOcclusionMethod::Gtao || ambientOcclusion.gtao.normalSource != GTAONormalSource::Mrt)
		{
			if (method == AmbientOcclusionMethod::Gtao)
				for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
				{
					auto info = graph->getPassInfo({ pass });
					if (info.callbackFactory != "MPP.GTAORaw") continue;
					auto parameters = info.parameters;
					parameters.setUniform("NORMAL_SOURCE", int32_t{ 0 });
					graph->setPassParameters({ pass }, parameters);
				}
			return;
		}

		GraphPassHandle scenePass, rawPass;
		GraphImageHandle sceneColour;
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({ pass });
			if (info.callbackFactory == "MPP.LegacyScene" && !info.colourOutputs.empty())
			{
				scenePass = { pass };
				sceneColour = info.colourOutputs.front().image;
			}
			else if (info.callbackFactory == "MPP.GTAORaw") rawPass = { pass };
		}
		if (!scenePass.isValid() || !rawPass.isValid()) return;

		auto colourDesc = graph->getImageInfo({ sceneColour.id, 0 }).desc;
		if (graph->getPassInfo(scenePass).colourOutputs.size() < 2)
		{
			GraphImageDesc placeholderDesc = colourDesc;
			placeholderDesc.format = GraphImageFormat::Rgba16f;
			placeholderDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			placeholderDesc.external = false;
			placeholderDesc.transient = true;
			auto placeholder = graph->createImage("LegacyGtaoMrtReserved1", placeholderDesc);
			graph->writeColour(scenePass, placeholder, GraphLoadOp::Clear, GraphStoreOp::DontCare);
		}
		GraphImageDesc normalsDesc = colourDesc;
		normalsDesc.format = GraphImageFormat::Rg16f;
		normalsDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
		normalsDesc.external = false;
		normalsDesc.transient = true;
		auto normals = graph->createImage("LegacyGtaoMrtNormals", normalsDesc);
		normals = graph->writeColour(scenePass, normals, GraphLoadOp::Clear, GraphStoreOp::Store);
		auto rawInfo = graph->getPassInfo(rawPass);
		bool boundNormals = false;
		for (size_t binding = 0; binding < rawInfo.samplerBindings.size(); ++binding)
			if (rawInfo.samplerBindings[binding].sampler == "NORMALS")
			{
				graph->setSamplerBinding(rawPass, binding, "NORMALS", normals);
				boundNormals = true;
			}
		if (!boundNormals) graph->bindSampler(rawPass, "NORMALS", normals);
		auto parameters = rawInfo.parameters;
		parameters.setUniform("NORMAL_SOURCE", int32_t{ 1 });
		graph->setPassParameters(rawPass, parameters);
	}

	DiagnosticBag LegacyPipelineDocument::validate(RenderGraphPassFactoryRegistry const* registry) const
	{
		DiagnosticBag diagnostics;
		if (version != CurrentVersion) diagnostics.error("MPP-LEGACY-PIPELINE-001", "Unsupported LegacyPipeline document version.", { sourcePath });
		if (name.empty()) diagnostics.error("MPP-LEGACY-PIPELINE-002", "LegacyPipeline name is required.", { sourcePath }, "pipeline");
		if (!graph) diagnostics.error("MPP-LEGACY-PIPELINE-003", "LegacyPipeline render graph is required.", { sourcePath }, "graph");
		else
		{
			auto compiled = graph->compile();
			for (auto const& message : compiled.diagnostics) diagnostics.error("MPP-LEGACY-PIPELINE-004", message, { sourcePath }, "graph");
			if (registry) diagnostics.append(registry->validate(*graph));
			uint32_t horizontal=0,vertical=0,extract=0,composite=0,ssaoRaw=0,gtaoRaw=0;for(uint32_t pass=0;pass<graph->getPassCount();++pass){auto factory=graph->getPassInfo({pass}).callbackFactory;if(factory=="MPP.BloomBlurHorizontal")++horizontal;else if(factory=="MPP.BloomBlurVertical")++vertical;else if(factory=="MPP.BloomExtract")++extract;else if(factory=="MPP.BloomComposite")++composite;else if(factory=="MPP.SSAORaw")++ssaoRaw;else if(factory=="MPP.GTAORaw")++gtaoRaw;}auto available=std::min(horizontal,vertical);if(bloom.blurPasses>64)diagnostics.error("MPP-LEGACY-PIPELINE-030","Bloom blur-pass count cannot exceed 64.",{sourcePath},"bloom");if(bloom.enabled&&(extract==0||composite==0))diagnostics.error("MPP-LEGACY-PIPELINE-031","Enabled bloom requires extract and composite passes.",{sourcePath},"bloom");if(bloom.enabled&&bloom.blurPasses>available)diagnostics.error("MPP-LEGACY-PIPELINE-032","Bloom requests "+std::to_string(bloom.blurPasses)+" blur pass(es), but the graph authors only "+std::to_string(available)+" horizontal/vertical pair(s).",{sourcePath},"bloom");if(ambientOcclusion.method==AmbientOcclusionMethod::Ssao&&ssaoRaw==0)diagnostics.error("MPP-LEGACY-PIPELINE-038","Selected SSAO requires an SSAO raw pass.",{sourcePath},"ambientOcclusion");if(ambientOcclusion.method==AmbientOcclusionMethod::Gtao&&gtaoRaw==0)diagnostics.error("MPP-LEGACY-PIPELINE-039","Selected GTAO requires a GTAO raw pass.",{sourcePath},"ambientOcclusion");

			if (ambientOcclusion.method == AmbientOcclusionMethod::Ssao)
			{
				auto const& options = ambientOcclusion.ssao;
				if (!std::isfinite(options.radius) || options.radius < 0.0f || !std::isfinite(options.intensity) || options.intensity < 0.0f || !std::isfinite(options.bias) || options.bias < 0.0f || !std::isfinite(options.power) || options.power <= 0.0f || options.sampleCount < 1 || options.sampleCount > 64 || options.blurRadius < 0 || options.blurRadius > 8) diagnostics.error("MPP-LEGACY-PIPELINE-040", "SSAO parameters are outside their supported ranges.", {sourcePath}, "ambientOcclusion");
			}
			else if (ambientOcclusion.method == AmbientOcclusionMethod::Gtao)
			{
				auto const& options = ambientOcclusion.gtao;
				bool valid = std::isfinite(options.radius) && options.radius >= 0.0f && std::isfinite(options.intensity) && options.intensity >= 0.0f && std::isfinite(options.thickness) && options.thickness >= 0.0f && std::isfinite(options.horizonBias) && options.horizonBias >= 0.0f && options.horizonBias <= 1.0f && std::isfinite(options.falloffStart) && std::isfinite(options.falloffEnd) && options.falloffStart >= 0.0f && options.falloffStart < options.falloffEnd && options.falloffEnd <= 1.0f && options.sliceCount >= 1 && options.sliceCount <= 16 && options.stepsPerSlice >= 1 && options.stepsPerSlice <= 16 && std::isfinite(options.power) && options.power > 0.0f && options.blurRadius >= 0 && options.blurRadius <= 8;
				if (!valid) diagnostics.error("MPP-LEGACY-PIPELINE-040", "GTAO parameters are outside their supported ranges.", {sourcePath}, "ambientOcclusion");
				if (options.normalSource == GTAONormalSource::Mrt)
				{
					GraphImageHandle normals;
					bool sceneContract = false;
					for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
					{
						auto const& info = graph->getPassInfo({ pass });
						if (info.callbackFactory != "MPP.LegacyScene") continue;
						if (info.colourOutputs.size() >= 3 && graph->getImageInfo(info.colourOutputs[2].image).desc.format == GraphImageFormat::Rg16f)
						{
							normals = info.colourOutputs[2].image;
							sceneContract = true;
						}
						break;
					}
					if (!sceneContract)
						diagnostics.error("MPP-LEGACY-PIPELINE-046", "GTAO normalSource=mrt requires the legacy scene shader contract: an RG16F octahedrally encoded view-space shading-normal output at colour location 2 (locations 0 and 1 must also be attached). Update the scene shader/graph or select normalSource=depth.", {sourcePath}, "ambientOcclusion");
					bool rawContract = false;
					for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
					{
						auto const& info = graph->getPassInfo({ pass });
						if (info.callbackFactory != "MPP.GTAORaw") continue;
						for (auto const& binding : info.samplerBindings)
							if (binding.sampler == "NORMALS" && sceneContract && binding.image.id == normals.id) rawContract = true;
					}
					if (!rawContract)
						diagnostics.error("MPP-LEGACY-PIPELINE-047", "GTAO normalSource=mrt requires its raw pass to bind the legacy scene location-2 RG16F normals image as sampler NORMALS; no depth fallback is used.", {sourcePath}, "ambientOcclusion");
				}
			}

			if(outputs.empty())diagnostics.error("MPP-LEGACY-PIPELINE-033","LegacyPipeline requires at least one explicit named output.",{sourcePath},"outputs");
			set<string> outputNames;
			auto findImage=[&](string const& requested)->std::optional<GraphImageInfo>{for(uint32_t image=0;image<graph->getImageCount();++image){auto info=graph->getImageInfo({image,0});if(info.name==requested)return info;}return std::nullopt;};
			for(auto const& output:outputs)
			{
				if(output.name.empty()||!outputNames.insert(output.name).second)diagnostics.error("MPP-LEGACY-PIPELINE-034","Output names must be non-empty and unique.",{sourcePath},output.name);
				if(output.image.empty()){diagnostics.error("MPP-LEGACY-PIPELINE-035","Output '"+output.name+"' requires an image.",{sourcePath},output.name);continue;}
				auto image=findImage(output.image);if(!image){diagnostics.error("MPP-LEGACY-PIPELINE-036","Output '"+output.name+"' references unknown image '"+output.image+"'.",{sourcePath},output.name);continue;}
				bool depth=image->desc.format>=GraphImageFormat::Depth16;if(depth||!hasGraphImageUsage(image->desc.usage,GraphImageUsage::ColourAttachment)||!hasGraphImageUsage(image->desc.usage,GraphImageUsage::Sampled))diagnostics.error("MPP-LEGACY-PIPELINE-037","Output '"+output.name+"' must reference a sampled colour-attachment image.",{sourcePath},output.name);
			}
		}
		set<string> libraries;
		for (auto const& library : resourceLibraries)
		{
			if (library.empty() || !libraries.insert(library).second) diagnostics.error("MPP-LEGACY-PIPELINE-005", "Resource library paths must be non-empty and unique.", { sourcePath }, "resources");
			else { auto path=std::filesystem::path(library);if(path.is_absolute())diagnostics.warning("MPP-LEGACY-PIPELINE-014","Absolute resource-library path is not portable.",{sourcePath},library);auto resolved=path.is_absolute()?path:std::filesystem::path(sourcePath).parent_path()/path;if(!std::filesystem::exists(resolved))diagnostics.error("MPP-LEGACY-PIPELINE-015","Resource library does not exist: "+resolved.string(),{sourcePath},library); }
		}
		auto kindName=[](LegacyPipelineResourceKind kind){switch(kind){case LegacyPipelineResourceKind::BasicMaterial:return "BasicMaterial";case LegacyPipelineResourceKind::Program:return "Program";case LegacyPipelineResourceKind::Texture:return "Texture";case LegacyPipelineResourceKind::Sampler:return "Sampler";case LegacyPipelineResourceKind::PostEffectMaterial:return "PostEffectMaterial";default:return "ParticleEffect";}};
		set<string> localNames;
		for(auto const& resource:localResources)
		{
			if(resource.name.empty()||!localNames.insert(resource.name).second)diagnostics.error("MPP-LEGACY-PIPELINE-022","Local resource names must be non-empty and unique.",{sourcePath},resource.name);
			if(resource.definition.getName()!=kindName(resource.kind))diagnostics.error("MPP-LEGACY-PIPELINE-023","Local resource '"+resource.name+"' payload type does not match its declared kind.",{sourcePath},resource.name);
		}
		set<string> externalNames;
		for(auto const& external:externalResources){auto qualified=external.libraryName+"::"+external.resource.name;if(external.libraryName.empty()||external.resource.name.empty())diagnostics.error("MPP-LEGACY-PIPELINE-025","External resource library and resource names are required.",{external.libraryPath},qualified);else if(!externalNames.insert(qualified).second)diagnostics.error("MPP-LEGACY-PIPELINE-026","Duplicate qualified external resource '"+qualified+"'.",{external.libraryPath},qualified);if(!external.readOnly)diagnostics.error("MPP-LEGACY-PIPELINE-027","External library resources must be read-only.",{external.libraryPath},qualified);}
		auto resourceReferenceIsResolvable=[&](string const& name){return localNames.contains(name)||externalNames.contains(name)||name.find('/')!=string::npos;};
		set<string> importIds;
		for(auto const& import:imports)
		{
			if(import.id.empty()||import.semantic.empty()||!importIds.insert(import.id).second) diagnostics.error("MPP-LEGACY-PIPELINE-010","Import IDs must be non-empty and unique and require a semantic.",{sourcePath},import.id);
			if(!import.required&&import.fallback.empty()) diagnostics.error("MPP-LEGACY-PIPELINE-011","Optional import '"+import.id+"' requires an explicit fallback.",{sourcePath},import.id);
			bool matched=false;
			if(graph) for(auto handle:graph->getImportedImages()){auto info=graph->getImageInfo(handle);if(info.importName==import.id||info.importName==import.semantic){matched=true;if(info.desc.format!=import.format||((uint32_t)info.desc.usage&(uint32_t)import.usage)!=(uint32_t)import.usage)diagnostics.error("MPP-LEGACY-PIPELINE-012","Import '"+import.id+"' graph descriptor is incompatible with its typed contract.",{sourcePath},import.id);}}
			if(!matched) diagnostics.warning("MPP-LEGACY-PIPELINE-013","Typed import '"+import.id+"' is not referenced by the graph.",{sourcePath},import.id);
		}
		set<string> bindings;
		for (auto const& binding : previewBindings)
		{
			if (binding.binding.empty() || binding.materialResource.empty()) diagnostics.error("MPP-LEGACY-PIPELINE-006", "Preview material binding and resource are required.", { sourcePath }, binding.binding);
			else if (!bindings.insert(binding.binding).second) diagnostics.error("MPP-LEGACY-PIPELINE-007", "Duplicate preview material binding '" + binding.binding + "'.", { sourcePath }, binding.binding);
			else if(!resourceReferenceIsResolvable(binding.materialResource))diagnostics.error("MPP-LEGACY-PIPELINE-024","Preview binding resource '"+binding.materialResource+"' is neither document-local nor externally qualified.",{sourcePath},binding.binding);
		}
		set<string> extensionNamespaces;for(auto const& extension:extensions)if(extension.nameSpace.empty()||!extensionNamespaces.insert(extension.nameSpace).second)diagnostics.error("MPP-LEGACY-PIPELINE-028","Extension namespaces must be non-empty and unique.",{sourcePath},extension.nameSpace);
		set<string> overrideTargets;
		for(auto const& value:previewOverrides)
		{
			auto target=value.modelId+"\n"+value.binding;if(value.modelId.empty()||value.binding.empty())diagnostics.error("MPP-LEGACY-PIPELINE-018","Preview overrides require model and binding IDs.",{sourcePath},value.modelId);else if(!overrideTargets.insert(target).second)diagnostics.error("MPP-LEGACY-PIPELINE-019","Duplicate preview override for model '"+value.modelId+"' and binding '"+value.binding+"'.",{sourcePath},value.modelId);if(!value.binding.empty()&&!bindings.contains(value.binding))diagnostics.error("MPP-LEGACY-PIPELINE-020","Preview override references unknown binding '"+value.binding+"'.",{sourcePath},value.modelId);
			for(auto const& uniform:value.values.getUniformData()){auto const& data=uniform.second;bool supported=data.count==1&&((data.type==program::GLSLType::Int||data.type==program::GLSLType::Bool)||(data.type==program::GLSLType::Float&&data.numElements>=1&&data.numElements<=4));if(!supported)diagnostics.error("MPP-LEGACY-PIPELINE-021","Preview override value '"+uniform.first+"' has an unsupported type or array shape.",{sourcePath},value.modelId);}
		}
		if (previewScene.empty()) diagnostics.warning("MPP-LEGACY-PIPELINE-008", "No preview scene is assigned.", { sourcePath }, "previewScene");
		else {auto path=std::filesystem::path(previewScene);if(path.is_absolute())diagnostics.warning("MPP-LEGACY-PIPELINE-016","Absolute preview-scene path is not portable.",{sourcePath},"previewScene");auto resolved=path.is_absolute()?path:std::filesystem::path(sourcePath).parent_path()/path;if(!std::filesystem::exists(resolved))diagnostics.error("MPP-LEGACY-PIPELINE-017","Preview scene does not exist: "+resolved.string(),{sourcePath},"previewScene");}
		return diagnostics;
	}

	DiagnosticBag LegacyPipelineDocument::validate(Caps const& caps,RenderGraphPassFactoryRegistry const* registry) const
	{
		return validate(caps,glm::uvec2(0),registry);
	}

	DiagnosticBag LegacyPipelineDocument::validate(Caps const& caps,glm::uvec2 const& viewport,RenderGraphPassFactoryRegistry const* registry) const
	{
		auto diagnostics=validate(registry);if(graph){if(ambientOcclusion.method==AmbientOcclusionMethod::Gtao&&ambientOcclusion.gtao.normalSource==GTAONormalSource::Mrt&&(caps.maxColourAttachments<3||caps.maxDrawBuffers<3))diagnostics.error("MPP-LEGACY-PIPELINE-048","GTAO normalSource=mrt requires hardware with at least 3 colour attachments and 3 draw buffers for scene colour (location 0), reserved MRT (location 1), and RG16F normals (location 2); select normalSource=depth on incompatible hardware.",{sourcePath},"ambientOcclusion");auto compiled=graph->compile(caps,viewport);for(auto const& message:compiled.diagnostics)diagnostics.error("MPP-LEGACY-PIPELINE-029",message,{sourcePath},"graph");}return diagnostics;
	}

	DiagnosticBag LegacyPipelineDocument::validateOutputAntiAliasing(AntiAliasingDefaults const& defaults,Caps const* caps) const
	{
		DiagnosticBag diagnostics;if(!graph)return diagnostics;std::optional<AntiAliasingDefaults> shared;
		auto findImage=[&](string const& requested)->std::optional<GraphImageInfo>{for(uint32_t image=0;image<graph->getImageCount();++image){auto info=graph->getImageInfo({image,0});if(info.name==requested)return info;}return std::nullopt;};
		for(auto const& output:outputs)
		{
			auto effective=resolveAntiAliasing(defaults,output.antiAliasing);
			if(shared&&(effective.msaa!=shared->msaa||effective.ssaa!=shared->ssaa||effective.taa!=shared->taa))diagnostics.error("MPP-LEGACY-PIPELINE-042","Output '"+output.name+"' has effective MSAA, SSAA, or TAA settings that differ from another output in the pipeline.",{sourcePath},output.name);else if(!shared)shared=effective;
			if(caps&&!caps->supportsMsaa(antiAliasingSampleCount(effective.msaa)))diagnostics.error("MPP-LEGACY-PIPELINE-043","Output '"+output.name+"' requests unsupported "+antiAliasingSamplesName(effective.msaa)+" MSAA.",{sourcePath},output.name);
			auto image=findImage(output.image);if(!image)continue;
			if(effective.fxaa&&image->desc.format!=GraphImageFormat::Rgba8&&image->desc.format!=GraphImageFormat::Srgb8Alpha8&&image->desc.format!=GraphImageFormat::Rgb10a2)diagnostics.error("MPP-LEGACY-PIPELINE-044","Effective FXAA output '"+output.name+"' requires RGBA8, SRGB8_ALPHA8, or RGB10_A2.",{sourcePath},output.name);
			if(effective.taa&&output.taaDepth.empty()&&!image->desc.external)diagnostics.error("MPP-LEGACY-PIPELINE-045","Effective TAA output '"+output.name+"' requires taaDepth or an external output target with a depth texture.",{sourcePath},output.name);
		}
		return diagnostics;
	}
}
