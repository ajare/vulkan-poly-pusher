#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mpp/data/StructuredData.h"
#include "mpp/Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderPipelineOutput.h"
#include "mpp/PbrPipelineDocument.h"

namespace mpp
{
	class RenderGraphPassFactoryRegistry;
	struct Caps;

	// Structurally parallel to PbrPipelineDocument, minus the PBR-only
	// environment/IBL concept: legacy lighting is flat ambient + point lights,
	// with no equivalent to translate environment into. Preview binding and
	// override types are shared with PbrPipelineDocument (PbrPreviewBinding,
	// PbrPreviewOverride) since neither is PBR-specific in shape.
	enum class LegacyPipelineResourceKind { BasicMaterial, Program, Texture, Sampler, PostEffectMaterial, ParticleEffect };

	struct _MPPAPI LegacyPipelineResourceDocument
	{
		std::string name;
		LegacyPipelineResourceKind kind{ LegacyPipelineResourceKind::BasicMaterial };
		// Existing concrete resource XML payload. Its root name matches kind.
		mpp::data::StructuredData definition{ "BasicMaterial" };
	};

	struct _MPPAPI LegacyPipelineExternalResourceDocument
	{
		std::string libraryName;
		std::string libraryPath;
		LegacyPipelineResourceDocument resource;
		bool readOnly{ true };
	};

	class _MPPAPI LegacyPipelineDocument
	{
	public:
		static constexpr uint32_t CurrentVersion = 1;

		uint32_t version{ CurrentVersion };
		std::string name;
		std::string sourcePath;
		std::string previewScene;
		std::vector<std::string> resourceLibraries;
		std::vector<LegacyPipelineResourceDocument> localResources;
		// Resolved parser/runtime state; serialization emits only resourceLibraries.
		std::vector<LegacyPipelineExternalResourceDocument> externalResources;
		std::vector<PbrPipelineImportDocument> imports;
		std::vector<RenderPipelineOutput> outputs;
		std::vector<PbrPipelineExtensionDocument> extensions;
		std::shared_ptr<RenderGraph> graph;
		PbrPipelineBloomDocument bloom;
		AmbientOcclusionOptions ambientOcclusion;
		PostEffectChain postEffects;
		std::vector<PbrPreviewBinding> previewBindings;
		std::vector<PbrPreviewOverride> previewOverrides;

		// Normalizes the generated GTAO MRT-normal wiring after parsing or an
		// authoring change. SSAO deliberately remains depth-only.
		void setAmbientOcclusionMethod(AmbientOcclusionMethod method);
		DiagnosticBag validate(RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		DiagnosticBag validate(Caps const& caps, RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		// Prefer this wherever a viewport is known: without one, graph images sized
		// relative to the viewport cannot be resolved, so they escape device-limit
		// validation here and throw during allocation instead.
		DiagnosticBag validate(Caps const& caps, glm::uvec2 const& viewport, RenderGraphPassFactoryRegistry const* registry = nullptr) const;
		DiagnosticBag validateOutputAntiAliasing(AntiAliasingDefaults const& defaults, Caps const* caps = nullptr) const;
	};
}
