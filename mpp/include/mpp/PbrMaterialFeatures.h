#pragma once

#include <cstdint>
#include <string>

#include "mpp/Config.h"
#include "mpp/PbrMaterialSpecification.h"

namespace mpp
{
	enum class PbrMaterialFeature : uint32_t
	{
		None = 0,
		BaseColourMap = 1u << 0,
		Metallic = 1u << 1,
		Roughness = 1u << 2,
		MetallicRoughnessMap = 1u << 3,
		NormalMap = 1u << 4,
		Occlusion = 1u << 5,
		Emissive = 1u << 6,
		AlphaMask = 1u << 7,
		AlphaBlend = 1u << 8,
		DoubleSided = 1u << 9,
		MetallicMap = 1u << 10,
		RoughnessMap = 1u << 11,
		EmissiveMap = 1u << 12,
		// Screen-space reflections with prefiltered-cubemap fallback. See doc/WATER_SSR.md.
		Water = 1u << 13,
		LegacyFullContract = 1u << 31
	};

	using PbrMaterialFeatures = uint32_t;

	constexpr PbrMaterialFeatures pbrFeature(PbrMaterialFeature feature) { return static_cast<PbrMaterialFeatures>(feature); }
	constexpr bool hasPbrFeature(PbrMaterialFeatures features, PbrMaterialFeature feature) { return (features & pbrFeature(feature)) != 0; }

	_MPPAPI PbrMaterialFeatures derivePbrMaterialFeatures(
		PbrMaterialSpecification::PbrSurface const& surface,
		std::vector<PbrMaterialSpecification::TextureOptions> const& textures,
		bool legacyFullContract = false);
	_MPPAPI std::string describePbrMaterialFeatures(PbrMaterialFeatures features);
	_MPPAPI std::string makePbrSpecializationDefines(PbrMaterialFeatures features);
	_MPPAPI std::string injectPbrSpecializationDefines(std::string const& shaderSource, PbrMaterialFeatures features);
}
