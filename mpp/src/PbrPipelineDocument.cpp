#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include "mpp/Caps.h"
#include "mpp/PbrPipelineDocument.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"

using namespace std;

namespace mpp
{
	bool PbrPipelineDocument::makeLocalCopy(string const& qualifiedName,string const& localName)
	{
		if(localName.empty())return false;for(auto const& resource:localResources)if(resource.name==localName)return false;auto found=std::find_if(externalResources.begin(),externalResources.end(),[&](auto const& value){return value.libraryName+"::"+value.resource.name==qualifiedName;});if(found==externalResources.end())return false;auto local=found->resource;local.name=localName;mpp::data::StructuredData renamed(local.definition.getName());for(auto const& entry:local.definition){if(entry.first=="name")renamed.addEntry("name",localName);else renamed.addEntry(entry.first,entry.second);}local.definition=renamed;localResources.push_back(local);
		auto rewrite=[&](string& value){if(value==qualifiedName)value=localName;};auto rewriteData=[&](auto&& self,mpp::data::StructuredData& data)->void{if(data.isValue()){auto value=data.getValue();rewrite(value);data.setValue(value);}else for(auto& entry:data)self(self,entry.second);};for(auto& resource:localResources)rewriteData(rewriteData,resource.definition);for(auto& binding:previewBindings)rewrite(binding.materialResource);rewrite(environment.irradiance);rewrite(environment.prefilteredSpecular);rewrite(environment.brdfLut);rewrite(environment.background);for(auto& import:imports)rewrite(import.fallback);if(graph)for(uint32_t id=0;id<graph->getPassCount();++id){auto info=graph->getPassInfo({id});if(info.programResource==qualifiedName)graph->setPassProgramResource({id},localName);}return true;
	}

