#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "mpp/data/StructuredData.h"
#include "mpp/resource-parsers/Config.h"

namespace mpp::resource_parsers
{
	// Core conversion result. Later phases populate the definition, selected
	// material metadata, generated image files, and non-fatal warnings.
	struct _MPPRESOURCEPARSERSAPI GltfPbrMaterialLoadResult
	{
		mpp::data::StructuredData definition{"PbrMaterial"};
		uint32_t materialIndex{0};
		std::string materialName;
		std::vector<std::filesystem::path> generatedImages;
		std::vector<std::string> warnings;
	};

	class _MPPRESOURCEPARSERSAPI GltfPbrMaterialLoader
	{
	public:
		// Loads the first glTF material. Parsing/conversion is introduced in
		// subsequent phases; this stable API intentionally reports that status
		// until then.
		static std::vector<std::string> listMaterialNames(std::filesystem::path const& filepath);
		static GltfPbrMaterialLoadResult loadMaterialByName(std::filesystem::path const& filepath, std::string const& materialName);
		static GltfPbrMaterialLoadResult loadFirstMaterial(std::filesystem::path const& filepath);
	};
}
