#pragma once

#include <string>
#include <vector>

#include "mpp/TextureParams.h"
#include "mpp/UniformCollection.h"
#include "mpp/Material.h"
#include <mpp/mesh/MeshSpecification.h>

namespace mpp
{
	struct BasicMaterialSpecification
	{
		struct ProgramOptions
		{
			bool resourceExists{ false };
			std::string existingResource;
			bool isChild{ false };

			struct Shader
			{
				enum class Type { Default, File, Resource };
				Type type{ Type::Default };
				std::string data;
			};

			bool is2d;
			mesh::MeshSpecification spec;
			Shader vertexShader, geometryShader, fragmentShader;
		};

		struct TextureOptions
		{
			bool resourceExists{ false };
			std::string sampler;
			std::string existingResource;
			bool isChild{ false };
			std::string source;
			TextureTarget target{ TextureTarget::Texture2D };
			TextureParams params;
		};

		ProgramOptions program;
		// Generic shaders can only cast a non-solid silhouette when they author
		// this contract; the opaque default preserves legacy materials.
		ShadowCasterContract shadowCaster;
		UniformCollection uniforms;
		std::vector<TextureOptions> textures;
	};
}
