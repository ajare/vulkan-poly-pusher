#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mpp/data/StructuredData.h"
#include "mpp/AmbientOcclusion.h"
#include "mpp/Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderPipelineOutput.h"

namespace mpp
{
	class RenderGraphPassFactoryRegistry;
	struct Caps;

	struct _MPPAPI PbrPreviewBinding
	{
		std::string binding;
		std::string materialResource;
	};

	struct _MPPAPI PbrPreviewOverride
	{
		std::string modelId;
		std::string binding;
		UniformCollection values;
	};

	enum class PbrPipelineResourceKind { PbrMaterial, Program, Texture, Sampler, PostEffectMaterial, ParticleEffect };

	struct _MPPAPI PbrPipelineResourceDocument
	{
		std::string name;
		PbrPipelineResourceKind kind{ PbrPipelineResourceKind::PbrMaterial };
		// Existing concrete resource XML payload. Its root name matches kind.
		mpp::data::StructuredData definition{ "PbrMaterial" };
	};

	struct _MPPAPI PbrPipelineExternalResourceDocument
	{
		std::string libraryName;
		std::string libraryPath;
		PbrPipelineResourceDocument resource;
		bool readOnly{ true };
	};

	struct _MPPAPI PbrPipelineImportDocument
	{
		std::string id;
		std::string semantic;
		GraphImageFormat format{ GraphImageFormat::Rgba8 };
		GraphImageUsage usage{ GraphImageUsage::None };
		bool required{ true };
		std::string fallback;
	};

	struct _MPPAPI PbrPipelineExtensionDocument
	{
		std::string nameSpace;
		mpp::data::StructuredData payload{ "Payload" };
	};

	struct _MPPAPI PbrPipelineEnvironmentDocument
	{
		std::string binding;
		std::string irradiance;
		std::string prefilteredSpecular;
		std::string brdfLut;
		std::string background;
		// Optional linear equirectangular HDR source for renderer-owned IBL generation.
		std::string hdrEquirectangular;
		uint32_t environmentResolution{512};
		uint32_t irradianceResolution{32};
		uint32_t prefilterResolution{128};
	};

	struct _MPPAPI PbrPipelineBloomDocument
	{
		bool enabled{ false };
		uint32_t blurPasses{ 0 };
	};

	// One entry in the ordered, reorderable post-effect chain that runs after
	// the 3D scene and before UI (see doc/POST_EFFECT_CHAIN_IMPLEMENTATION_PLAN.md).
	// Each entry becomes one MPP.FullscreenEffect graph pass; PbrPipelineDocument
	// auto-wires its primary "TEX0" input to the previous entry's output (or the
	// scene colour target for the first entry) whenever the chain is (re)built,
	// which is what makes reordering `entries` safe.
	struct _MPPAPI PostEffectChainEntry
	{
		// Stable identifier: the graph pass name and the key used by
		// RenderPipeline::setPostEffectEnabled/setPostEffectParameter at runtime.
		std::string name;
		// PostEffectMaterial resource name.
		std::string material;
		bool enabled{ true };
		// Output size relative to the previous stage's (1.0 = inherit).
		float outputScale{ 1.0f };
		bool inheritFormat{ true };
		GraphImageFormat outputFormat{ GraphImageFormat::Rgba16f };
		// Sampler slots beyond the auto-wired primary "TEX0" chain link. Each
		// source is resolved against another chain entry's name (its output) or
		// a named graph image (e.g. "SceneDepth", "SceneEmissive").
		std::vector<std::pair<std::string, std::string>> extraSamplerBindings;
	};

	struct _MPPAPI PostEffectChain
	{
		std::vector<PostEffectChainEntry> entries;
	};

	class _MPPAPI PbrPipelineDocument
	{
	public:
		static constexpr uint32_t CurrentVersion = 1;

		uint32_t version{ CurrentVersion };
		std::string name;
		std::string sourcePath;
		// Editor migration state; native serialization never emits this flag.
		bool importedFromRenderGraph{ false };
		std::string previewScene;
		std::vector<std::string> resourceLibraries;
		std::vector<PbrPipelineResourceDocument> localResources;
		// Resolved parser/runtime state; serialization emits only resourceLibraries.
		std::vector<PbrPipelineExternalResourceDocument> externalResources;
		std::vector<PbrPipelineImportDocument> imports;
		std::vector<RenderPipelineOutput> outputs;
		std::vector<PbrPipelineExtensionDocument> extensions;
		std::shared_ptr<RenderGraph> graph;
		PbrPipelineEnvironmentDocument environment;
		PbrPipelineBloomDocument bloom;
		AmbientOcclusionOptions ambientOcclusion;
		PostEffectChain postEffects;
		std::vector<PbrPreviewBinding> previewBindings;
		std::vector<PbrPreviewOverride> previewOverrides;

		// Clones a read-only qualified library resource into the document and rewrites direct pipeline references.
		bool makeLocalCopy(std::string const& qualifiedName, std::string const& localName);
		// Keeps the authored graph consistent with Bloom: the PBR emissive MRT and
		// Bloom passes exist only while Bloom is enabled.
		void setBloomEnabled(bool enabled);
		// Inserts/removes the selected fixed ambient-occlusion sequence immediately
		// after the opaque scene pass, before bloom extraction.
		void setAmbientOcclusionMethod(AmbientOcclusionMethod method);
		// Expands `postEffects.entries` into concrete MPP.FullscreenEffect passes,
		// auto-wiring each entry's primary input to the previous entry's output (or
		// `inputImage`/`inputImageName` for the first entry). Removes any
		// previously generated chain passes/images first, so calling this again
		// after editing or reordering `postEffects.entries` regenerates the wiring
		// from scratch rather than layering stale passes on top of new ones.
		// Returns the final entry's output (or `inputImage` unchanged if the chain
		// is empty) -- callers wire this into whatever reads the chain's result.
		GraphImageHandle buildPostEffectChain(GraphImageHandle inputImage, std::string const& inputImageName);
		DiagnosticBag validate(RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		DiagnosticBag validate(Caps const& caps,RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		// Prefer this wherever a viewport is known: without one, graph images sized
		// relative to the viewport cannot be resolved, so they escape device-limit
		// validation here and throw during allocation instead.
		DiagnosticBag validate(Caps const& caps,glm::uvec2 const& viewport,RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		DiagnosticBag validateOutputAntiAliasing(AntiAliasingDefaults const& defaults,Caps const* caps = nullptr) const;
	};
}
