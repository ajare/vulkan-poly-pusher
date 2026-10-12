#include "mpp/LegacyMaterialConversion.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"
#include "mpp/resource-parsers/LegacyPipelineConversion.h"

using namespace std;

namespace mpp::resource_parsers
{
	namespace
	{
		bool programSubtreeIsDefault(mpp::data::StructuredData const& node)
		{
			auto shaderIsDefault = [&](char const* key)
			{
				if (!node.hasEntry(key)) return true;
				auto const& shader = node.getEntry(key);
				return !shader.hasEntry("file") && !shader.hasEntry("resource");
			};
			return shaderIsDefault("VertexShader") && shaderIsDefault("FragmentShader");
		}

		mpp::data::StructuredData const* findResourceDefinition(PbrPipelineDocument const& document, string const& reference, PbrPipelineResourceKind kind)
		{
			for (auto const& local : document.localResources) if (local.kind == kind && local.name == reference) return &local.definition;
			for (auto const& external : document.externalResources)
			{
				auto qualified = external.libraryName + "::" + external.resource.name;
				if (external.resource.kind == kind && (external.resource.name == reference || qualified == reference)) return &external.resource.definition;
			}
			return nullptr;
		}

		// A material converts cleanly only if its program resolves to the
		// built-in default shader -- either no <Program> at all, an embedded
		// <Resource> with no custom VertexShader/FragmentShader, or a <Ref>
		// to a separate Program resource that is itself default. An
		// unresolvable <Ref> is treated conservatively as non-default.
		bool materialProgramIsDefault(PbrPipelineDocument const& document, mpp::data::StructuredData const& materialDefinition)
		{
			if (!materialDefinition.hasEntry("Program")) return true;
			auto const& programEntry = materialDefinition.getEntry("Program");
			if (programEntry.hasEntry("Resource")) return programSubtreeIsDefault(programEntry.getEntry("Resource"));
			if (programEntry.hasEntry("Ref"))
			{
				auto found = findResourceDefinition(document, programEntry.getEntry("Ref").getValue(), PbrPipelineResourceKind::Program);
				return found && programSubtreeIsDefault(*found);
			}
			return false;
		}

		LegacyPipelineResourceKind toLegacyKind(PbrPipelineResourceKind kind)
		{
			switch (kind)
			{
			case PbrPipelineResourceKind::Program: return LegacyPipelineResourceKind::Program;
			case PbrPipelineResourceKind::Texture: return LegacyPipelineResourceKind::Texture;
			case PbrPipelineResourceKind::Sampler: return LegacyPipelineResourceKind::Sampler;
			case PbrPipelineResourceKind::PostEffectMaterial: return LegacyPipelineResourceKind::PostEffectMaterial;
			case PbrPipelineResourceKind::ParticleEffect: return LegacyPipelineResourceKind::ParticleEffect;
			default: return LegacyPipelineResourceKind::BasicMaterial;
			}
		}
	}

	LegacyPipelineDocument convertPbrPipelineToLegacy(PbrPipelineDocument const& source, string const& bakedTextureDirectory, DiagnosticBag& diagnostics)
	{
		LegacyPipelineDocument result;
		result.version = LegacyPipelineDocument::CurrentVersion;
		result.name = source.name;
		result.sourcePath = source.sourcePath;
		result.previewScene = source.previewScene;
		result.resourceLibraries = source.resourceLibraries;
		result.imports = source.imports;
		result.outputs = source.outputs;
		result.extensions = source.extensions;
		result.graph = source.graph;
		result.bloom = source.bloom;
		result.ambientOcclusion = source.ambientOcclusion;
		result.postEffects = source.postEffects;
		result.previewBindings = source.previewBindings;
		result.previewOverrides = source.previewOverrides;

		auto convertResource = [&](PbrPipelineResourceDocument const& resource) -> LegacyPipelineResourceDocument
		{
			LegacyPipelineResourceDocument converted;
			converted.name = resource.name;
			if (resource.kind == PbrPipelineResourceKind::PbrMaterial)
			{
				converted.kind = LegacyPipelineResourceKind::BasicMaterial;
				auto parsed = FilePbrMaterialStream::parseDefinition(resource.definition, nullptr, source.sourcePath);
				auto programIsDefault = materialProgramIsDefault(source, resource.definition);
				converted.definition = convertPbrMaterialToBasic(resource.name, parsed.second, resource.definition, programIsDefault, bakedTextureDirectory, diagnostics);
			}
			else
			{
				converted.kind = toLegacyKind(resource.kind);
				converted.definition = resource.definition;
			}
			return converted;
		};

		for (auto const& resource : source.localResources) result.localResources.push_back(convertResource(resource));
		for (auto const& external : source.externalResources)
		{
			LegacyPipelineExternalResourceDocument convertedExternal;
			convertedExternal.libraryName = external.libraryName;
			convertedExternal.libraryPath = external.libraryPath;
			convertedExternal.readOnly = external.readOnly;
			convertedExternal.resource = convertResource(external.resource);
			result.externalResources.push_back(convertedExternal);
		}

		if (!source.environment.binding.empty())
			diagnostics.warning("MPP-LEGACY-CONVERT-001", "Source pipeline's PBR environment/IBL binding '" + source.environment.binding + "' has no legacy equivalent and was dropped.", { source.sourcePath }, "environment");

		return result;
	}
}
