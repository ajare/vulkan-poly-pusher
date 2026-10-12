#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "mpp/resource-parsers/GltfPbrMaterialLoader.h"

namespace mpp::resource_parsers
{
	namespace
	{
		struct Json
		{
			enum class Type { Null, Boolean, Number, String, Array, Object } type{Type::Null};
			bool boolean{}; double number{}; std::string string; std::vector<Json> array; std::map<std::string, Json> object;
			Json const* get(std::string const& key) const { auto found = object.find(key); return found == object.end() ? nullptr : &found->second; }
		};
		class Parser
		{
			char const* current; char const* end;
			void space() { while (current != end && std::isspace((unsigned char)*current)) ++current; }
			void expect(char character) { space(); if (current == end || *current++ != character) throw std::runtime_error("Invalid glTF JSON."); }
			std::string string()
			{
				expect('\"'); std::string result;
				while (current != end && *current != '\"') { if (*current == '\\') { ++current; if (current == end) throw std::runtime_error("Invalid glTF JSON escape."); char value = *current++; result += value == 'n' ? '\n' : value == 'r' ? '\r' : value == 't' ? '\t' : value; } else result += *current++; }
				expect('\"'); return result;
			}
			Json value()
			{
				space(); if (current == end) throw std::runtime_error("Unexpected end of glTF JSON.");
				if (*current == '{') { ++current; Json result; result.type = Json::Type::Object; space(); while (current != end && *current != '}') { auto key = string(); expect(':'); result.object.emplace(std::move(key), value()); space(); if (*current != '}') expect(','); space(); } expect('}'); return result; }
				if (*current == '[') { ++current; Json result; result.type = Json::Type::Array; space(); while (current != end && *current != ']') { result.array.push_back(value()); space(); if (*current != ']') expect(','); space(); } expect(']'); return result; }
				if (*current == '\"') { Json result; result.type = Json::Type::String; result.string = string(); return result; }
				if (end - current >= 4 && std::string_view(current, 4) == "true") { current += 4; Json result; result.type = Json::Type::Boolean; result.boolean = true; return result; }
				if (end - current >= 5 && std::string_view(current, 5) == "false") { current += 5; Json result; result.type = Json::Type::Boolean; return result; }
				if (end - current >= 4 && std::string_view(current, 4) == "null") { current += 4; return {}; }
				char* next{}; Json result; result.type = Json::Type::Number; result.number = std::strtod(current, &next); if (next == current) throw std::runtime_error("Invalid glTF JSON value."); current = next; return result;
			}
		public:
			explicit Parser(std::string const& source) : current(source.data()), end(source.data() + source.size()) {}
			Json parse() { auto result = value(); space(); if (current != end) throw std::runtime_error("Trailing glTF JSON data."); return result; }
		};
	}

