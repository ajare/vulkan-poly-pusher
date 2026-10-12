#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include "utils/Image.h"

#include "mpp/LegacyMaterialConversion.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	namespace
	{
		bool hasTextureWithSampler(PbrMaterialSpecification const& source, char const* sampler)
		{
			for (auto const& texture : source.textures) if (texture.sampler == sampler) return true;
			return false;
		}

		// Bakes a 4x4 solid-colour PNG for a PBR material that only supplies a
		// flat baseColourFactor: the legacy default fragment shader has no
		// material-colour uniform to feed a flat factor into, so a texture is
		// the only representable equivalent.
		void bakeBaseColourTexture(glm::vec4 const& colour, std::string const& absolutePath)
		{
			std::filesystem::create_directories(std::filesystem::path(absolutePath).parent_path());
			uint8_t pixel[4] =
			{
				(uint8_t)(std::clamp(colour.r, 0.0f, 1.0f) * 255.0f + 0.5f),
				(uint8_t)(std::clamp(colour.g, 0.0f, 1.0f) * 255.0f + 0.5f),
				(uint8_t)(std::clamp(colour.b, 0.0f, 1.0f) * 255.0f + 0.5f),
				(uint8_t)(std::clamp(colour.a, 0.0f, 1.0f) * 255.0f + 0.5f)
			};
			constexpr size_t dimension = 4;
			uint8_t data[dimension * dimension * 4];
			for (size_t texel = 0; texel < dimension * dimension; ++texel) memcpy(data + texel * 4, pixel, 4);
			utils::Image image;
			image.loadFromData(dimension, dimension, 32, data);
			image.saveToFile(absolutePath);
		}
	}

	mpp::data::StructuredData convertPbrMaterialToBasic(
		std::string const& name,
		PbrMaterialSpecification const& source,
		mpp::data::StructuredData const& sourceDefinition,
		bool programIsDefault,
		std::string const& bakedTextureDirectory,
		DiagnosticBag& diagnostics)
	{
		if (!programIsDefault)
		{
			diagnostics.error("MPP-LEGACY-MATERIAL-001", "Material '" + name + "' uses a custom vertex/fragment shader; only the built-in default PBR shader can be converted to the legacy contract.", {}, name);
			THROW_MPP("Cannot convert PBR material '" + name + "' with a custom shader to the legacy contract.", __LINE__, __FILE__, __func__);
		}

		mpp::data::StructuredData result("BasicMaterial");
		result.addEntry("name", name);

		if (sourceDefinition.hasEntry("Program")) result.addEntry("Program", sourceDefinition.getEntry("Program"));

		bool hasBaseColourTexture = sourceDefinition.hasEntry("BaseColourMap");
		if (hasBaseColourTexture)
		{
			mpp::data::StructuredData textures("Textures");
			mpp::data::StructuredData textureNode("Texture");
			textureNode.addEntry("Variable", "TEX1");
			auto const& baseColourMap = sourceDefinition.getEntry("BaseColourMap");
			if (baseColourMap.hasEntry("Resource")) textureNode.addEntry("Resource", baseColourMap.getEntry("Resource"));
			else if (baseColourMap.hasEntry("Ref")) textureNode.addEntry("Ref", baseColourMap.getEntry("Ref").getValue());
			textures.addEntry("Texture", textureNode);
			result.addEntry("Textures", textures);
		}
		else if (source.pbr.enabled)
		{
			auto bakedFilename = name + ".BaseColour.png";
			auto absolutePath = (std::filesystem::path(bakedTextureDirectory) / bakedFilename).string();
			bakeBaseColourTexture(source.pbr.baseColourFactor, absolutePath);
			mpp::data::StructuredData textures("Textures");
			mpp::data::StructuredData textureNode("Texture");
			textureNode.addEntry("Variable", "TEX1");
			mpp::data::StructuredData resource("Resource");
			resource.addEntry("filename", bakedFilename);
			textureNode.addEntry("Resource", resource);
			textures.addEntry("Texture", textureNode);
			result.addEntry("Textures", textures);
			diagnostics.info("MPP-LEGACY-MATERIAL-002", "Material '" + name + "' had no base-colour texture; a flat-colour texture was generated to approximate its baseColourFactor.", {}, name);
		}

		if (source.pbr.enabled)
		{
			bool hasOtherPbrMap =
				hasTextureWithSampler(source, "PBR_METALLIC_ROUGHNESS_MAP") ||
				hasTextureWithSampler(source, "PBR_METALLIC_MAP") ||
				hasTextureWithSampler(source, "PBR_ROUGHNESS_MAP") ||
				hasTextureWithSampler(source, "PBR_NORMAL_MAP") ||
				hasTextureWithSampler(source, "PBR_OCCLUSION_MAP") ||
				hasTextureWithSampler(source, "PBR_EMISSIVE_MAP");
			diagnostics.warning("MPP-LEGACY-MATERIAL-003", "Material '" + name + "': metallic, roughness, normal, occlusion, and emissive maps/factors are not representable in the legacy material contract and were dropped." + std::string(hasOtherPbrMap ? "" : " (Factors only; no maps were authored.)"), {}, name);

			if (source.pbr.alphaMode != PbrMaterialSpecification::PbrAlphaMode::Opaque)
				diagnostics.warning("MPP-LEGACY-MATERIAL-004", "Material '" + name + "': alpha mode is not supported by the legacy material contract (BasicMaterial is always opaque) and was dropped.", {}, name);

			if (source.pbr.doubleSided)
				diagnostics.warning("MPP-LEGACY-MATERIAL-005", "Material '" + name + "': doubleSided is not supported by the legacy material contract and was dropped.", {}, name);
		}

		return result;
	}
}
