#pragma once

#include <string>
#include <map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

#include "mpp/TextureParams.h"
#include "mpp/UniformCollection.h"

#include <mpp/mesh/MeshSpecification.h>

namespace mpp
{

	struct PbrMaterialSpecification
	{
		struct ProgramOptions
		{
			bool resourceExists{ false };

			// For an existing program resource
			std::string existingResource;
			bool isChild{ false };

			// Info for creating new resource
			struct Shader
			{
				enum class Type
				{
					Default,
					File,
					Resource
				};

				Type type{ Type::Default };
				std::string data;
			};

			bool is2d{ false };
			mesh::MeshSpecification spec;
			Shader vertexShader, geometryShader, fragmentShader;
		};

		enum class PbrAlphaMode
		{
			Opaque,
			Mask,
			Blend
		};

		// Screen-space-reflection water surface. Authoring `enabled` selects the
		// PbrMaterialFeature::Water specialization, which reflects the live scene
		// where the ray march succeeds and falls back to the prefiltered IBL
		// cubemap everywhere else. See doc/WATER_SSR.md.
		struct PbrWater
		{
			bool enabled{ false };
			// Animated normal perturbation. Two octaves of the same authored normal
			// maps at different scales/speeds, so the reflection never reads as one
			// tiling texture.
			float distortionScale{ 6.0f };
			float distortionStrength{ 0.06f };
			glm::vec2 scrollSpeed{ 0.02f, -0.013f };
			// Added to the material roughness when selecting the resolved-scene mip,
			// so water can blur its reflection without becoming a rough BRDF.
			float microRoughness{ 0.05f };
			// Ray march tuning. Steps trade quality for cost linearly, and the
			// distance should be roughly how far away the reflected geometry is.
			float ssrMaxDistance{ 40.0f };
			int32_t ssrSteps{ 32 };
			// How thick the march assumes surfaces are. Applied after the crossing
			// is refined, not during stepping, so it is independent of step size;
			// see doc/WATER_SSR.md for why that ordering is what stops reflected
			// objects extruding along the ray.
			float ssrThickness{ 0.5f };
			// UV margin over which a hit near the viewport border fades out.
			float edgeFade{ 0.1f };
			// nDotV range over which SSR hands over to the cubemap. Confidence is 1
			// at or above `grazingFallbackStart` and 0 at or below `grazingFallbackEnd`.
			float grazingFallbackStart{ 0.35f };
			float grazingFallbackEnd{ 0.1f };
		};

		struct PbrSurface
		{
			bool enabled{ false };
			glm::vec4 baseColourFactor{ 1.0f };
			float metallicFactor{ 1.0f };
			float roughnessFactor{ 1.0f };
			glm::vec3 emissiveFactor{ 0.0f };
			float normalScale{ 1.0f };
			float occlusionStrength{ 1.0f };
			PbrAlphaMode alphaMode{ PbrAlphaMode::Opaque };
			float alphaCutoff{ 0.5f };
			bool doubleSided{ false };
			PbrWater water;
		};

		struct TextureOptions
		{
			bool resourceExists{ false };
			std::string sampler;

			// For an existing program resource
			std::string existingResource;
			bool isChild{ false };

			// Info for creating new resource
			std::string source;
			TextureTarget target{ TextureTarget::Texture2D };
			// Component selected by scalar PBR maps: 0=R, 1=G, 2=B, 3=A.
			uint32_t channel{ 0 };

			TextureParams params;
		};

	public:

		ProgramOptions program;
		// Runtime-only compatibility marker. It is deliberately not serialized.
		bool legacyFullContract{ false };
		UniformCollection uniforms;
		PbrSurface pbr;
		std::vector<TextureOptions> textures;
	};

}