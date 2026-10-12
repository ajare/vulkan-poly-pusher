#include <algorithm>
#include <utility>

#include "mpp/MppException.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphScenePass.h"

namespace mpp
{
	void RenderGraphPassFactoryRegistry::registerFactory(std::string const& name, RenderGraphPassCallback callback)
	{
		if (name.empty() || !callback)
		{
			THROW_MPP("Render graph callback factory requires a name and callback.", __LINE__, __FILE__, __func__);
		}
		mFactories[name] = std::move(callback);
	}

	void RenderGraphPassFactoryRegistry::registerFactory(std::string const& name, RenderGraphPassCallback callback, GraphPassAuthoringMetadata metadata)
	{
		registerFactory(name, std::move(callback));
		mMetadata[name] = std::move(metadata);
	}

	void RenderGraphPassFactoryRegistry::unregisterFactory(std::string const& name)
	{
		mFactories.erase(name);
		mMetadata.erase(name);
	}

	RenderGraphPassCallback RenderGraphPassFactoryRegistry::findFactory(std::string const& name) const
	{
		auto const found = mFactories.find(name);
		return found == mFactories.end() ? RenderGraphPassCallback() : found->second;
	}

	void RenderGraphPassFactoryRegistry::registerScenePassFactory(std::string const& name, std::function<std::unique_ptr<RenderGraphScenePass>()> factory)
	{
		if (name.empty() || !factory)
		{
			THROW_MPP("Render graph scene-pass factory requires a name and factory.", __LINE__, __FILE__, __func__);
		}
		mScenePassFactories[name] = std::move(factory);
	}

	void RenderGraphPassFactoryRegistry::registerScenePassFactory(std::string const& name, std::function<std::unique_ptr<RenderGraphScenePass>()> factory, GraphPassAuthoringMetadata metadata)
	{
		registerScenePassFactory(name, std::move(factory));
		mMetadata[name] = std::move(metadata);
	}

	std::unique_ptr<RenderGraphScenePass> RenderGraphPassFactoryRegistry::createScenePass(std::string const& name) const
	{
		auto const found = mScenePassFactories.find(name);
		return found == mScenePassFactories.end() ? nullptr : found->second();
	}

	void RenderGraphPassFactoryRegistry::registerMetadata(std::string const& name, GraphPassAuthoringMetadata metadata)
	{
		if (name.empty()) THROW_MPP("Render graph pass metadata requires a name.", __LINE__, __FILE__, __func__);
		mMetadata[name] = std::move(metadata);
	}

	GraphPassAuthoringMetadata const* RenderGraphPassFactoryRegistry::findMetadata(std::string const& name) const
	{
		auto const found = mMetadata.find(name);
		return found == mMetadata.end() ? nullptr : &found->second;
	}

	std::vector<std::string> RenderGraphPassFactoryRegistry::getRegisteredMetadataNames() const
	{
		std::vector<std::string> result;
		for (auto const& entry : mMetadata) result.push_back(entry.first);
		return result;
	}

