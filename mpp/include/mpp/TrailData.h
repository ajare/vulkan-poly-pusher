#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "mpp/Config.h"
#include "mpp/ParticleData.h"

namespace mpp
{
	// One camera-facing ribbon's copied creation data. A trail owns position
	// history; it never creates particles to approximate that history.
	struct _MPPAPI TrailSpecification
	{
		uint32_t maximumPointCount{ 128u };
		float pointLifetime{ 1.0f };
		float minimumPointDistance{ 0.05f };
		float width{ 1.0f };
		float uvScale{ 1.0f };
		std::array<float, 4> tintAndAlpha{ 1.0f, 1.0f, 1.0f, 1.0f };
		float emissiveIntensity{ 1.0f };
		float softFadeDistance{ 0.0f };
		ParticleBlendClass blendClass{ ParticleBlendClass::Additive };
		ParticleCurve widthOverLife{};
		ParticleGradient colourOverLife{};
	};

	// CPU-written state for one trail. The matching std430 declaration lives in
	// TrailShaders.h. historyGeneration clears a fixed GPU history slice without
	// requiring the CPU to write any trail points.
	struct alignas(16) TrailControlData
	{
		// Current world position and recording-enabled flag.
		std::array<float, 4> positionEnabled{};
		// Point lifetime, minimum sample distance, U per world unit, base width.
		std::array<float, 4> lifetimeDistanceUvWidth{ 1.0f, 0.05f, 1.0f, 1.0f };
		std::array<float, 4> tintAndAlpha{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Emissive intensity, soft fade distance, LUT row, padding.
		std::array<float, 4> appearance{ 1.0f, 0.0f, 0.0f, 0.0f };
		// Occupied, blend class, history generation, point capacity.
		std::array<uint32_t, 4> modes{ 0u, 0u, 1u, 128u };
	};

	// A trail point is a separate GPU primitive, not a ParticleRecord. Every
	// point carries its own age/lifetime and cumulative length for generated UVs.
	struct alignas(16) TrailPointRecord
	{
		std::array<float, 4> positionAge{};
		// Lifetime, cumulative distance, then reserved values.
		std::array<float, 4> lifetimeDistance{};
	};

	struct alignas(16) TrailState
	{
		// First ring index, live count, history generation, padding.
		std::array<uint32_t, 4> ring{};
		// Last fixed sample position and cumulative travelled distance.
		std::array<float, 4> samplePositionDistance{};
	};

	static_assert(sizeof(TrailControlData) == 80u);
	static_assert(sizeof(TrailPointRecord) == 32u);
	static_assert(sizeof(TrailState) == 32u);
	static_assert(std::is_standard_layout_v<TrailPointRecord>);
}
