#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mpp/Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/RenderGraph.h"

namespace mpp
{
	class RenderGraphExecutionContext;
	class RenderGraphScenePass;
	using RenderGraphPassCallback = std::function<void(RenderGraphExecutionContext const&)>;

	struct _MPPAPI GraphPassInputMetadata
	{
		std::string name;
		std::string sampler;
		bool required{ true };
		std::vector<GraphImageFormat> acceptedFormats;
		std::string fallbackId;
	};

	struct _MPPAPI GraphPassOutputMetadata
	{
		std::string name;
		bool depth{ false };
		bool required{ true };
		std::vector<GraphImageFormat> acceptedFormats;
	};

	struct _MPPAPI GraphPassParameterMetadata
	{
		std::string name;
		program::GLSLType type{ program::GLSLType::Unknown };
		size_t count{ 1 };
		size_t elements{ 1 };
		bool required{ false };
		bool hasRange{ false };
		double minimum{ 0.0 };
		double maximum{ 0.0 };
		std::string uiHint;
	};

	struct _MPPAPI GraphPassAuthoringMetadata
	{
		// Name of a parameter the pass will otherwise infer from its own pass name,
		// which makes renaming the pass change its behaviour. Validation reports a
		// deprecation warning while the parameter is absent. Empty for passes with
		// no such fallback.
		std::string nameDerivedFallbackParameter;
		std::string displayName;
		std::string category;
		GraphPassType type{ GraphPassType::Scene };
		std::vector<GraphPassInputMetadata> inputs;
		std::vector<GraphPassOutputMetadata> outputs;
		std::vector<GraphPassParameterMetadata> parameters;
		std::vector<std::string> materialSlots;
		bool acceptsProgram{ false };
		bool supportsRasterState{ false };
		bool allowAdditionalInputs{ false };
		bool allowAdditionalOutputs{ false };
		bool allowAdditionalParameters{ false };
	};

	// Maps XML-authored identifiers to application-provided executable code.
	// Factories are registered by the application; code itself is never XML.
	class _MPPAPI RenderGraphPassFactoryRegistry
	{
		std::map<std::string, RenderGraphPassCallback> mFactories;
		std::map<std::string, std::function<std::unique_ptr<RenderGraphScenePass>()>> mScenePassFactories;
		std::map<std::string, GraphPassAuthoringMetadata> mMetadata;

	public:
		void registerFactory(std::string const& name, RenderGraphPassCallback callback);
		void registerFactory(std::string const& name, RenderGraphPassCallback callback, GraphPassAuthoringMetadata metadata);
		void unregisterFactory(std::string const& name);
		RenderGraphPassCallback findFactory(std::string const& name) const;

		void registerScenePassFactory(std::string const& name, std::function<std::unique_ptr<RenderGraphScenePass>()> factory);
		void registerScenePassFactory(std::string const& name, std::function<std::unique_ptr<RenderGraphScenePass>()> factory, GraphPassAuthoringMetadata metadata);
		std::unique_ptr<RenderGraphScenePass> createScenePass(std::string const& name) const;

		void registerMetadata(std::string const& name, GraphPassAuthoringMetadata metadata);
		GraphPassAuthoringMetadata const* findMetadata(std::string const& name) const;
		std::vector<std::string> getRegisteredMetadataNames() const;
		DiagnosticBag validate(RenderGraph const& graph) const;
	};
}