	DiagnosticBag RenderGraphPassFactoryRegistry::validate(RenderGraph const& graph) const
	{
		DiagnosticBag diagnostics;
		for (uint32_t passId = 0; passId < graph.getPassCount(); ++passId)
		{
			auto const pass = graph.getPassInfo({ passId });
			if (!pass.enabled) continue;
			auto const* metadata = findMetadata(pass.callbackFactory);
			if (!metadata)
			{
				diagnostics.error("MPP-PASS-001", "Pass '" + pass.name + "' has no registered authoring metadata for factory '" + pass.callbackFactory + "'.", {}, pass.name);
				continue;
			}
			if (pass.type != metadata->type)
				diagnostics.error("MPP-PASS-002", "Pass '" + pass.name + "' type does not match its factory metadata.", {}, pass.name);
			if (!metadata->acceptsProgram && !pass.programResource.empty())
				diagnostics.error("MPP-PASS-003", "Pass '" + pass.name + "' specifies a program that its factory does not accept.", {}, pass.name);
			if (!metadata->supportsRasterState && pass.rasterState.explicitState)
				diagnostics.error("MPP-PASS-004", "Pass '" + pass.name + "' specifies unsupported raster state.", {}, pass.name);
			if ((pass.callbackFactory == "MPP.ParticleScene" || pass.callbackFactory == "MPP.ParticleWeightedOit" ||
				pass.callbackFactory == "MPP.ParticleDistortion" || pass.callbackFactory == "MPP.ParticleVolumetricLighting" ||
				pass.callbackFactory == "MPP.TrailScene") && pass.rasterState.explicitState && pass.rasterState.depthWrite)
				diagnostics.warning("MPP-PASS-014", "Particle primitive pass '" + pass.name + "' requests depth writes, but transparent draws always force them off.", {}, pass.name);

			size_t requiredInputs = 0;
			for (auto const& input : metadata->inputs) if (input.required) ++requiredInputs;
			if (pass.sampledInputs.size() < requiredInputs || (!metadata->allowAdditionalInputs && pass.sampledInputs.size() > metadata->inputs.size()))
				diagnostics.error("MPP-PASS-005", "Pass '" + pass.name + "' sampled-input count does not match its factory contract.", {}, pass.name);
			for (size_t input = 0; input < std::min(pass.sampledInputs.size(), metadata->inputs.size()); ++input)
			{
				auto const format = graph.getImageInfo(pass.sampledInputs[input]).desc.format;
				auto const& accepted = metadata->inputs[input].acceptedFormats;
				if (!accepted.empty() && std::find(accepted.begin(), accepted.end(), format) == accepted.end())
					diagnostics.error("MPP-PASS-006", "Pass '" + pass.name + "' input '" + metadata->inputs[input].name + "' has an incompatible format.", {}, pass.name);
			}

			size_t colourIndex = 0, depthIndex = 0;
			for (auto const& output : metadata->outputs)
			{
				bool present = output.depth ? depthIndex < pass.depthOutputs.size() : colourIndex < pass.colourOutputs.size();
				if (!present)
				{
					if (output.required) diagnostics.error("MPP-PASS-007", "Pass '" + pass.name + "' is missing required output '" + output.name + "'.", {}, pass.name);
					continue;
				}
				auto handle = output.depth ? pass.depthOutputs[depthIndex++].image : pass.colourOutputs[colourIndex++].image;
				auto const format = graph.getImageInfo(handle).desc.format;
				if (!output.acceptedFormats.empty() && std::find(output.acceptedFormats.begin(), output.acceptedFormats.end(), format) == output.acceptedFormats.end())
					diagnostics.error("MPP-PASS-008", "Pass '" + pass.name + "' output '" + output.name + "' has an incompatible format.", {}, pass.name);
			}
			if (!metadata->allowAdditionalOutputs && (colourIndex < pass.colourOutputs.size() || depthIndex < pass.depthOutputs.size()))
				diagnostics.error("MPP-PASS-009", "Pass '" + pass.name + "' declares outputs not accepted by its factory contract.", {}, pass.name);

			auto const& values = pass.parameters.getUniformData();
			for (auto const& parameter : metadata->parameters)
			{
				auto const found = values.find(parameter.name);
				if (found == values.end())
				{
					if (parameter.required) diagnostics.error("MPP-PASS-010", "Pass '" + pass.name + "' is missing parameter '" + parameter.name + "'.", {}, pass.name);
					continue;
				}
				if (found->second.type != parameter.type || found->second.count != parameter.count || found->second.numElements != parameter.elements)
					diagnostics.error("MPP-PASS-011", "Pass '" + pass.name + "' parameter '" + parameter.name + "' has the wrong reflected type or shape.", {}, pass.name);
			}
			if (!metadata->nameDerivedFallbackParameter.empty() && values.find(metadata->nameDerivedFallbackParameter) == values.end())
			{
				diagnostics.warning("MPP-PASS-013", "Pass '" + pass.name + "' does not declare '" + metadata->nameDerivedFallbackParameter +
					"', so it is inferred from digits at the end of the pass name. Renaming the pass would silently change its behaviour; declare the parameter instead.", {}, pass.name);
			}
			for (auto const& value : values)
				if (!metadata->allowAdditionalParameters && std::find_if(metadata->parameters.begin(), metadata->parameters.end(), [&](GraphPassParameterMetadata const& parameter) { return parameter.name == value.first; }) == metadata->parameters.end())
					diagnostics.warning("MPP-PASS-012", "Pass '" + pass.name + "' has unrecognized parameter '" + value.first + "'.", {}, pass.name);
		}
		return diagnostics;
	}
}