	GltfPbrMaterialLoadResult GltfPbrMaterialLoader::loadMaterialByName(std::filesystem::path const& filepath, std::string const& requestedName)
	{
		if (filepath.empty()) throw std::invalid_argument("glTF material loader requires a file path.");
		auto extension = filepath.extension().string(); for (auto& character : extension) character = (char)std::tolower((unsigned char)character);
		if (extension == ".glb") throw std::runtime_error("Binary .glb material loading is not supported yet; use JSON .gltf.");
		if (extension != ".gltf") throw std::runtime_error("glTF material loader requires a .gltf file: " + filepath.string());
		std::ifstream file(filepath, std::ios::binary); if (!file) throw std::runtime_error("Could not open glTF file: " + filepath.string());
		std::string source((std::istreambuf_iterator<char>(file)), {}); auto root = Parser(source).parse();
		if (root.type != Json::Type::Object) throw std::runtime_error("glTF root must be a JSON object.");
		auto asset = root.get("asset"); auto version = asset && asset->type == Json::Type::Object ? asset->get("version") : nullptr;
		if (!version || version->type != Json::Type::String || version->string.empty() || version->string.front() != '2') throw std::runtime_error("glTF asset.version must identify glTF 2.x.");
		auto materials = root.get("materials");
		if (!materials || materials->type != Json::Type::Array || materials->array.empty()) throw std::runtime_error("glTF file contains no materials.");
		size_t selected = materials->array.size();
		for (size_t index = 0; index < materials->array.size(); ++index) { auto name = materials->array[index].get("name"); auto candidate = name && name->type == Json::Type::String && !name->string.empty() ? name->string : filepath.stem().string() + ".Material" + std::to_string(index); if (candidate == requestedName) { selected = index; break; } }
		if (selected == materials->array.size()) throw std::runtime_error("glTF material not found: " + requestedName);
		auto const& material = materials->array[selected]; if (material.type != Json::Type::Object) throw std::runtime_error("Selected glTF material must be an object.");
		GltfPbrMaterialLoadResult result;
		result.materialIndex = (uint32_t)selected;
		auto name = material.get("name"); result.materialName = name && name->type == Json::Type::String && !name->string.empty() ? name->string : filepath.stem().string() + ".Material" + std::to_string(selected);
		result.definition = mpp::data::StructuredData("PbrMaterial"); result.definition.addEntry("name", result.materialName);
		mpp::data::StructuredData mesh("MeshSpecification"); mesh.addEntry("primitive", "triangles"); mesh.addEntry("indexed", "true"); mesh.addEntry("storage", "static");
		mpp::data::StructuredData buffer("Buffer"); for (auto const* data : {"position3", "normal3", "texcoord2"}) { mpp::data::StructuredData channel("Channel"); channel.addEntry("data", data); channel.addEntry("type", "float32"); buffer.addEntry("Channel", channel); } mpp::data::StructuredData colour("Channel"); colour.addEntry("data", "colour4"); colour.addEntry("type", "float32"); colour.addEntry("normalised", "true"); buffer.addEntry("Channel", colour); { mpp::data::StructuredData tangent("Channel"); tangent.addEntry("data", "tangent4"); tangent.addEntry("type", "float32"); buffer.addEntry("Channel", tangent); } mesh.addEntry("Buffer", buffer); result.definition.addEntry("MeshSpecification", mesh);
		mpp::data::StructuredData surface("Surface");
		auto scalar = [](Json const* value, float fallback) { return value && value->type == Json::Type::Number ? (float)value->number : fallback; };
		auto vector = [&](Json const* value, std::initializer_list<float> defaults) { std::vector<float> result(defaults); if (value && value->type == Json::Type::Array) for (size_t i = 0; i < result.size() && i < value->array.size(); ++i) result[i] = scalar(&value->array[i], result[i]); return result; };
		auto pbr = material.get("pbrMetallicRoughness");
		auto base = pbr && pbr->type == Json::Type::Object ? vector(pbr->get("baseColorFactor"), {1, 1, 1, 1}) : std::vector<float>{1, 1, 1, 1};
		surface.addEntry("baseColourFactor", std::to_string(base[0])+" "+std::to_string(base[1])+" "+std::to_string(base[2])+" "+std::to_string(base[3]));
		surface.addEntry("metallicFactor", std::to_string(pbr && pbr->type == Json::Type::Object ? scalar(pbr->get("metallicFactor"), 1) : 1));
		surface.addEntry("roughnessFactor", std::to_string(pbr && pbr->type == Json::Type::Object ? scalar(pbr->get("roughnessFactor"), 1) : 1));
		// PipelineEditor map-mode semantics use a white multiplier. glTF assets
		// commonly omit emissiveFactor when an emissiveTexture is authored; retain
		// an explicit factor when present, otherwise let that texture contribute.
		auto emissiveTexture = material.get("emissiveTexture");
		auto emissive = emissiveTexture ? vector(material.get("emissiveFactor"), {1, 1, 1}) : vector(material.get("emissiveFactor"), {0, 0, 0}); surface.addEntry("emissiveFactor", std::to_string(emissive[0])+" "+std::to_string(emissive[1])+" "+std::to_string(emissive[2]));
		surface.addEntry("normalScale", "1"); surface.addEntry("occlusionStrength", "1");
		auto alpha = material.get("alphaMode"); surface.addEntry("alphaMode", alpha && alpha->type == Json::Type::String ? alpha->string : "OPAQUE");
		surface.addEntry("alphaCutoff", std::to_string(scalar(material.get("alphaCutoff"), 0.5f)));
		auto sided = material.get("doubleSided"); surface.addEntry("doubleSided", sided && sided->type == Json::Type::Boolean && sided->boolean ? "true" : "false");
		result.definition.addEntry("Surface", surface);
		auto textures = root.get("textures"), images = root.get("images"), buffers = root.get("buffers"), views = root.get("bufferViews");
		std::map<size_t, std::filesystem::path> extracted;
		auto decode = [](std::string const& encoded) { static std::string const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::vector<uint8_t> bytes; int value = 0, bits = -8; for (char c : encoded) { if (c == '=') break; auto index = alphabet.find(c); if (index == std::string::npos) continue; value = value << 6 | (int)index; bits += 6; if (bits >= 0) { bytes.push_back((uint8_t)(value >> bits)); bits -= 8; } } return bytes; };
		auto texturePath = [&](Json const* texture) -> std::filesystem::path
		{
			if (!texture || texture->type != Json::Type::Object || !textures || !images || textures->type != Json::Type::Array || images->type != Json::Type::Array) return {};
			auto index = texture->get("index"); if (!index || index->type != Json::Type::Number || index->number < 0 || (size_t)index->number >= textures->array.size()) return {};
			auto sourceIndex = textures->array[(size_t)index->number].get("source"); if (!sourceIndex || sourceIndex->type != Json::Type::Number || sourceIndex->number < 0 || (size_t)sourceIndex->number >= images->array.size()) return {}; auto imageIndex = (size_t)sourceIndex->number; auto const& image = images->array[imageIndex];
			auto uri = image.get("uri"); if (uri && uri->type == Json::Type::String && !uri->string.starts_with("data:")) return (filepath.parent_path() / std::filesystem::path(uri->string)).lexically_normal(); if (auto found = extracted.find(imageIndex); found != extracted.end()) return found->second;
			std::vector<uint8_t> bytes; std::string mime = image.get("mimeType") && image.get("mimeType")->type == Json::Type::String ? image.get("mimeType")->string : "image/png";
			if (uri && uri->type == Json::Type::String && uri->string.starts_with("data:")) { auto comma = uri->string.find(','); if (comma == std::string::npos) return {}; bytes = decode(uri->string.substr(comma + 1)); }
			else { auto view = image.get("bufferView"); if (!view || !views || !buffers || view->type != Json::Type::Number || (size_t)view->number >= views->array.size()) return {}; auto const& bufferView = views->array[(size_t)view->number]; auto buffer = bufferView.get("buffer"); if (!buffer || buffer->type != Json::Type::Number || (size_t)buffer->number >= buffers->array.size()) return {}; auto sourceUri = buffers->array[(size_t)buffer->number].get("uri"); if (!sourceUri || sourceUri->type != Json::Type::String || !sourceUri->string.starts_with("data:")) return {}; auto comma = sourceUri->string.find(','); auto source = comma == std::string::npos ? std::vector<uint8_t>{} : decode(sourceUri->string.substr(comma + 1)); auto offset = bufferView.get("byteOffset") ? (size_t)bufferView.get("byteOffset")->number : 0; auto length = bufferView.get("byteLength") ? (size_t)bufferView.get("byteLength")->number : 0; if (offset + length > source.size()) return {}; bytes.assign(source.begin() + offset, source.begin() + offset + length); }
			if (bytes.empty()) return {}; auto directory = filepath.parent_path() / ".mpp-gltf-images"; std::filesystem::create_directories(directory); auto output = directory / (filepath.stem().string() + ".image" + std::to_string(imageIndex) + (mime == "image/jpeg" ? ".jpg" : ".png")); std::ofstream extractedFile(output, std::ios::binary); extractedFile.write((char const*)bytes.data(), bytes.size()); if (!extractedFile) return {}; extracted.emplace(imageIndex, output); return output;
		};
		auto addMap = [&](char const* name, Json const* texture, char const* colourSpace)
		{
			auto imagePath = texturePath(texture); if (imagePath.empty()) { if (texture) result.warnings.push_back(std::string("glTF ") + name + " image is embedded or unresolved and was not imported yet."); return; }
			mpp::data::StructuredData image("Texture"); image.addEntry("name", result.materialName + "." + name); image.addEntry("target", "2D"); image.addEntry("filename", imagePath.string()); image.addEntry("colourSpace", colourSpace); image.addEntry("minFilter", "LINEAR"); image.addEntry("magFilter", "LINEAR"); image.addEntry("wrap", "REPEAT");
			mpp::data::StructuredData map(name); map.addEntry("Resource", image); result.definition.addEntry(name, map); result.generatedImages.push_back(imagePath);
		};
		if (pbr && pbr->type == Json::Type::Object) { addMap("BaseColourMap", pbr->get("baseColorTexture"), "SRGB"); addMap("MetallicRoughnessMap", pbr->get("metallicRoughnessTexture"), "LINEAR"); }
		addMap("NormalMap", material.get("normalTexture"), "LINEAR"); if (auto normal = material.get("normalTexture")) result.definition.getEntry("Surface").setEntryValue("normalScale", std::to_string(scalar(normal->get("scale"), 1)));
		addMap("OcclusionMap", material.get("occlusionTexture"), "LINEAR"); if (auto occlusion = material.get("occlusionTexture")) result.definition.getEntry("Surface").setEntryValue("occlusionStrength", std::to_string(scalar(occlusion->get("strength"), 1)));
		addMap("EmissiveMap", material.get("emissiveTexture"), "SRGB");
		return result;
	}

