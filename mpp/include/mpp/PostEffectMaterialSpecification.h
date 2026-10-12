#pragma once

#include <string>
#include <vector>

#include "mpp/UniformCollection.h"

#include <mpp/mesh/MeshSpecification.h>

namespace mpp
{
	// Authoring-time description of a PostEffectMaterial: the program it wraps,
	// the sampler slots the fullscreen effect chain binds by name (scene colour,
	// previous-stage output, depth, ...), and default tunable uniform values
	// (threshold, intensity, exposure, ...) overridable per chain entry at
	// runtime. No texture list -- unlike a surface Material, a post effect's
	// image inputs come from the render graph's chain wiring, not
	// material-authored textures.
	struct PostEffectMaterialSpecification
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

			bool is2d{ true };
			mesh::MeshSpecification spec;
			Shader vertexShader, geometryShader, fragmentShader;
		};

	public:

		ProgramOptions program;
		UniformCollection uniforms;
		// Sampler names the effect's fragment program is expected to declare,
		// e.g. "SCENE", "BLOOM", "TEX0". Validated against the loaded Program's
		// reflected samplers when the resource is created.
		std::vector<std::string> samplerSlots;
	};
}
