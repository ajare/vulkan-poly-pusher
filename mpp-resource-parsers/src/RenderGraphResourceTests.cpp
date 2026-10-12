#include <filesystem>
#include <fstream>

#include "mpp/Caps.h"
#include "mpp/LegacyPipelineDocument.h"
#include "mpp/PbrPipelineDocument.h"
#include "mpp/resource-parsers/LegacyPipelineParser.h"
#include "mpp/resource-parsers/LegacyPipelineSerializer.h"
#include "mpp/resource-parsers/LegacyPipelineResourceValidator.h"
#include "mpp/resource-parsers/PbrPipelineParser.h"
#include "mpp/resource-parsers/PbrPipelineSerializer.h"
#include "mpp/resource-parsers/PbrPipelineResourceValidator.h"
#include "mpp/resource-parsers/RenderGraphParser.h"
#include "mpp/resource-parsers/RenderGraphResourceTests.h"
#include "mpp/resource-parsers/RenderGraphSerializer.h"
#include "mpp/resource-parsers/SceneParser.h"
#include "mpp/resource-parsers/SceneSerializer.h"

namespace mpp::resource_parsers
{
	namespace
	{
		bool runForExtension(std::string const& extension, std::string* failure)
		{
			auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
			auto const root = std::filesystem::temp_directory_path() / ("mpp_render_graph_resource_test" + extension);
			auto const bootstrapXml = root.string() + "_bootstrap.xml";
			auto const source = root.string() + "_source" + extension;
			auto const roundTrip = root.string() + "_roundtrip" + extension;
			{
				std::ofstream file(bootstrapXml);
				file << R"(<RenderGraph>
<Images>
 <Image><name>Input</name><format>RGBA16F</format><usage>sampled,colourAttachment</usage><width>32</width><height>16</height><mipLevels>3</mipLevels><colourSpace>LINEAR</colourSpace><minFilter>LINEAR</minFilter><magFilter>LINEAR</magFilter><wrap>CLAMP_TO_EDGE</wrap></Image>
 <Image><name>Screen</name><format>RGBA8</format><usage>colourAttachment,presentation</usage><import>screen</import><external>true</external></Image>
</Images>
<Passes>
 <Pass><name>Effect</name><type>fullscreen</type><program>Effects.Test</program><factory>Tests.Effect</factory><Inputs><Sampled><sampler>TEX1</sampler><mipLevel>1</mipLevel><image>Input</image></Sampled></Inputs><Parameters><Float><name>EXPOSURE</name><value>1.5</value></Float><Vec3><name>TINT</name><value>1 0.5 0.25</value></Vec3></Parameters><Colours><Output><image>Input</image><mipLevel>2</mipLevel><load>clear</load><store>store</store><clear>0 0 0 1</clear></Output></Colours><Raster><explicit>true</explicit><fill>line</fill><frontFace>clockwise</frontFace><cull>front</cull><depthTest>false</depthTest><depthWrite>false</depthWrite><depthCompare>greaterEqual</depthCompare><blend>true</blend><colourBlendOp>reverseSubtract</colourBlendOp><alphaBlendOp>maximum</alphaBlendOp><sourceColourBlend>sourceAlpha</sourceColourBlend><destinationColourBlend>oneMinusSourceAlpha</destinationColourBlend><sourceAlphaBlend>one</sourceAlphaBlend><destinationAlphaBlend>oneMinusDestinationAlpha</destinationAlphaBlend><multisample>false</multisample><alphaToCoverage>true</alphaToCoverage><scissor>true</scissor><scissorRectangle>4 8 16 32</scissorRectangle><ColourWriteMasks><Mask>true false true false</Mask></ColourWriteMasks></Raster></Pass>
 <Pass><name>Present</name><type>present</type><Inputs><Sampled><image>Input</image></Sampled></Inputs><Colours><Output><image>Screen</image><load>dontCare</load><store>store</store></Output></Colours></Pass>
</Passes>
</RenderGraph>)";
			}
			try
			{
				// The fixture text above is XML; for the YAML leg, bootstrap the real
				// source file by parsing it once and re-serializing through the same
				// writer path production code uses, instead of hand-authoring a
				// second fixture that would drift from the collapse scheme.
				if (extension == ".xml")
				{
					std::filesystem::copy_file(bootstrapXml, source, std::filesystem::copy_options::overwrite_existing);
				}
				else
				{
					auto bootstrapGraph = RenderGraphParser::fromFile(bootstrapXml);
					RenderGraphSerializer::toFile(bootstrapGraph, source);
				}

				auto graph = RenderGraphParser::fromFile(source);
				if (graph.getImageCount() != 2 || graph.getPassCount() != 2) return fail(extension + ": parsed graph counts differ");
				auto screen = graph.getImageInfo({ 1, 0 });
				if (!screen.desc.external || screen.importName != "screen") return fail(extension + ": named import was not parsed");
				auto effect = graph.getPassInfo({ 0 });
				if (effect.type != GraphPassType::Fullscreen || effect.programResource != "Effects.Test" || effect.samplerBindings.size() != 1 || effect.samplerBindings.front().mipLevel != 1 || effect.parameters.getNumUniforms() != 2 || effect.colourOutputs.front().mipLevel != 2) return fail(extension + ": pass metadata was not parsed");

				// Raster state is authored in the editor and honoured by the executor, so
				// losing it in either direction silently discards the author's work.
				GraphRasterState expectedRaster;
				expectedRaster.explicitState = true;
				expectedRaster.fillMode = GraphFillMode::Line;
				expectedRaster.frontFace = GraphFrontFace::Clockwise;
				expectedRaster.cullMode = GraphCullMode::Front;
				expectedRaster.depthTest = false;
				expectedRaster.depthWrite = false;
				expectedRaster.depthCompare = GraphCompareOp::GreaterEqual;
				expectedRaster.blend = true;
				expectedRaster.colourBlendOp = GraphBlendOp::ReverseSubtract;
				expectedRaster.alphaBlendOp = GraphBlendOp::Maximum;
				expectedRaster.sourceColourBlend = GraphBlendFactor::SourceAlpha;
				expectedRaster.destinationColourBlend = GraphBlendFactor::OneMinusSourceAlpha;
				expectedRaster.sourceAlphaBlend = GraphBlendFactor::One;
				expectedRaster.destinationAlphaBlend = GraphBlendFactor::OneMinusDestinationAlpha;
				expectedRaster.multisample = false;
				expectedRaster.alphaToCoverage = true;
				expectedRaster.scissor = true;
				expectedRaster.scissorRectangle = { 4, 8, 16, 32 };
				expectedRaster.colourWriteMasks = { { true, false, true, false } };
				if (!(effect.rasterState == expectedRaster)) return fail(extension + ": pass raster state was not parsed");
				if (!(graph.getPassInfo({ 1 }).rasterState == GraphRasterState{})) return fail(extension + ": a pass without a Raster block did not keep the default state");

				RenderGraphSerializer::toFile(graph, roundTrip);
				auto restored = RenderGraphParser::fromFile(roundTrip);
				auto restoredEffect = restored.getPassInfo({ 0 });
				if (restored.getImageCount() != 2 || restoredEffect.programResource != "Effects.Test" || restoredEffect.samplerBindings.size() != 1 || restoredEffect.samplerBindings.front().mipLevel != 1 || restoredEffect.parameters.getNumUniforms() != 2 || restoredEffect.colourOutputs.front().mipLevel != 2) return fail(extension + ": graph round trip lost metadata");
				if (!(restoredEffect.rasterState == expectedRaster)) return fail(extension + ": graph round trip lost raster state");
				if (!(restored.getPassInfo({ 1 }).rasterState == GraphRasterState{})) return fail(extension + ": graph round trip invented raster state for a default pass");

				// A default state must not be emitted at all, so documents that never
				// touched raster state stay byte-identical through the editor.
				{
					std::ifstream restoredFile(roundTrip);
					std::string const text((std::istreambuf_iterator<char>(restoredFile)), std::istreambuf_iterator<char>());
					std::string const rasterMarker = extension == ".xml" ? "<Raster>" : "Raster:";
					if (text.find(rasterMarker) == std::string::npos) return fail(extension + ": explicit raster state was not serialized");
					if (text.find(rasterMarker) != text.rfind(rasterMarker)) return fail(extension + ": default raster state was serialized");
				}

				GraphImageDesc cubeDepth; cubeDepth.format = GraphImageFormat::Depth24; cubeDepth.shape = GraphImageShape::CubeMap; cubeDepth.absoluteSize = { 32, 32 };
				cubeDepth.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled; cubeDepth.depthCompare = true;
				RenderGraph cubeGraph; auto cube = cubeGraph.createImage("ShadowCube", cubeDepth); cube = cubeGraph.writeDepth(cubeGraph.addPass("Face4"), cube, GraphLoadOp::Clear, GraphStoreOp::Store, 0.4f, 0, 4);
				RenderGraphSerializer::toFile(cubeGraph, roundTrip); auto restoredCube = RenderGraphParser::fromFile(roundTrip); auto restoredCubeInfo = restoredCube.getImageInfo({ 0, 0 }); auto restoredCubePass = restoredCube.getPassInfo({ 0 });
				if (restoredCubeInfo.desc.shape != GraphImageShape::CubeMap || !restoredCubeInfo.desc.depthCompare || restoredCubePass.depthOutputs.front().cubeFace != 4) return fail(extension + ": depth cubemap shape, comparison, or face was lost in round trip");
			}
			catch (std::exception const& exception)
			{
				return fail(extension + ": " + exception.what());
			}
			std::filesystem::remove(bootstrapXml);
			std::filesystem::remove(source);
			std::filesystem::remove(roundTrip);
			return true;
		}
	}

		bool runPbrPipelineSsaoDocumentTest(std::string const& extension, std::string* failure)
		{
			auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
			auto const path = (std::filesystem::temp_directory_path() / ("mpp_ssao_pipeline_document" + extension)).string();
			try
			{
				PbrPipelineDocument document;
				document.name = "SSAO structural test";
				document.graph = std::make_shared<RenderGraph>();
				GraphImageDesc colour; colour.format = GraphImageFormat::Rgba16f; colour.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
				GraphImageDesc depth; depth.format = GraphImageFormat::Depth24; depth.usage = GraphImageUsage::DepthAttachment;
				auto scene = document.graph->createImage("SceneHdr", colour);
				auto sceneDepth = document.graph->createImage("SceneDepth", depth);
				auto bloom = document.graph->createImage("BloomExtract", colour);
				auto scenePass = document.graph->addPass("PbrScene", GraphPassType::Scene);
				document.graph->setPassCallbackFactory(scenePass, "MPP.PbrScene");
				scene = document.graph->writeColour(scenePass, scene, GraphLoadOp::Clear, GraphStoreOp::Store);
				document.graph->writeDepth(scenePass, sceneDepth, GraphLoadOp::Clear, GraphStoreOp::DontCare);
				auto bloomPass = document.graph->addPass("BloomExtract", GraphPassType::Fullscreen);
				document.graph->setPassCallbackFactory(bloomPass, "MPP.FullscreenEffect");
				document.graph->setPassProgramResource(bloomPass, "PostEffect.BloomExtract");
				document.graph->bindSampler(bloomPass, "TEX1", scene);
				document.graph->writeColour(bloomPass, bloom);
				document.ambientOcclusion.method = AmbientOcclusionMethod::Gtao;
				document.ambientOcclusion.ssao = { 0.7f, 1.3f, 0.04f, 1.2f, 24, 3 };
				document.ambientOcclusion.gtao = { 1.5f, 1.2f, 0.4f, 0.05f, 0.1f, 1.0f, 6, 4, 1.4f, 3 };
				document.ambientOcclusion.gtao.normalSource = GTAONormalSource::Mrt;
				document.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
				if (!document.graph->compile().diagnostics.empty()) return fail(extension + ": ambient occlusion authored an invalid graph: " + document.graph->compile().diagnostics.front());
				auto const pass = [&](uint32_t index) { return document.graph->getPassInfo({ index }); };
				if (document.graph->getPassCount() != 5 || pass(1).name != "GTAO" || pass(1).callbackFactory != "MPP.GTAORaw" || pass(2).name != "AmbientOcclusionBlur" || pass(3).name != "AmbientOcclusionComposite" || pass(4).name != "BloomExtract") return fail(extension + ": GTAO passes were not inserted between the scene and bloom extract passes");
				if (document.graph->getImageCount() != 8 || pass(4).samplerBindings.front().image.id == scene.id) return fail(extension + ": GTAO images or bloom routing were not authored");
				auto sceneInfo = document.graph->getPassInfo(scenePass);
				if (sceneInfo.colourOutputs.size() != 3 || document.graph->getImageInfo(sceneInfo.colourOutputs[2].image).desc.format != GraphImageFormat::Rg16f || pass(1).samplerBindings.size() != 2 || pass(1).samplerBindings[1].sampler != "NORMALS" || pass(1).samplerBindings[1].image.id != sceneInfo.colourOutputs[2].image.id) return fail(extension + ": GTAO MRT normals were not attached at scene location 2 and bound to the raw pass");
				if (!hasGraphImageUsage(document.graph->getImageInfo({ sceneDepth.id, 0 }).desc.usage, GraphImageUsage::Sampled) || document.graph->getPassInfo(scenePass).depthOutputs.front().store != GraphStoreOp::Store) return fail(extension + ": GTAO did not retain sampled scene depth");
				document.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
				if (document.graph->getPassCount() != 5 || document.graph->getImageCount() != 8) return fail(extension + ": repeated MRT GTAO selection duplicated graph resources");
				document.ambientOcclusion.gtao.normalSource = GTAONormalSource::Depth;
				document.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
				if (document.graph->getPassCount() != 5 || document.graph->getImageCount() != 6 || document.graph->getPassInfo({ 0 }).colourOutputs.size() != 1 || document.graph->getPassInfo({ 1 }).samplerBindings.size() != 1) return fail(extension + ": depth GTAO did not remove MRT-only resources and bindings");
				document.ambientOcclusion.gtao.normalSource = GTAONormalSource::Mrt;
				document.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
				PbrPipelineSerializer::toFile(document, path);
				std::ifstream normalSourceFile(path); std::string withoutNormalSource((std::istreambuf_iterator<char>(normalSourceFile)), std::istreambuf_iterator<char>()); normalSourceFile.close();
				if (extension == ".xml") { auto begin = withoutNormalSource.find("        <normalSource>mrt</normalSource>\n"); if (begin == std::string::npos) return fail(extension + ": GTAO normal source was not serialized"); withoutNormalSource.erase(begin, std::string("        <normalSource>mrt</normalSource>\n").size()); }
				else { auto begin = withoutNormalSource.find("      normalSource: mrt\n"); if (begin == std::string::npos) return fail(extension + ": GTAO normal source was not serialized"); withoutNormalSource.erase(begin, std::string("      normalSource: mrt\n").size()); }
				auto const defaultPath = (std::filesystem::temp_directory_path() / ("mpp_default_gtao_normal_source" + extension)).string(); std::ofstream defaultFile(defaultPath); defaultFile << withoutNormalSource; defaultFile.close();
				auto defaulted = PbrPipelineParser::fromFile(defaultPath);
				if (defaulted.ambientOcclusion.gtao.normalSource != GTAONormalSource::Depth || defaulted.graph->getPassInfo({ 0 }).colourOutputs.size() != 1) return fail(extension + ": omitted GTAO normal source did not preserve depth behavior");
				std::filesystem::remove(defaultPath);
				auto restored = PbrPipelineParser::fromFile(path);
				if (!restored.graph->compile().diagnostics.empty()) return fail(extension + ": round-tripped ambient-occlusion graph is invalid: " + restored.graph->compile().diagnostics.front());
				if (restored.ambientOcclusion.method != AmbientOcclusionMethod::Gtao || restored.ambientOcclusion.ssao.sampleCount != 24 || restored.ambientOcclusion.gtao.radius != 1.5f || restored.ambientOcclusion.gtao.thickness != 0.4f || restored.ambientOcclusion.gtao.sliceCount != 6 || restored.ambientOcclusion.gtao.stepsPerSlice != 4 || restored.ambientOcclusion.gtao.blurRadius != 3 || restored.ambientOcclusion.gtao.normalSource != GTAONormalSource::Mrt) return fail(extension + ": ambient-occlusion options did not survive pipeline round trip");
				restored.ambientOcclusion.gtao.falloffEnd = restored.ambientOcclusion.gtao.falloffStart;
				auto invalidGtaoDiagnostics = restored.validate(); bool rejectedInvalidGtao = false; for (auto const& diagnostic : invalidGtaoDiagnostics.getDiagnostics()) rejectedInvalidGtao |= diagnostic.code == "MPP-PIPELINE-053";
				if (!rejectedInvalidGtao) return fail(extension + ": invalid GTAO parameters were accepted");
				restored.ambientOcclusion.gtao.falloffEnd = 1.0f;
				Caps insufficientMrtCaps; insufficientMrtCaps.maxColourAttachments = 2; insufficientMrtCaps.maxDrawBuffers = 2;
				auto capabilityDiagnostics = restored.validate(insufficientMrtCaps); bool rejectedMrtCapability = false; std::string capabilityCodes; for (auto const& diagnostic : capabilityDiagnostics.getDiagnostics()) { rejectedMrtCapability |= diagnostic.code == "MPP-PIPELINE-056"; capabilityCodes += diagnostic.code + " "; }
				if (!rejectedMrtCapability) return fail(extension + ": MRT GTAO did not report insufficient hardware capability (" + capabilityCodes + ")");
				for (uint32_t passId = 0; passId < restored.graph->getPassCount(); ++passId) if (restored.graph->getPassInfo({ passId }).callbackFactory == "MPP.PbrScene") { restored.graph->removeColourOutput({ passId }, 2); break; }
				auto contractDiagnostics = restored.validate(); bool rejectedMrtContract = false; std::string contractCodes; for (auto const& diagnostic : contractDiagnostics.getDiagnostics()) { rejectedMrtContract |= diagnostic.code == "MPP-PIPELINE-054"; contractCodes += diagnostic.code + " "; }
				if (!rejectedMrtContract) return fail(extension + ": MRT GTAO did not report a missing location-2 scene output (" + contractCodes + ")");
				restored.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
				restored.setAmbientOcclusionMethod(AmbientOcclusionMethod::None);
				if (restored.graph->getPassCount() != 2 || restored.graph->getImageCount() != 3 || restored.graph->getPassInfo({ 1 }).name != "BloomExtract" || restored.graph->getPassInfo({ 1 }).samplerBindings.front().image.id != scene.id) return fail(extension + ": disabling ambient occlusion did not clean up only its generated resources");

				std::ifstream nativeFile(path); std::string legacyText((std::istreambuf_iterator<char>(nativeFile)), std::istreambuf_iterator<char>()); nativeFile.close();
				if (extension == ".xml")
				{
					auto begin = legacyText.find("    <AmbientOcclusion>"); auto end = legacyText.find("    </AmbientOcclusion>", begin);
					if (begin == std::string::npos || end == std::string::npos) return fail(extension + ": native AmbientOcclusion section was not serialized");
					end += std::string("    </AmbientOcclusion>\n").size();
					legacyText.replace(begin, end - begin, "    <SSAO>\n        <enabled>true</enabled>\n        <radius>0.75</radius>\n        <sampleCount>12</sampleCount>\n    </SSAO>\n");
				}
				else
				{
					auto begin = legacyText.find("  AmbientOcclusion:\n"); auto end = legacyText.find("  RenderGraph:\n", begin);
					if (begin == std::string::npos || end == std::string::npos) return fail(extension + ": native AmbientOcclusion section was not serialized");
					legacyText.replace(begin, end - begin, "  SSAO:\n    enabled: true\n    radius: 0.75\n    sampleCount: 12\n");
				}
				std::ofstream legacyFile(path, std::ios::trunc); legacyFile << legacyText; legacyFile.close();
				auto migrated = PbrPipelineParser::fromFile(path);
				if (migrated.ambientOcclusion.method != AmbientOcclusionMethod::Ssao || migrated.ambientOcclusion.ssao.radius != 0.75f || migrated.ambientOcclusion.ssao.sampleCount != 12 || migrated.graph->getPassInfo({1}).callbackFactory != "MPP.SSAORaw") return fail(extension + ": legacy SSAO section was not migrated");
			}
			catch (std::exception const& exception) { return fail(extension + ": " + exception.what()); }
			std::filesystem::remove(path);
			return true;
		}

	bool runLegacyPipelineSsaoDocumentTest(std::string const& extension, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		auto const path = (std::filesystem::temp_directory_path() / ("mpp_legacy_ssao_pipeline_document" + extension)).string();
		auto hasDiagnostic = [](DiagnosticBag const& diagnostics, std::string const& code)
		{
			for (auto const& diagnostic : diagnostics.getDiagnostics()) if (diagnostic.code == code) return true;
			return false;
		};
		try
		{
			LegacyPipelineDocument document;
			document.name = "Legacy SSAO structural test";
			document.graph = std::make_shared<RenderGraph>();
			GraphImageDesc colour; colour.format = GraphImageFormat::Rgba16f; colour.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			auto output = document.graph->createImage("Output", colour);
			auto raw = document.graph->addPass("SSAO", GraphPassType::Fullscreen);
			document.graph->setPassCallbackFactory(raw, "MPP.SSAORaw");
			document.graph->writeColour(raw, output);
			document.outputs.push_back({ "Output", "Output" });
			document.ambientOcclusion.method = AmbientOcclusionMethod::Ssao;
			document.ambientOcclusion.ssao = { 0.7f, 1.3f, 0.04f, 1.2f, 24, 3 };
			LegacyPipelineSerializer::toFile(document, path);
			auto restored = LegacyPipelineParser::fromFile(path);
			if (restored.ambientOcclusion.method != AmbientOcclusionMethod::Ssao || restored.ambientOcclusion.ssao.radius != 0.7f || restored.ambientOcclusion.ssao.intensity != 1.3f || restored.ambientOcclusion.ssao.bias != 0.04f || restored.ambientOcclusion.ssao.power != 1.2f || restored.ambientOcclusion.ssao.sampleCount != 24 || restored.ambientOcclusion.ssao.blurRadius != 3) return fail(extension + ": SSAO options did not survive LegacyPipeline round trip");
			if (hasDiagnostic(restored.validate(), "MPP-LEGACY-PIPELINE-038")) return fail(extension + ": enabled SSAO with its raw pass was rejected");
			restored.graph->removePass({ 0 });
			if (!hasDiagnostic(restored.validate(), "MPP-LEGACY-PIPELINE-038")) return fail(extension + ": enabled SSAO without its raw pass was accepted");
			restored.ambientOcclusion.method = AmbientOcclusionMethod::None;
			if (hasDiagnostic(restored.validate(), "MPP-LEGACY-PIPELINE-038")) return fail(extension + ": disabled SSAO without its raw pass was rejected");
		}
		catch (std::exception const& exception) { return fail(extension + ": " + exception.what()); }
		std::filesystem::remove(path);
		return true;
	}

	bool runLegacyPipelineGtaoMrtDocumentTest(std::string const& extension, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		auto const path = (std::filesystem::temp_directory_path() / ("mpp_legacy_gtao_mrt_pipeline_document" + extension)).string();
		auto hasDiagnostic = [](DiagnosticBag const& diagnostics, std::string const& code)
		{
			for (auto const& diagnostic : diagnostics.getDiagnostics()) if (diagnostic.code == code) return true;
			return false;
		};
		try
		{
			LegacyPipelineDocument document;
			document.name = "Legacy GTAO MRT structural test";
			document.graph = std::make_shared<RenderGraph>();
			GraphImageDesc colour; colour.format = GraphImageFormat::Rgba16f; colour.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			GraphImageDesc depth; depth.format = GraphImageFormat::Depth24; depth.usage = GraphImageUsage::DepthAttachment;
			auto sceneColour = document.graph->createImage("SceneColour", colour);
			auto sceneDepth = document.graph->createImage("SceneDepth", depth);
			auto scene = document.graph->addPass("LegacyScene", GraphPassType::Scene);
			document.graph->setPassCallbackFactory(scene, "MPP.LegacyScene");
			document.graph->writeColour(scene, sceneColour);
			document.graph->writeDepth(scene, sceneDepth);
			auto rawImage = document.graph->createImage("GtaoRaw", colour);
			auto raw = document.graph->addPass("GTAO", GraphPassType::Fullscreen);
			document.graph->setPassCallbackFactory(raw, "MPP.GTAORaw");
			document.graph->bindSampler(raw, "DEPTH", sceneDepth);
			document.graph->writeColour(raw, rawImage);
			document.outputs.push_back({ "Output", "SceneColour" });
			document.ambientOcclusion.method = AmbientOcclusionMethod::Gtao;
			document.ambientOcclusion.gtao.normalSource = GTAONormalSource::Mrt;
			document.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
			LegacyPipelineSerializer::toFile(document, path);
			auto restored = LegacyPipelineParser::fromFile(path);
			auto sceneInfo = restored.graph->getPassInfo({ 0 });
			auto rawInfo = restored.graph->getPassInfo({ 1 });
			if (restored.ambientOcclusion.gtao.normalSource != GTAONormalSource::Mrt || sceneInfo.colourOutputs.size() != 3 || restored.graph->getImageInfo(sceneInfo.colourOutputs[2].image).desc.format != GraphImageFormat::Rg16f || rawInfo.samplerBindings.size() != 2 || rawInfo.samplerBindings[1].sampler != "NORMALS" || rawInfo.samplerBindings[1].image.id != sceneInfo.colourOutputs[2].image.id) return fail(extension + ": legacy MRT GTAO did not round-trip its location-2 normals wiring");
			Caps insufficientMrtCaps; insufficientMrtCaps.maxColourAttachments = 2; insufficientMrtCaps.maxDrawBuffers = 2;
			if (!hasDiagnostic(restored.validate(insufficientMrtCaps), "MPP-LEGACY-PIPELINE-048")) return fail(extension + ": legacy MRT GTAO accepted insufficient hardware");
			restored.graph->removeColourOutput({ 0 }, 2);
			if (!hasDiagnostic(restored.validate(), "MPP-LEGACY-PIPELINE-046")) return fail(extension + ": legacy MRT GTAO accepted a missing location-2 scene output");
			restored.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);

			std::ifstream file(path); std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()); file.close();
			std::string marker = extension == ".xml" ? "        <normalSource>mrt</normalSource>\n" : "      normalSource: mrt\n";
			auto begin = text.find(marker); if (begin == std::string::npos) return fail(extension + ": legacy GTAO normal source was not serialized");
			text.erase(begin, marker.size()); std::ofstream defaultFile(path, std::ios::trunc); defaultFile << text; defaultFile.close();
			auto defaulted = LegacyPipelineParser::fromFile(path);
			if (defaulted.ambientOcclusion.gtao.normalSource != GTAONormalSource::Depth || defaulted.graph->getPassInfo({ 0 }).colourOutputs.size() != 1 || defaulted.graph->getPassInfo({ 1 }).samplerBindings.size() != 1) return fail(extension + ": omitted legacy GTAO normal source did not retain depth graph shape");
			defaulted.setAmbientOcclusionMethod(AmbientOcclusionMethod::Gtao);
			LegacyPipelineSerializer::toFile(defaulted, path);
			auto depthRoundTrip = LegacyPipelineParser::fromFile(path);
			if (depthRoundTrip.ambientOcclusion.gtao.normalSource != GTAONormalSource::Depth) return fail(extension + ": explicit legacy depth normal source did not round-trip");

			std::ifstream depthFile(path); text.assign((std::istreambuf_iterator<char>(depthFile)), std::istreambuf_iterator<char>()); depthFile.close();
			auto gtaoText = text;
			if (extension == ".xml")
			{
				auto aoBegin = text.find("    <AmbientOcclusion>"); auto aoEnd = text.find("    </AmbientOcclusion>", aoBegin);
				if (aoBegin == std::string::npos || aoEnd == std::string::npos) return fail(extension + ": legacy AmbientOcclusion section was not serialized");
				aoEnd += std::string("    </AmbientOcclusion>\n").size();
				text.replace(aoBegin, aoEnd - aoBegin, "    <SSAO>\n        <enabled>true</enabled>\n    </SSAO>\n");
			}
			else
			{
				auto aoBegin = text.find("  AmbientOcclusion:\n"); auto aoEnd = text.find("  RenderGraph:\n", aoBegin);
				if (aoBegin == std::string::npos || aoEnd == std::string::npos) return fail(extension + ": legacy AmbientOcclusion section was not serialized");
				text.replace(aoBegin, aoEnd - aoBegin, "  SSAO:\n    enabled: true\n");
			}
			std::ofstream ssaoFile(path, std::ios::trunc); ssaoFile << text; ssaoFile.close();
			auto migrated = LegacyPipelineParser::fromFile(path);
			if (migrated.ambientOcclusion.method != AmbientOcclusionMethod::Ssao || migrated.ambientOcclusion.gtao.normalSource != GTAONormalSource::Depth || migrated.graph->getPassInfo({ 0 }).colourOutputs.size() != 1) return fail(extension + ": historical legacy SSAO acquired GTAO MRT behavior");

			text = gtaoText;
			if (extension == ".xml") text.replace(text.find("<normalSource>depth</normalSource>"), std::string("<normalSource>depth</normalSource>").size(), "<normalSource>invalid</normalSource>");
			else text.replace(text.find("normalSource: depth"), std::string("normalSource: depth").size(), "normalSource: invalid");
			std::ofstream invalidFile(path, std::ios::trunc); invalidFile << text; invalidFile.close();
			try { (void)LegacyPipelineParser::fromFile(path); return fail(extension + ": invalid legacy GTAO normal source was accepted"); }
			catch (std::exception const& exception) { if (std::string(exception.what()).find("Invalid GTAO normal source") == std::string::npos) return fail(extension + ": invalid legacy GTAO normal source diagnostic was unclear"); }
		}
		catch (std::exception const& exception) { return fail(extension + ": " + exception.what()); }
		std::filesystem::remove(path);
		return true;
	}

	bool runParticlePipelineResourceTest(std::string const& extension, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		auto const path = (std::filesystem::temp_directory_path() / ("mpp_particle_pipeline_resource" + extension)).string();
		try
		{
			PbrPipelineDocument document; document.name = "Particle resource round trip"; document.graph = std::make_shared<RenderGraph>();
			PbrPipelineResourceDocument resource; resource.name = "Effects.Smoke"; resource.kind = PbrPipelineResourceKind::ParticleEffect; resource.definition = mpp::data::StructuredData("ParticleEffect");
			resource.definition.addEntry("version", "1"); resource.definition.addEntry("name", "Smoke"); resource.definition.addEntry("maximumParticleCount", "8");
			mpp::data::StructuredData emitters("Emitters"), emitter("Emitter"), spawn("Spawn"); emitter.addEntry("name", "Smoke"); emitter.addEntry("maximumParticleCount", "8"); spawn.addEntry("shape", "point"); spawn.addEntry("rate", "1"); emitter.addEntry("Spawn", spawn); emitters.addEntry("Emitter", emitter); resource.definition.addEntry("Emitters", emitters); document.localResources.push_back(resource);
			PbrPipelineSerializer::toFile(document, path); auto restored = PbrPipelineParser::fromFile(path);
			if (restored.localResources.size() != 1u || restored.localResources[0].kind != PbrPipelineResourceKind::ParticleEffect || validatePbrPipelineResourceDefinitions(restored).hasErrors()) return fail(extension + ": PBR particle effect pipeline resource did not round-trip or validate");
			LegacyPipelineDocument legacy; legacy.name = "Legacy particle resource round trip"; legacy.graph = std::make_shared<RenderGraph>(); LegacyPipelineResourceDocument legacyResource; legacyResource.name = resource.name; legacyResource.kind = LegacyPipelineResourceKind::ParticleEffect; legacyResource.definition = resource.definition; legacy.localResources.push_back(legacyResource);
			LegacyPipelineSerializer::toFile(legacy, path); auto restoredLegacy = LegacyPipelineParser::fromFile(path);
			if (restoredLegacy.localResources.size() != 1u || restoredLegacy.localResources[0].kind != LegacyPipelineResourceKind::ParticleEffect || validateLegacyPipelineResourceDefinitions(restoredLegacy).hasErrors()) return fail(extension + ": legacy particle effect pipeline resource did not round-trip or validate");
		}
		catch (std::exception const& exception) { return fail(extension + ": " + exception.what()); }
		std::filesystem::remove(path); return true;
	}

	bool runSceneShadowDocumentTest(std::string const& extension, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		auto const path = std::filesystem::temp_directory_path() / ("mpp_scene_shadow_round_trip" + extension);
		try
		{
			SceneDocument document;
			document.name = "Point-shadow round trip";
			SceneLightDocument fill; fill.id = "Fill";
			SceneLightDocument point; point.id = "PointShadow"; point.type = SceneLightType::Point;
			point.position = { 3.0f, 4.0f, 5.0f }; point.range = 24.0f; point.castsShadows = true;
			SceneLightDocument rim; rim.id = "Rim";
			document.lights = { fill, point, rim };
			SceneParticleEffectDocument particles; particles.id = "Campfire"; particles.effect = "Effects/Fire";
			particles.translation = { 1.0f, 2.0f, 3.0f }; particles.rotationDegrees = { 0.0f, 45.0f, 0.0f };
			particles.scale = { 2.0f, 2.0f, 2.0f }; particles.visible = false; document.particleEffects.push_back(particles);
			SceneSerializer::toFile(document, path.string());
			auto restored = SceneParser::fromFile(path.string());
			if (restored.validate().hasErrors() || restored.getShadowLightIndex() != 1 ||
				restored.lights[1].type != SceneLightType::Point || restored.lights[1].position != document.lights[1].position ||
				restored.lights[1].range != document.lights[1].range || restored.particleEffects.size() != 1u ||
				restored.particleEffects[0].id != particles.id || restored.particleEffects[0].effect != particles.effect ||
				restored.particleEffects[0].translation != particles.translation ||
				restored.particleEffects[0].rotationDegrees != particles.rotationDegrees ||
				restored.particleEffects[0].scale != particles.scale || restored.particleEffects[0].visible)
				return fail(extension + ": scene round trip lost its authored light or particle effect");
		}
		catch (std::exception const& exception) { return fail(extension + ": " + exception.what()); }
		std::filesystem::remove(path);
		return true;
	}

	bool runRenderGraphResourceTests(std::string* failure)
	{
		for (auto const& extension : { std::string(".xml"), std::string(".yaml") })
		{
			if (!runForExtension(extension, failure)) return false;
			if (!runSceneShadowDocumentTest(extension, failure)) return false;
			if (!runParticlePipelineResourceTest(extension, failure)) return false;
			if (!runPbrPipelineSsaoDocumentTest(extension, failure)) return false;
			if (!runLegacyPipelineSsaoDocumentTest(extension, failure)) return false;
			if (!runLegacyPipelineGtaoMrtDocumentTest(extension, failure)) return false;
		}
		return true;
	}
}
