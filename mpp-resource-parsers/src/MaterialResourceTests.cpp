#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <fstream>
#include <vector>

#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ProgrammaticPbrMaterialStream.h"
#include "mpp/ResourceStreamSerializer.h"
#include "mpp/RenderGraph.h"
#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FileMaterialStream.h"
#include "mpp/resource-parsers/GltfPbrMaterialLoader.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/MaterialResourceTests.h"
#include "mpp/resource-parsers/PbrPipelineParser.h"
#include "mpp/resource-parsers/PbrPipelineSerializer.h"

namespace mpp::resource_parsers
{
	namespace
	{
		std::vector<char> readBytes(std::filesystem::path const& path)
		{
			std::ifstream input(path, std::ios::binary);
			return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		}

		void addLegacyDefinitionTables(std::vector<char>& bytes, size_t typeLength, uint32_t definitionCount)
		{
			size_t const afterChildren = 4 + sizeof(uint32_t) + typeLength + sizeof(uint32_t);
			uint32_t const one = 1, zero = 0;
			std::vector<char> legacyHeader(12);
			std::memcpy(legacyHeader.data(), &definitionCount, 4);
			std::memcpy(legacyHeader.data() + 4, &zero, 4);
			std::memcpy(legacyHeader.data() + 8, &zero, 4);
			bytes.insert(bytes.begin() + afterChildren, legacyHeader.begin(), legacyHeader.end());
			bytes.insert(bytes.begin() + afterChildren + legacyHeader.size(), reinterpret_cast<char const*>(&one), reinterpret_cast<char const*>(&one) + sizeof(one));
		}

		void makeVersion2(std::filesystem::path const& path)
		{
			auto bytes = readBytes(path);
			addLegacyDefinitionTables(bytes, 13, 1);
			std::memcpy(bytes.data(), "RSE2", 4);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}

		void makeLegacy(std::filesystem::path const& path, uint32_t definitionCount = 1)
		{
			auto bytes = readBytes(path);
			char const typed[] = "BasicMaterial";
			auto found = std::search(bytes.begin(), bytes.end(), typed, typed + sizeof(typed) - 1);
			if (found == bytes.end()) throw std::runtime_error("BasicMaterial tag not found in test stream");
			auto const offset = static_cast<size_t>(found - bytes.begin());
			uint32_t legacyLength = 8;
			std::memcpy(bytes.data() + offset - sizeof(uint32_t), &legacyLength, sizeof(legacyLength));
			std::memcpy(bytes.data() + offset, "Material", legacyLength);
			bytes.erase(bytes.begin() + offset + legacyLength, bytes.begin() + offset + sizeof(typed) - 1);
			// RSE2/RSER placed a name table before type data and a definition count
			// inside each concrete stream. This fixture has no children.
			addLegacyDefinitionTables(bytes, legacyLength, definitionCount);
			std::memcpy(bytes.data(), "RSER", 4);
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
	}