	void PbrPipelineDocument::setBloomEnabled(bool enabled)
	{
		bloom.enabled = enabled;
		if (!graph) return;
		// Recognizes both the legacy hard-coded bloom passes (callbackFactory
		// MPP.Bloom*/MPP.ToneMapPresent) and a migrated generic post-effect chain
		// (callbackFactory MPP.FullscreenEffect, identified by its
		// programResource -- PostEffectMaterial local resources authored for
		// bloom/tonemap are named with a "Bloom"/"ToneMap" suffix by convention,
		// e.g. "PostEffect.BloomExtract"). This bridge exists only because
		// setBloomEnabled predates the generic chain; it goes away with
		// setBloomEnabled itself once every authored document has migrated (see
		// doc/POST_EFFECT_CHAIN_IMPLEMENTATION_PLAN.md M4).
		auto endsWith = [](string const& value, string const& suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; };
		auto isFullscreenEffect = [&](GraphPassInfo const& info, string const& suffix) { return info.callbackFactory == "MPP.FullscreenEffect" && endsWith(info.programResource, suffix); };
		auto isAnyBloomPass = [&](GraphPassInfo const& info) { return info.callbackFactory.starts_with("MPP.Bloom") || (info.callbackFactory == "MPP.FullscreenEffect" && info.programResource.find("Bloom") != string::npos); };
		auto isBloomExtract = [&](GraphPassInfo const& info) { return info.callbackFactory == "MPP.BloomExtract" || isFullscreenEffect(info, "BloomExtract"); };
		auto isBloomComposite = [&](GraphPassInfo const& info) { return info.callbackFactory == "MPP.BloomComposite" || isFullscreenEffect(info, "BloomComposite"); };
		auto isToneMapPresent = [&](GraphPassInfo const& info) { return info.callbackFactory == "MPP.ToneMapPresent" || isFullscreenEffect(info, "ToneMap"); };
		GraphImageHandle sceneColour, emissive;
		std::vector<GraphImageHandle> bloomCompositeOutputs;
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({pass});
			if (info.callbackFactory == "MPP.PbrScene" && !info.colourOutputs.empty())
			{
				sceneColour = info.colourOutputs.front().image;
				if (enabled && info.colourOutputs.size() < 2)
				{
					GraphImageHandle target;
					for (uint32_t image = 0; image < graph->getImageCount(); ++image)
						if (graph->getImageInfo({image, 0}).name == "SceneEmissive") target = {image, (uint32_t)graph->getImageVersionCount(image) - 1};
					if (!target.isValid())
					{
						GraphImageDesc desc; desc.format = GraphImageFormat::Rgba16f;
						desc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
						target = graph->createImage("SceneEmissive", desc);
					}
					emissive = graph->writeColour({pass}, target, GraphLoadOp::Clear, GraphStoreOp::Store);
				}
				else if (info.colourOutputs.size() >= 2)
				{
					// Keep the authored emissive MRT attached while Bloom is disabled.
					// Bloom enablement is a post-process routing choice; changing the
					// scene framebuffer topology can alter otherwise identical PBR draws.
					emissive = info.colourOutputs[1].image;
				}
			}
			if (isAnyBloomPass(info)) graph->setPassEnabled({pass}, enabled);
		}
		if (!sceneColour.isValid()) return;
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({pass});
			if (isBloomExtract(info) && enabled && emissive.isValid())
			{
				// Removing SceneEmissive while Bloom is disabled removes every
				// dependent sampler binding. Re-enable must restore TEX1, not merely
				// update it when it happened to survive the topology change.
				if (info.samplerBindings.empty())
					graph->bindSampler({pass}, "TEX1", emissive);
				else
					graph->setSamplerBinding({pass}, 0, info.samplerBindings[0].sampler, emissive, info.samplerBindings[0].mipLevel);
			}
			if (isBloomComposite(info) && !info.colourOutputs.empty()) bloomCompositeOutputs.push_back(info.colourOutputs.front().image);
		}
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({pass});
			if (!isToneMapPresent(info) || info.samplerBindings.empty()) continue;
			auto input = !enabled || bloomCompositeOutputs.empty() ? sceneColour : bloomCompositeOutputs.front();
			graph->setSamplerBinding({pass}, 0, info.samplerBindings[0].sampler, input, info.samplerBindings[0].mipLevel);
		}
	}

	void PbrPipelineDocument::setAmbientOcclusionMethod(AmbientOcclusionMethod method)
	{
		ambientOcclusion.method = method;
		if (!graph) return;

		auto isAmbientOcclusionPass = [](GraphPassInfo const& info)
		{
			return info.callbackFactory == "MPP.SSAORaw" || info.callbackFactory == "MPP.GTAORaw" ||
				info.callbackFactory == "MPP.SSAOBlur" || info.callbackFactory == "MPP.SSAOComposite" ||
				info.callbackFactory == "MPP.AmbientOcclusionBlur" || info.callbackFactory == "MPP.AmbientOcclusionComposite";
		};
		auto findImage = [&](string const& name)
		{
			for (uint32_t image = 0; image < graph->getImageCount(); ++image)
				if (graph->getImageInfo({ image, 0 }).name == name) return GraphImageHandle{ image, (uint32_t)graph->getImageVersionCount(image) - 1 };
			return GraphImageHandle{};
		};
		auto removeGeneratedMrtNormals = [&]
		{
			// The location-1 placeholder exists only to make the generated normals
			// target location 2 when Bloom has not already supplied its MRT output.
			// Remove only our named resources and their attachments; author-owned
			// images, bindings, and scene outputs remain untouched.
			auto normals = findImage("GtaoMrtNormals");
			auto reserved = findImage("GtaoMrtReserved1");
			for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
			{
				auto info = graph->getPassInfo({ pass });
				if (info.callbackFactory != "MPP.PbrScene") continue;
				for (uint32_t output = (uint32_t)info.colourOutputs.size(); output-- > 0; )
				{
					auto image = info.colourOutputs[output].image;
					if ((normals.isValid() && image.id == normals.id) ||
						(reserved.isValid() && image.id == reserved.id && !bloom.enabled))
						graph->removeColourOutput({ pass }, output);
				}
			}
			auto isReferenced = [&](GraphImageHandle image)
			{
				for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
				{
					auto info = graph->getPassInfo({ pass });
					if (isAmbientOcclusionPass(info)) continue;
					for (auto const& output : info.colourOutputs) if (output.image.id == image.id) return true;
					for (auto const& binding : info.samplerBindings) if (binding.image.id == image.id) return true;
				}
				return false;
			};
			if (normals.isValid() && !isReferenced(normals)) graph->removeImage({ normals.id, 0 });
			if (reserved.isValid() && !isReferenced(reserved)) graph->removeImage({ reserved.id, 0 });
		};
		if (method == AmbientOcclusionMethod::None)
		{
			removeGeneratedMrtNormals();
			auto composite = findImage("AmbientOcclusionComposite");
			if (!composite.isValid()) composite = findImage("SsaoComposite");
			auto sceneColour = GraphImageHandle{};
			for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
			{
				auto info = graph->getPassInfo({ pass });
				if (info.callbackFactory == "MPP.PbrScene" && !info.colourOutputs.empty()) sceneColour = info.colourOutputs.front().image;
			}
			if (composite.isValid() && sceneColour.isValid())
				for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
				{
					auto info = graph->getPassInfo({ pass });
					if (isAmbientOcclusionPass(info)) continue;
					for (size_t binding = 0; binding < info.samplerBindings.size(); ++binding)
						if (info.samplerBindings[binding].image.id == composite.id)
							graph->setSamplerBinding({ pass }, binding, info.samplerBindings[binding].sampler, sceneColour, info.samplerBindings[binding].mipLevel);
				}
			for (uint32_t pass = (uint32_t)graph->getPassCount(); pass-- > 0; )
				if (isAmbientOcclusionPass(graph->getPassInfo({ pass }))) graph->removePass({ pass });
			for (uint32_t image = (uint32_t)graph->getImageCount(); image-- > 0; )
			{
				auto const& name = graph->getImageInfo({ image, 0 }).name;
				if (name == "SsaoRaw" || name == "SsaoBlur" || name == "SsaoComposite" || name == "AmbientOcclusionRaw" || name == "AmbientOcclusionBlur" || name == "AmbientOcclusionComposite") graph->removeImage({ image, 0 });
			}
			return;
		}

		// Rebuilding instead of layering duplicates keeps repeated enable calls and
		// parse-time normalization deterministic.
		setAmbientOcclusionMethod(AmbientOcclusionMethod::None);
		ambientOcclusion.method = method;
		GraphPassHandle scenePass;
		GraphImageHandle sceneColour, sceneDepth;
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({ pass });
			if (info.callbackFactory != "MPP.PbrScene" || info.colourOutputs.empty() || info.depthOutputs.empty()) continue;
			scenePass = { pass }; sceneColour = info.colourOutputs.front().image; sceneDepth = info.depthOutputs.front().image;
			for (size_t output = 0; output < info.depthOutputs.size(); ++output)
				if (info.depthOutputs[output].image.id == sceneDepth.id)
					graph->setDepthOutput(scenePass, output, info.depthOutputs[output].load, GraphStoreOp::Store, info.depthOutputs[output].clearDepth, info.depthOutputs[output].mipLevel);
			break;
		}
		if (!scenePass.isValid()) return;
		auto depthDesc = graph->getImageInfo({ sceneDepth.id, 0 }).desc;
		depthDesc.usage = depthDesc.usage | GraphImageUsage::Sampled;
		graph->setImageDesc({ sceneDepth.id, 0 }, depthDesc);
		auto colourDesc = graph->getImageInfo({ sceneColour.id, 0 }).desc;
		auto effectDesc = colourDesc;
		effectDesc.format = GraphImageFormat::Rgba8;
		effectDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
		effectDesc.external = false;
		effectDesc.transient = true;
		auto raw = graph->createImage("AmbientOcclusionRaw", effectDesc);
		auto blur = graph->createImage("AmbientOcclusionBlur", effectDesc);
		auto compositeDesc = colourDesc;
		compositeDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
		compositeDesc.external = false;
		compositeDesc.transient = true;
		auto composite = graph->createImage("AmbientOcclusionComposite", compositeDesc);

		UniformCollection rawParameters;
		if (method == AmbientOcclusionMethod::Ssao)
		{
			auto const& options = ambientOcclusion.ssao;
			rawParameters.setUniform("RADIUS", options.radius); rawParameters.setUniform("INTENSITY", options.intensity);
			rawParameters.setUniform("BIAS", options.bias); rawParameters.setUniform("POWER", options.power);
			rawParameters.setUniform("SAMPLE_COUNT", (int32_t)options.sampleCount);
		}
		else
		{
			auto const& options = ambientOcclusion.gtao;
			rawParameters.setUniform("RADIUS", options.radius); rawParameters.setUniform("INTENSITY", options.intensity);
			rawParameters.setUniform("THICKNESS", options.thickness); rawParameters.setUniform("HORIZON_BIAS", options.horizonBias);
			rawParameters.setUniform("FALLOFF_START", options.falloffStart); rawParameters.setUniform("FALLOFF_END", options.falloffEnd);
			rawParameters.setUniform("SLICE_COUNT", (int32_t)options.sliceCount); rawParameters.setUniform("STEPS_PER_SLICE", (int32_t)options.stepsPerSlice);
			rawParameters.setUniform("POWER", options.power);
			rawParameters.setUniform("NORMAL_SOURCE", (int32_t)(options.normalSource == GTAONormalSource::Mrt ? 1 : 0));
		}
		auto rawPass = graph->addPass(method == AmbientOcclusionMethod::Ssao ? "SSAO" : "GTAO", GraphPassType::Fullscreen);
		graph->setPassCallbackFactory(rawPass, method == AmbientOcclusionMethod::Ssao ? "MPP.SSAORaw" : "MPP.GTAORaw");
		graph->bindSampler(rawPass, "DEPTH", sceneDepth);
		if (method == AmbientOcclusionMethod::Gtao && ambientOcclusion.gtao.normalSource == GTAONormalSource::Mrt)
		{
			// Fragment output locations are attachment indices. Preserve location 1
			// for Bloom (or add a disposable placeholder) before attaching the
			// RG16F octahedral view-space normals target at location 2.
			auto sceneInfo = graph->getPassInfo(scenePass);
			if (sceneInfo.colourOutputs.size() < 2)
			{
				GraphImageDesc placeholderDesc = colourDesc;
				placeholderDesc.format = GraphImageFormat::Rgba16f;
				placeholderDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
				placeholderDesc.external = false;
				placeholderDesc.transient = true;
				auto placeholder = graph->createImage("GtaoMrtReserved1", placeholderDesc);
				graph->writeColour(scenePass, placeholder, GraphLoadOp::Clear, GraphStoreOp::DontCare);
			}
			GraphImageDesc normalsDesc = colourDesc;
			normalsDesc.format = GraphImageFormat::Rg16f;
			normalsDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			normalsDesc.external = false;
			normalsDesc.transient = true;
			auto normals = graph->createImage("GtaoMrtNormals", normalsDesc);
			normals = graph->writeColour(scenePass, normals, GraphLoadOp::Clear, GraphStoreOp::Store);
			graph->bindSampler(rawPass, "NORMALS", normals);
		}
		graph->setPassParameters(rawPass, rawParameters);
		raw = graph->writeColour(rawPass, raw);
		auto blurPass = graph->addPass("AmbientOcclusionBlur", GraphPassType::Fullscreen);
		graph->setPassCallbackFactory(blurPass, "MPP.AmbientOcclusionBlur");
		graph->bindSampler(blurPass, "AO", raw);
		graph->bindSampler(blurPass, "DEPTH", sceneDepth);
		UniformCollection blurParameters; blurParameters.setUniform("BLUR_RADIUS", (int32_t)(method == AmbientOcclusionMethod::Ssao ? ambientOcclusion.ssao.blurRadius : ambientOcclusion.gtao.blurRadius));
		graph->setPassParameters(blurPass, blurParameters);
		blur = graph->writeColour(blurPass, blur);
		auto compositePass = graph->addPass("AmbientOcclusionComposite", GraphPassType::Fullscreen);
		graph->setPassCallbackFactory(compositePass, "MPP.AmbientOcclusionComposite");
		graph->bindSampler(compositePass, "SCENE", sceneColour);
		graph->bindSampler(compositePass, "AO", blur);
		composite = graph->writeColour(compositePass, composite);

		auto moveAfterScene = [&](string const& name, uint32_t offset)
		{
			uint32_t scene = UINT32_MAX, pass = UINT32_MAX;
			for (uint32_t index = 0; index < graph->getPassCount(); ++index) { auto info = graph->getPassInfo({ index }); if (info.callbackFactory == "MPP.PbrScene") scene = index; if (info.name == name) pass = index; }
			if (scene != UINT32_MAX && pass != UINT32_MAX) graph->movePass({ pass }, scene + offset);
		};
		moveAfterScene(method == AmbientOcclusionMethod::Ssao ? "SSAO" : "GTAO", 1); moveAfterScene("AmbientOcclusionBlur", 2); moveAfterScene("AmbientOcclusionComposite", 3);
		for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
		{
			auto info = graph->getPassInfo({ pass });
			if (isAmbientOcclusionPass(info)) continue;
			for (size_t binding = 0; binding < info.samplerBindings.size(); ++binding)
				if (info.samplerBindings[binding].image.id == sceneColour.id)
					graph->setSamplerBinding({ pass }, binding, info.samplerBindings[binding].sampler, composite, info.samplerBindings[binding].mipLevel);
		}
	}

	GraphImageHandle PbrPipelineDocument::buildPostEffectChain(GraphImageHandle inputImage, string const& inputImageName)
	{
		if (!graph) return inputImage;

		static constexpr char const* kPassPrefix = "PostEffect:";
		static constexpr char const* kImagePrefix = "PostEffectOutput:";
		auto hasPrefix = [](string const& value, char const* prefix) { return value.rfind(prefix, 0) == 0; };

		// Remove any previously generated chain passes/images so rebuilding --
		// including after reordering `entries` -- regenerates wiring from scratch
		// rather than layering stale passes on top of new ones. Indices renumber
		// on removal (see RenderGraphExecutor.h's handle-vs-index note), so walk
		// backwards.
		for (uint32_t pass = (uint32_t)graph->getPassCount(); pass-- > 0; )
		{
			auto info = graph->getPassInfo({ pass });
			if (hasPrefix(info.name, kPassPrefix)) graph->removePass({ pass });
		}
		for (uint32_t image = (uint32_t)graph->getImageCount(); image-- > 0; )
		{
			auto info = graph->getImageInfo({ image, 0 });
			if (hasPrefix(info.name, kImagePrefix)) graph->removeImage({ image, 0 });
		}

		map<string, GraphImageHandle> outputsByName;
		auto resolveSource = [&](string const& source) -> GraphImageHandle
		{
			auto found = outputsByName.find(source);
			if (found != outputsByName.end()) return found->second;
			if (source == inputImageName) return inputImage;
			for (uint32_t image = 0; image < graph->getImageCount(); ++image)
			{
				auto info = graph->getImageInfo({ image, 0 });
				if (info.name == source) return { image, (uint32_t)graph->getImageVersionCount(image) - 1 };
			}
			return {};
		};

		GraphImageHandle current = inputImage;
		for (auto const& entry : postEffects.entries)
		{
			auto pass = graph->addPass(kPassPrefix + entry.name, GraphPassType::Fullscreen);
			graph->setPassCallbackFactory(pass, "MPP.FullscreenEffect");
			graph->setPassProgramResource(pass, entry.material);
			graph->bindSampler(pass, "TEX0", current);
			for (auto const& [slot, source] : entry.extraSamplerBindings)
			{
				auto resolved = resolveSource(source);
				if (resolved.isValid()) graph->bindSampler(pass, slot, resolved);
			}

			UniformCollection parameters;
			parameters.setUniform("ENABLED", entry.enabled ? 1 : 0);
			graph->setPassParameters(pass, parameters);

			auto const inputDesc = graph->getImageInfo(current).desc;
			GraphImageDesc outputDesc = inputDesc;
			outputDesc.relativeSize *= entry.outputScale;
			outputDesc.absoluteSize = glm::uvec2(glm::vec2(inputDesc.absoluteSize) * entry.outputScale);
			outputDesc.mipLevels = 1;
			outputDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			if (!entry.inheritFormat) outputDesc.format = entry.outputFormat;
			auto outputImage = graph->createImage(kImagePrefix + entry.name, outputDesc);
			auto output = graph->writeColour(pass, outputImage, GraphLoadOp::DontCare, GraphStoreOp::Store);

			outputsByName[entry.name] = output;
			current = output;
		}
		return current;
	}

	DiagnosticBag PbrPipelineDocument::validate(RenderGraphPassFactoryRegistry const* registry) const
	{
		DiagnosticBag diagnostics;
		if (version != CurrentVersion) diagnostics.error("MPP-PIPELINE-001", "Unsupported PbrPipeline document version.", { sourcePath });
		if (name.empty()) diagnostics.error("MPP-PIPELINE-002", "PbrPipeline name is required.", { sourcePath }, "pipeline");
		if (!graph) diagnostics.error("MPP-PIPELINE-003", "PbrPipeline render graph is required.", { sourcePath }, "graph");
		else
		{
			auto compiled = graph->compile();
			for (auto const& message : compiled.diagnostics) diagnostics.error("MPP-PIPELINE-004", message, { sourcePath }, "graph");
			if (registry) diagnostics.append(registry->validate(*graph));
			// Recognizes both the legacy hard-coded bloom passes (callbackFactory
			// MPP.Bloom*) and a migrated generic post-effect chain (callbackFactory
			// MPP.FullscreenEffect, identified by its programResource suffix; see
			// the identical bridge in setBloomEnabled above).
			auto endsWith=[](string const& value,string const& suffix){return value.size()>=suffix.size()&&value.compare(value.size()-suffix.size(),suffix.size(),suffix)==0;};
			auto isFullscreenEffect=[&](GraphPassInfo const& info,string const& suffix){return info.callbackFactory=="MPP.FullscreenEffect"&&endsWith(info.programResource,suffix);};
			uint32_t horizontal=0,vertical=0,extract=0,composite=0;for(uint32_t pass=0;pass<graph->getPassCount();++pass){auto info=graph->getPassInfo({pass});auto const& factory=info.callbackFactory;if(factory=="MPP.BloomBlurHorizontal"||isFullscreenEffect(info,"BloomBlurHorizontal"))++horizontal;else if(factory=="MPP.BloomBlurVertical"||isFullscreenEffect(info,"BloomBlurVertical"))++vertical;else if(factory=="MPP.BloomExtract"||isFullscreenEffect(info,"BloomExtract"))++extract;else if(factory=="MPP.BloomComposite"||isFullscreenEffect(info,"BloomComposite"))++composite;}auto available=std::min(horizontal,vertical);if(bloom.blurPasses>64)diagnostics.error("MPP-PIPELINE-030","Bloom blur-pass count cannot exceed 64.",{sourcePath},"bloom");if(bloom.enabled&&(extract==0||composite==0))diagnostics.error("MPP-PIPELINE-031","Enabled bloom requires extract and composite passes.",{sourcePath},"bloom");if(bloom.enabled&&bloom.blurPasses>available)diagnostics.error("MPP-PIPELINE-032","Bloom requests "+std::to_string(bloom.blurPasses)+" blur pass(es), but the graph authors only "+std::to_string(available)+" horizontal/vertical pair(s).",{sourcePath},"bloom");

			if (ambientOcclusion.method == AmbientOcclusionMethod::Ssao)
			{
				auto const& options = ambientOcclusion.ssao;
				if (!std::isfinite(options.radius) || options.radius < 0.0f || !std::isfinite(options.intensity) || options.intensity < 0.0f || !std::isfinite(options.bias) || options.bias < 0.0f || !std::isfinite(options.power) || options.power <= 0.0f || options.sampleCount < 1 || options.sampleCount > 64 || options.blurRadius < 0 || options.blurRadius > 8)
					diagnostics.error("MPP-PIPELINE-053", "SSAO parameters require finite non-negative radius, intensity, and bias; positive power; 1-64 samples; and blur radius 0-8.", {sourcePath}, "ambientOcclusion");
			}
			else if (ambientOcclusion.method == AmbientOcclusionMethod::Gtao)
			{
				auto const& options = ambientOcclusion.gtao;
				bool valid = std::isfinite(options.radius) && options.radius >= 0.0f && std::isfinite(options.intensity) && options.intensity >= 0.0f && std::isfinite(options.thickness) && options.thickness >= 0.0f && std::isfinite(options.horizonBias) && options.horizonBias >= 0.0f && options.horizonBias <= 1.0f && std::isfinite(options.falloffStart) && std::isfinite(options.falloffEnd) && options.falloffStart >= 0.0f && options.falloffStart < options.falloffEnd && options.falloffEnd <= 1.0f && options.sliceCount >= 1 && options.sliceCount <= 16 && options.stepsPerSlice >= 1 && options.stepsPerSlice <= 16 && std::isfinite(options.power) && options.power > 0.0f && options.blurRadius >= 0 && options.blurRadius <= 8;
				if (!valid) diagnostics.error("MPP-PIPELINE-053", "GTAO parameters require finite non-negative radius, intensity, and thickness; horizon bias 0-1; ordered falloff in [0,1]; 1-16 slices and steps; positive power; and blur radius 0-8.", {sourcePath}, "ambientOcclusion");
				if (options.normalSource == GTAONormalSource::Mrt)
				{
					GraphImageHandle normals;
					bool sceneContract = false;
					for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
					{
						auto const& info = graph->getPassInfo({ pass });
						if (info.callbackFactory != "MPP.PbrScene") continue;
						if (info.colourOutputs.size() >= 3 && graph->getImageInfo(info.colourOutputs[2].image).desc.format == GraphImageFormat::Rg16f)
						{
							normals = info.colourOutputs[2].image;
							sceneContract = true;
						}
						break;
					}
					if (!sceneContract)
						diagnostics.error("MPP-PIPELINE-054", "GTAO normalSource=mrt requires the PBR scene shader contract: an RG16F octahedrally encoded view-space shading-normal output at colour location 2 (locations 0 and 1 must also be attached). Update the scene shader/graph or select normalSource=depth.", {sourcePath}, "ambientOcclusion");
					bool rawContract = false;
					for (uint32_t pass = 0; pass < graph->getPassCount(); ++pass)
					{
						auto const& info = graph->getPassInfo({ pass });
						if (info.callbackFactory != "MPP.GTAORaw") continue;
						for (auto const& binding : info.samplerBindings)
							if (binding.sampler == "NORMALS" && sceneContract && binding.image.id == normals.id) rawContract = true;
					}
					if (!rawContract)
						diagnostics.error("MPP-PIPELINE-055", "GTAO normalSource=mrt requires its raw pass to bind the PBR scene location-2 RG16F normals image as sampler NORMALS; no depth fallback is used.", {sourcePath}, "ambientOcclusion");
				}
			}

			if(outputs.empty())diagnostics.error("MPP-PIPELINE-033","PbrPipeline requires at least one explicit named output.",{sourcePath},"outputs");
			set<string> outputNames;
			auto findImage=[&](string const& requested)->std::optional<GraphImageInfo>{for(uint32_t image=0;image<graph->getImageCount();++image){auto info=graph->getImageInfo({image,0});if(info.name==requested)return info;}return std::nullopt;};
			for(auto const& output:outputs)
			{
				if(output.name.empty()||!outputNames.insert(output.name).second)diagnostics.error("MPP-PIPELINE-034","Output names must be non-empty and unique.",{sourcePath},output.name);
				if(output.image.empty()){diagnostics.error("MPP-PIPELINE-035","Output '"+output.name+"' requires an image.",{sourcePath},output.name);continue;}
				auto image=findImage(output.image);if(!image){diagnostics.error("MPP-PIPELINE-036","Output '"+output.name+"' references unknown image '"+output.image+"'.",{sourcePath},output.name);continue;}
				bool depth=image->desc.format>=GraphImageFormat::Depth16;if(depth||!hasGraphImageUsage(image->desc.usage,GraphImageUsage::ColourAttachment)||!hasGraphImageUsage(image->desc.usage,GraphImageUsage::Sampled))diagnostics.error("MPP-PIPELINE-037","Output '"+output.name+"' must reference a sampled colour-attachment image.",{sourcePath},output.name);
				if(output.antiAliasing.fxaa.value_or(false)&&image->desc.format!=GraphImageFormat::Rgba8&&image->desc.format!=GraphImageFormat::Srgb8Alpha8&&image->desc.format!=GraphImageFormat::Rgb10a2)diagnostics.error("MPP-PIPELINE-039","FXAA output '"+output.name+"' requires RGBA8, SRGB8_ALPHA8, or RGB10_A2.",{sourcePath},output.name);
				if(!output.taaDepth.empty()){auto depthImage=findImage(output.taaDepth);if(!depthImage||depthImage->desc.format<GraphImageFormat::Depth16||!hasGraphImageUsage(depthImage->desc.usage,GraphImageUsage::DepthAttachment)||!hasGraphImageUsage(depthImage->desc.usage,GraphImageUsage::Sampled))diagnostics.error("MPP-PIPELINE-040","TAA depth source '"+output.taaDepth+"' for output '"+output.name+"' must be a sampled depth-attachment image.",{sourcePath},output.name);else{uint32_t depthId=UINT32_MAX;for(uint32_t candidate=0;candidate<graph->getImageCount();++candidate)if(graph->getImageInfo({candidate,0}).name==output.taaDepth){depthId=candidate;break;}bool stored=false;for(uint32_t pass=0;pass<graph->getPassCount();++pass)for(auto const& attachment:graph->getPassInfo({pass}).depthOutputs)if(attachment.image.id==depthId)stored=attachment.store==GraphStoreOp::Store;if(!stored)diagnostics.error("MPP-PIPELINE-046","TAA depth source '"+output.taaDepth+"' must retain its final write with store=store.",{sourcePath},output.name);}}else if(output.antiAliasing.taa.value_or(false)&&!image->desc.external)diagnostics.error("MPP-PIPELINE-041","TAA output '"+output.name+"' requires taaDepth because its output image cannot provide an external target depth attachment.",{sourcePath},output.name);
			}
		}
		set<string> libraries;
		for (auto const& library : resourceLibraries)
		{
			if (library.empty() || !libraries.insert(library).second) diagnostics.error("MPP-PIPELINE-005", "Resource library paths must be non-empty and unique.", { sourcePath }, "resources");
			else { auto path=std::filesystem::path(library);if(path.is_absolute())diagnostics.warning("MPP-PIPELINE-014","Absolute resource-library path is not portable.",{sourcePath},library);auto resolved=path.is_absolute()?path:std::filesystem::path(sourcePath).parent_path()/path;if(!std::filesystem::exists(resolved))diagnostics.error("MPP-PIPELINE-015","Resource library does not exist: "+resolved.string(),{sourcePath},library); }
		}
		set<string> localNames;
		for(auto const& resource:localResources)
		{
			if(resource.name.empty()||!localNames.insert(resource.name).second)diagnostics.error("MPP-PIPELINE-022","Local resource names must be non-empty and unique.",{sourcePath},resource.name);auto expected=resource.kind==PbrPipelineResourceKind::PbrMaterial?"PbrMaterial":resource.kind==PbrPipelineResourceKind::Program?"Program":resource.kind==PbrPipelineResourceKind::Texture?"Texture":resource.kind==PbrPipelineResourceKind::Sampler?"Sampler":resource.kind==PbrPipelineResourceKind::PostEffectMaterial?"PostEffectMaterial":"ParticleEffect";if(resource.definition.getName()!=expected)diagnostics.error("MPP-PIPELINE-023","Local resource '"+resource.name+"' payload type does not match its declared kind.",{sourcePath},resource.name);
		}
		set<string> externalNames;
		for(auto const& external:externalResources){auto qualified=external.libraryName+"::"+external.resource.name;if(external.libraryName.empty()||external.resource.name.empty())diagnostics.error("MPP-PIPELINE-025","External resource library and resource names are required.",{external.libraryPath},qualified);else if(!externalNames.insert(qualified).second)diagnostics.error("MPP-PIPELINE-026","Duplicate qualified external resource '"+qualified+"'.",{external.libraryPath},qualified);if(!external.readOnly)diagnostics.error("MPP-PIPELINE-027","External library resources must be read-only.",{external.libraryPath},qualified);}
		auto resourceReferenceIsResolvable=[&](string const& name){return localNames.contains(name)||externalNames.contains(name)||name.find('/')!=string::npos;};
		set<string> importIds;
		for(auto const& import:imports)
		{
			if(import.id.empty()||import.semantic.empty()||!importIds.insert(import.id).second) diagnostics.error("MPP-PIPELINE-010","Import IDs must be non-empty and unique and require a semantic.",{sourcePath},import.id);
			if(!import.required&&import.fallback.empty()) diagnostics.error("MPP-PIPELINE-011","Optional import '"+import.id+"' requires an explicit fallback.",{sourcePath},import.id);
			bool matched=false;
			if(graph) for(auto handle:graph->getImportedImages()){auto info=graph->getImageInfo(handle);if(info.importName==import.id||info.importName==import.semantic){matched=true;if(info.desc.format!=import.format||((uint32_t)info.desc.usage&(uint32_t)import.usage)!=(uint32_t)import.usage)diagnostics.error("MPP-PIPELINE-012","Import '"+import.id+"' graph descriptor is incompatible with its typed contract.",{sourcePath},import.id);}}
			if(!matched) diagnostics.warning("MPP-PIPELINE-013","Typed import '"+import.id+"' is not referenced by the graph.",{sourcePath},import.id);
		}
		set<string> bindings;
		for (auto const& binding : previewBindings)
		{
			if (binding.binding.empty() || binding.materialResource.empty()) diagnostics.error("MPP-PIPELINE-006", "Preview material binding and resource are required.", { sourcePath }, binding.binding);
			else if (!bindings.insert(binding.binding).second) diagnostics.error("MPP-PIPELINE-007", "Duplicate preview material binding '" + binding.binding + "'.", { sourcePath }, binding.binding);
			else if(!resourceReferenceIsResolvable(binding.materialResource))diagnostics.error("MPP-PIPELINE-024","Preview binding resource '"+binding.materialResource+"' is neither document-local nor externally qualified.",{sourcePath},binding.binding);
		}
		set<string> extensionNamespaces;for(auto const& extension:extensions)if(extension.nameSpace.empty()||!extensionNamespaces.insert(extension.nameSpace).second)diagnostics.error("MPP-PIPELINE-028","Extension namespaces must be non-empty and unique.",{sourcePath},extension.nameSpace);
		set<string> overrideTargets;
		for(auto const& value:previewOverrides)
		{
			auto target=value.modelId+"\n"+value.binding;if(value.modelId.empty()||value.binding.empty())diagnostics.error("MPP-PIPELINE-018","Preview overrides require model and binding IDs.",{sourcePath},value.modelId);else if(!overrideTargets.insert(target).second)diagnostics.error("MPP-PIPELINE-019","Duplicate preview override for model '"+value.modelId+"' and binding '"+value.binding+"'.",{sourcePath},value.modelId);if(!value.binding.empty()&&!bindings.contains(value.binding))diagnostics.error("MPP-PIPELINE-020","Preview override references unknown binding '"+value.binding+"'.",{sourcePath},value.modelId);
			for(auto const& uniform:value.values.getUniformData()){auto const& data=uniform.second;bool supported=data.count==1&&((data.type==program::GLSLType::Int||data.type==program::GLSLType::Bool)||(data.type==program::GLSLType::Float&&data.numElements>=1&&data.numElements<=4));if(!supported)diagnostics.error("MPP-PIPELINE-021","Preview override value '"+uniform.first+"' has an unsupported type or array shape.",{sourcePath},value.modelId);}
		}
		if (previewScene.empty()) diagnostics.warning("MPP-PIPELINE-008", "No preview scene is assigned.", { sourcePath }, "previewScene");
		else {auto path=std::filesystem::path(previewScene);if(path.is_absolute())diagnostics.warning("MPP-PIPELINE-016","Absolute preview-scene path is not portable.",{sourcePath},"previewScene");auto resolved=path.is_absolute()?path:std::filesystem::path(sourcePath).parent_path()/path;if(!std::filesystem::exists(resolved))diagnostics.error("MPP-PIPELINE-017","Preview scene does not exist: "+resolved.string(),{sourcePath},"previewScene");}
		if(!environment.hdrEquirectangular.empty()){auto extension=std::filesystem::path(environment.hdrEquirectangular).extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char value){return(char)std::tolower(value);});if(extension!=".exr")diagnostics.error("MPP-PIPELINE-050","HDR IBL source must be an .exr image.",{sourcePath},"environment");if(!environment.environmentResolution||!environment.irradianceResolution||!environment.prefilterResolution)diagnostics.error("MPP-PIPELINE-051","HDR IBL resolutions must be non-zero.",{sourcePath},"environment");if(!environment.irradiance.empty()||!environment.prefilteredSpecular.empty())diagnostics.warning("MPP-PIPELINE-052","HDR IBL source takes precedence over explicit irradiance or prefiltered cubemaps; manual bindings are retained for fallback authoring.",{sourcePath},"environment");}
		if (environment.binding.empty()) diagnostics.warning("MPP-PIPELINE-009", "No logical PBR environment binding is assigned.", { sourcePath }, "environment");
		return diagnostics;
	}

	DiagnosticBag PbrPipelineDocument::validate(Caps const& caps,RenderGraphPassFactoryRegistry const* registry) const
	{
		return validate(caps,glm::uvec2(0),registry);
	}

	DiagnosticBag PbrPipelineDocument::validate(Caps const& caps,glm::uvec2 const& viewport,RenderGraphPassFactoryRegistry const* registry) const
	{
		auto diagnostics=validate(registry);if(graph){if(ambientOcclusion.method==AmbientOcclusionMethod::Gtao&&ambientOcclusion.gtao.normalSource==GTAONormalSource::Mrt&&(caps.maxColourAttachments<3||caps.maxDrawBuffers<3))diagnostics.error("MPP-PIPELINE-056","GTAO normalSource=mrt requires hardware with at least 3 colour attachments and 3 draw buffers for scene colour (location 0), Bloom/reserved MRT (location 1), and RG16F normals (location 2); select normalSource=depth on incompatible hardware.",{sourcePath},"ambientOcclusion");auto compiled=graph->compile(caps,viewport);for(auto const& message:compiled.diagnostics)diagnostics.error("MPP-PIPELINE-029",message,{sourcePath},"graph");}return diagnostics;
	}

	DiagnosticBag PbrPipelineDocument::validateOutputAntiAliasing(AntiAliasingDefaults const& defaults,Caps const* caps) const
	{
		DiagnosticBag diagnostics;if(!graph)return diagnostics;std::optional<AntiAliasingDefaults> shared;
		auto findImage=[&](string const& requested)->std::optional<GraphImageInfo>{for(uint32_t image=0;image<graph->getImageCount();++image){auto info=graph->getImageInfo({image,0});if(info.name==requested)return info;}return std::nullopt;};
		for(auto const& output:outputs)
		{
			auto effective=resolveAntiAliasing(defaults,output.antiAliasing);
			if(shared&&(effective.msaa!=shared->msaa||effective.ssaa!=shared->ssaa||effective.taa!=shared->taa))diagnostics.error("MPP-PIPELINE-042","Output '"+output.name+"' has effective MSAA, SSAA, or TAA settings that differ from another output in the pipeline.",{sourcePath},output.name);else if(!shared)shared=effective;
			if(caps&&!caps->supportsMsaa(antiAliasingSampleCount(effective.msaa)))diagnostics.error("MPP-PIPELINE-043","Output '"+output.name+"' requests unsupported "+antiAliasingSamplesName(effective.msaa)+" MSAA.",{sourcePath},output.name);
			if(effective.msaa!=AntiAliasingSamples::Off){for(uint32_t graphImage=0;graphImage<graph->getImageCount();++graphImage){auto info=graph->getImageInfo({graphImage,0});if(!info.desc.external&&info.desc.mipLevels>1&&(hasGraphImageUsage(info.desc.usage,GraphImageUsage::ColourAttachment)||hasGraphImageUsage(info.desc.usage,GraphImageUsage::DepthAttachment))){diagnostics.error("MPP-PIPELINE-047","MSAA pipeline attachment '"+info.name+"' cannot declare mip levels; render to a single-level attachment and resolve before generating or sampling mips.",{sourcePath},output.name);break;}}for(uint32_t pass=0;pass<graph->getPassCount();++pass){bool external=false,internal=false;auto classify=[&](GraphImageHandle attachment){auto info=graph->getImageInfo({attachment.id,0});(info.desc.external?external:internal)=true;};auto info=graph->getPassInfo({pass});for(auto const& attachment:info.colourOutputs)classify(attachment.image);for(auto const& attachment:info.depthOutputs)classify(attachment.image);if(external&&internal){diagnostics.error("MPP-PIPELINE-048","MSAA pass '"+info.name+"' mixes external single-sample and internal multisample attachments.",{sourcePath},output.name);break;}}}
			auto image=findImage(output.image);if(!image)continue;
			// A warning rather than an error: outputs must share one effective SSAA
			// setting (MPP-PIPELINE-042), so a pipeline mixing a screen output with an
			// offscreen one legitimately inherits SSAA on both. The offscreen one just
			// cannot act on it -- its destination is the graph image the graph already
			// rendered at raster size, leaving nothing to downsample from.
			if(effective.ssaa!=AntiAliasingSamples::Off&&!image->desc.external)diagnostics.warning("MPP-PIPELINE-050","Effective SSAA on offscreen output '"+output.name+"' has no effect; its destination is the graph image '"+output.image+"', which is already rendered at raster size. TAA and FXAA still apply.",{sourcePath},output.name);
			if(effective.fxaa&&image->desc.format!=GraphImageFormat::Rgba8&&image->desc.format!=GraphImageFormat::Srgb8Alpha8&&image->desc.format!=GraphImageFormat::Rgb10a2)diagnostics.error("MPP-PIPELINE-044","Effective FXAA output '"+output.name+"' requires RGBA8, SRGB8_ALPHA8, or RGB10_A2.",{sourcePath},output.name);
			if(effective.taa&&output.taaDepth.empty()&&!image->desc.external)diagnostics.error("MPP-PIPELINE-045","Effective TAA output '"+output.name+"' requires taaDepth or an external output target with a depth texture.",{sourcePath},output.name);if(effective.taa&&!output.taaDepth.empty()){auto depth=findImage(output.taaDepth);if(depth){bool outputAbsolute=image->desc.absoluteSize.x||image->desc.absoluteSize.y,depthAbsolute=depth->desc.absoluteSize.x||depth->desc.absoluteSize.y;bool mismatch=outputAbsolute!=depthAbsolute||(outputAbsolute?image->desc.absoluteSize!=depth->desc.absoluteSize:image->desc.relativeSize!=depth->desc.relativeSize);if(mismatch)diagnostics.error("MPP-PIPELINE-049","TAA depth source '"+output.taaDepth+"' must resolve to the same dimensions as output '"+output.name+"'.",{sourcePath},output.name);}}

		}
		return diagnostics;
	}
}
