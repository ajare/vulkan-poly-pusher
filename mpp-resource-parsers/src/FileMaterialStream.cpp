#include <cctype>

#include "mpp/ResourceManager.h"
#include <filesystem>

#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FileGltfPbrMaterialStream.h"
#include "mpp/resource-parsers/FileMaterialStream.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"
#include "StructuredDataAdapter.h"

namespace mpp::resource_parsers
{
	ResourceStreamPtr FileMaterialStream::fromFile(ResourceManager* resourceMgr, std::string const& filepath, bool relativisePaths)
	{
		auto extension = std::filesystem::path(filepath).extension().string();
		for (auto& character : extension) character = (char)std::tolower((unsigned char)character);
		if (extension == ".gltf") return std::make_shared<FileGltfPbrMaterialStream>(resourceMgr, filepath, relativisePaths);
		auto data = detail::readDocumentRoot(filepath);
		auto const& root = data.getName();
		if (root == "BasicMaterial") return std::make_shared<FileBasicMaterialStream>(resourceMgr, filepath, relativisePaths);
		if (root == "PbrMaterial") return std::make_shared<FilePbrMaterialStream>(resourceMgr, filepath, relativisePaths);
		if (root == "Material")
		{
			if (resourceMgr) resourceMgr->warnMessage("Loading deprecated <Material> XML; rename and re-export it as BasicMaterial or PbrMaterial.");
			if (data.hasEntry("Pbr")) return std::make_shared<FilePbrMaterialStream>(resourceMgr, filepath, relativisePaths);
			return std::make_shared<FileBasicMaterialStream>(resourceMgr, filepath, relativisePaths);
		}
		THROW_MPP_RESOURCE_PARSERS("Material XML root must be BasicMaterial or PbrMaterial: " + filepath, __LINE__, __FILE__, __func__);
	}
}