	std::vector<std::string> GltfPbrMaterialLoader::listMaterialNames(std::filesystem::path const& filepath)
	{
		if (filepath.extension() != ".gltf") throw std::runtime_error("glTF material listing requires a JSON .gltf file.");
		std::ifstream file(filepath, std::ios::binary); if (!file) throw std::runtime_error("Could not open glTF file: " + filepath.string());
		std::string source((std::istreambuf_iterator<char>(file)), {}); auto root = Parser(source).parse(); auto materials = root.get("materials");
		if (!materials || materials->type != Json::Type::Array || materials->array.empty()) throw std::runtime_error("glTF file contains no materials.");
		std::vector<std::string> names;
		for (size_t index = 0; index < materials->array.size(); ++index) { auto name = materials->array[index].get("name"); auto candidate = name && name->type == Json::Type::String && !name->string.empty() ? name->string : filepath.stem().string() + ".Material" + std::to_string(index); while (std::find(names.begin(), names.end(), candidate) != names.end()) candidate += "." + std::to_string(index + 1); names.push_back(std::move(candidate)); }
		return names;
	}

	GltfPbrMaterialLoadResult GltfPbrMaterialLoader::loadFirstMaterial(std::filesystem::path const& filepath)
	{
		auto names = listMaterialNames(filepath); auto result = loadMaterialByName(filepath, names.front());
		if (names.size() > 1) result.warnings.push_back("glTF defines " + std::to_string(names.size()) + " materials; MPP loaded material 0 ('" + result.materialName + "') and ignored the remaining materials.");
		return result;
	}
}
