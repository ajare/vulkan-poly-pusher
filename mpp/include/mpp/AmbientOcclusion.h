#pragma once

#include "mpp/Config.h"

#include <string>

namespace mpp
{
	enum class AmbientOcclusionMethod
	{
		None,
		Ssao,
		Gtao
	};

	struct _MPPAPI SSAOOptions
	{
		float radius{ 0.5f };
		float intensity{ 1.0f };
		float bias{ 0.025f };
		float power{ 1.0f };
		int sampleCount{ 16 };
		int blurRadius{ 2 };
	};

	enum class GTAONormalSource
	{
		Depth,
		Mrt
	};

	struct _MPPAPI GTAOOptions
	{
		float radius{ 1.0f };
		float intensity{ 1.0f };
		float thickness{ 0.5f };
		float horizonBias{ 0.05f };
		float falloffStart{ 0.1f };
		float falloffEnd{ 1.0f };
		int sliceCount{ 6 };
		int stepsPerSlice{ 4 };
		float power{ 1.0f };
		int blurRadius{ 2 };
		GTAONormalSource normalSource{ GTAONormalSource::Depth };
	};

	struct _MPPAPI AmbientOcclusionOptions
	{
		AmbientOcclusionMethod method{ AmbientOcclusionMethod::None };
		SSAOOptions ssao;
		GTAOOptions gtao;
		// Optional graph-image name (e.g. a RenderPipelineOptions::sceneExtraOutputs
		// entry, or any other graph image) whose red channel modulates the
		// composited occlusion factor: appliedAmbient = mix(1.0, ao, modulation).
		// Empty (the default) keeps compositing byte-for-byte identical to plain
		// scene.rgb * ao.r.
		std::string modulationInput;
	};
}