	bool runMaterialResourceTests(ResourceManager* resourceMgr, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		auto const root = std::filesystem::temp_directory_path() / "mpp_material_resource_tests";
		// `root` is a filename prefix for most artefacts but the glTF fixture below
		// is written *inside* it. Without this the ofstream silently fails and the
		// loader reports a missing file, which blocked the whole suite.
		std::error_code rootError;
		std::filesystem::create_directories(root, rootError);
		if (rootError) return fail("could not create the material test directory: " + rootError.message());
		auto const basicXml = root.string() + "_basic.xml";
		auto const pbrXml = root.string() + "_pbr.xml";
		auto const invalidPbrXml = root.string() + "_invalid_pbr.xml";
		auto const embeddedVariantsXml = root.string() + "_embedded_variants.xml";
		auto const basicBin = root.string() + "_basic.bin";
		auto const pbrBin = root.string() + "_pbr.bin";
		auto const version2BasicBin = root.string() + "_v2_basic.bin";
		auto const legacyBasicBin = root.string() + "_legacy_basic.bin";
		auto const legacyPbrBin = root.string() + "_legacy_pbr.bin";
		auto const legacyMultiBin = root.string() + "_legacy_multi.bin";
		auto const hdrPipelineXml = root.string() + "_hdr_ibl.pipeline.xml";
		auto const hdrPipelineYaml = root.string() + "_hdr_ibl.pipeline.yaml";
		auto const basicYaml = root.string() + "_basic.yaml";
		auto const pbrYaml = root.string() + "_pbr.yaml";
		try
		{
			PbrPipelineDocument hdrPipeline; hdrPipeline.sourcePath = hdrPipelineXml; hdrPipeline.graph = std::make_shared<RenderGraph>(); hdrPipeline.environment.binding = "HdrEnvironment"; hdrPipeline.environment.hdrEquirectangular = "environments/studio.exr"; hdrPipeline.environment.environmentResolution = 256; hdrPipeline.environment.irradianceResolution = 16; hdrPipeline.environment.prefilterResolution = 64; PbrPipelineSerializer::toFile(hdrPipeline, hdrPipelineXml); auto parsedHdrPipeline = PbrPipelineParser::fromFile(hdrPipelineXml); if (parsedHdrPipeline.environment.hdrEquirectangular != "environments/studio.exr" || parsedHdrPipeline.environment.environmentResolution != 256 || parsedHdrPipeline.environment.irradianceResolution != 16 || parsedHdrPipeline.environment.prefilterResolution != 64) return fail("HDR IBL pipeline environment did not survive serializer/parser round trip");
			// Same document, .yaml extension: proves the format is a drop-in
			// replacement for the generic Serializer/FileStream dispatch, not just
			// something XmlSerializer happens to accept.
			PbrPipelineSerializer::toFile(hdrPipeline, hdrPipelineYaml); auto parsedHdrPipelineYaml = PbrPipelineParser::fromFile(hdrPipelineYaml); if (parsedHdrPipelineYaml.environment.hdrEquirectangular != "environments/studio.exr" || parsedHdrPipelineYaml.environment.environmentResolution != 256 || parsedHdrPipelineYaml.environment.irradianceResolution != 16 || parsedHdrPipelineYaml.environment.prefilterResolution != 64) return fail("HDR IBL pipeline environment did not survive YAML serializer/parser round trip");
			{ std::ofstream file(basicXml); file << "<BasicMaterial><name>Test.Basic</name></BasicMaterial>"; }
			{ std::ofstream file(pbrXml); file << "<PbrMaterial><name>Test.Pbr</name><Surface><metallicFactor>0.25</metallicFactor><roughnessFactor>0.75</roughnessFactor></Surface></PbrMaterial>"; }
			{ std::ofstream file(basicYaml); file << "BasicMaterial:\n  name: Test.Basic\n"; }
			{ std::ofstream file(pbrYaml); file << "PbrMaterial:\n  name: Test.Pbr\n  Surface:\n    metallicFactor: 0.25\n    roughnessFactor: 0.75\n"; }
			{ std::ofstream file(invalidPbrXml); file << "<PbrMaterial><name>Test.InvalidPbr</name><Surface><roughnessFactor>1.5</roughnessFactor></Surface></PbrMaterial>"; }
			{ std::ofstream file(embeddedVariantsXml); file << "<Texture><target>2D</target><filename>a.png</filename><Quality><name>Low</name><target>2D</target><filename>b.png</filename></Quality></Texture>"; }
			auto basicFile = FileMaterialStream::fromFile(resourceMgr, basicXml);
			auto pbrFile = FileMaterialStream::fromFile(resourceMgr, pbrXml);
			if (basicFile->getType() != "BasicMaterial" || pbrFile->getType() != "PbrMaterial") return fail("material XML root dispatch selected the wrong stream type");
			auto basicYamlFile = FileMaterialStream::fromFile(resourceMgr, basicYaml);
			auto pbrYamlFile = FileMaterialStream::fromFile(resourceMgr, pbrYaml);
			if (basicYamlFile->getType() != "BasicMaterial" || pbrYamlFile->getType() != "PbrMaterial") return fail("material YAML root dispatch selected the wrong stream type");
			bool rejected = false;
			try { FileBasicMaterialStream wrong(resourceMgr, pbrXml); wrong.load(); } catch (...) { rejected = true; }
			if (!rejected) return fail("BasicMaterial parser accepted a PbrMaterial root");
			rejected = false;
			try { FilePbrMaterialStream wrong(resourceMgr, basicXml); wrong.load(); } catch (...) { rejected = true; }
			if (!rejected) return fail("PbrMaterial parser accepted a BasicMaterial root");
			rejected = false;
			try { FilePbrMaterialStream invalid(resourceMgr, invalidPbrXml); invalid.load(); } catch (...) { rejected = true; }
			if (!rejected) return fail("PbrMaterial parser accepted an out-of-range surface factor");
			rejected = false;
			try { FileTextureStream variants(resourceMgr, embeddedVariantsXml); variants.load(); } catch (...) { rejected = true; }
			if (!rejected) return fail("Texture parser accepted embedded Quality variants");

			ResourceStreamSerializer serializer(resourceMgr);
			auto basic = std::make_shared<ProgrammaticBasicMaterialStream>(resourceMgr);
			basic->setUniform("CUSTOM_VALUE", 4.0f);
			serializer.serialize(basic, basicBin);
			auto restoredBasic = serializer.deserialize(basicBin);
			if (restoredBasic->getType() != "BasicMaterial") return fail("BasicMaterial binary round trip changed its concrete type");
			restoredBasic->load();
			auto restoredBasicStream = dynamic_cast<BasicMaterialStream*>(restoredBasic.get());
			if (!restoredBasicStream || !restoredBasicStream->getUniforms().getUniformData().contains("CUSTOM_VALUE")) return fail("BasicMaterial generic uniform was lost during binary round trip");

			auto pbr = std::make_shared<ProgrammaticPbrMaterialStream>(resourceMgr);
			PbrMaterialSpecification::PbrSurface low;
			low.metallicFactor = 0.2f; low.roughnessFactor = 0.8f;
			pbr->setSurface(low);
			pbr->setExtensionUniform("PBR_EXT_TEST", glm::vec2(2.0f, 3.0f));
			serializer.serialize(pbr, pbrBin);
			auto bytes = readBytes(pbrBin);
			// RSE4 added each texture's colour space to RSE3's layout; only RSE4 is
			// written now (see ResourceStreamSerializer::serialize's own comment).
			if (bytes.size() < 4 || std::memcmp(bytes.data(), "RSE4", 4) != 0) return fail("new resource stream is not versioned RSE4");
			if (std::search(bytes.begin(), bytes.end(), "PbrMaterial", "PbrMaterial" + 11) == bytes.end()) return fail("PbrMaterial binary tag is missing");
			if (std::search(bytes.begin(), bytes.end(), "\x08\x00\x00\x00Material", "\x08\x00\x00\x00Material" + 12) != bytes.end()) return fail("new serializer emitted a legacy Material tag");
			auto restoredPbrBase = serializer.deserialize(pbrBin);
			auto restoredPbr = dynamic_cast<PbrMaterialStream*>(restoredPbrBase.get());
			if (!restoredPbr || restoredPbrBase->getType() != "PbrMaterial") return fail("PbrMaterial binary round trip changed its concrete type");
			if (restoredPbr->usesLegacyFullContract()) return fail("new PbrMaterial binary round trip acquired legacy full-contract state");
			restoredPbr->load();
			if (restoredPbr->getPbrSurface().metallicFactor != 0.2f || restoredPbr->getPbrSurface().roughnessFactor != 0.8f) return fail("PBR surface did not survive binary round trip");

			std::filesystem::copy_file(basicBin, version2BasicBin, std::filesystem::copy_options::overwrite_existing);
			makeVersion2(version2BasicBin);
			if (serializer.deserialize(version2BasicBin)->getType() != "BasicMaterial") return fail("single-definition RSE2 BasicMaterial did not migrate");
			std::filesystem::copy_file(basicBin, legacyBasicBin, std::filesystem::copy_options::overwrite_existing);
			makeLegacy(legacyBasicBin);
			if (serializer.deserialize(legacyBasicBin)->getType() != "BasicMaterial") return fail("legacy basic Material did not convert to BasicMaterial");
			std::filesystem::copy_file(basicBin, legacyMultiBin, std::filesystem::copy_options::overwrite_existing);
			makeLegacy(legacyMultiBin, 2);
			rejected = false;
			try { serializer.deserialize(legacyMultiBin); } catch (...) { rejected = true; }
			if (!rejected) return fail("legacy multi-definition resource stream did not fail migration");
			auto legacyPbr = std::make_shared<ProgrammaticBasicMaterialStream>(resourceMgr);
			legacyPbr->setUniform("PBR_ENABLED", int32_t(1));
			legacyPbr->setUniform("PBR_METALLIC_FACTOR", 0.65f);
			legacyPbr->setUniform("PBR_ROUGHNESS_FACTOR", 0.35f);
			serializer.serialize(legacyPbr, legacyPbrBin);
			makeLegacy(legacyPbrBin);
			auto converted = serializer.deserialize(legacyPbrBin);
			auto convertedPbr = dynamic_cast<PbrMaterialStream*>(converted.get());
			if (!convertedPbr || convertedPbr->getPbrSurface().metallicFactor != 0.65f || convertedPbr->getPbrSurface().roughnessFactor != 0.35f) return fail("legacy PBR Material surface conversion failed");
			if (!convertedPbr->usesLegacyFullContract()) return fail("legacy PBR Material did not retain its temporary full-contract marker");
			auto gltf = root / "mpp-material-test.gltf";
			std::ofstream(gltf) << R"({"asset":{"version":"2.0"},"materials":[{"name":"First","pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1]}},{"name":"Second","emissiveTexture":{"index":0}}]})";
			auto names = GltfPbrMaterialLoader::listMaterialNames(gltf);
			if (names.size() != 2 || names[0] != "First" || names[1] != "Second") return fail("glTF material name listing failed");
			auto selected = GltfPbrMaterialLoader::loadMaterialByName(gltf, "Second");
			if (selected.materialIndex != 1 || selected.materialName != "Second") return fail("named glTF material loading failed");
			if (selected.definition.getEntry("Surface").getEntry("emissiveFactor").getValue() != "1.000000 1.000000 1.000000") return fail("glTF emissive texture did not receive white map multiplier");
			auto first = GltfPbrMaterialLoader::loadFirstMaterial(gltf);
			if (first.materialName != "First" || first.warnings.empty()) return fail("first glTF material warning failed");
			std::filesystem::remove(gltf);
		}
		catch (std::exception const& exception) { return fail(exception.what()); }
		std::filesystem::remove(basicXml); std::filesystem::remove(pbrXml); std::filesystem::remove(invalidPbrXml); std::filesystem::remove(embeddedVariantsXml);
		std::filesystem::remove(basicYaml); std::filesystem::remove(pbrYaml);
		std::filesystem::remove(basicBin); std::filesystem::remove(pbrBin);
		std::filesystem::remove(version2BasicBin); std::filesystem::remove(legacyBasicBin); std::filesystem::remove(legacyPbrBin); std::filesystem::remove(legacyMultiBin); std::filesystem::remove(hdrPipelineXml); std::filesystem::remove(hdrPipelineYaml);
		std::filesystem::remove(root, rootError);
		return true;
	}
}
