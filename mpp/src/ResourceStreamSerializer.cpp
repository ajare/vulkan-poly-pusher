#include "mpp/program/ShaderStage.h"

#include "mpp/ResourceStreamSerializer.h"
#include "mpp/ResourceManager.h"
#include "mpp/BasicMaterialStream.h"
#include "mpp/ProgramStream.h"
#include "mpp/SamplerStream.h"
#include "mpp/StringStream.h"
#include "mpp/TextureStream.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/PbrMaterialStream.h"
#include "mpp/ProgrammaticPbrMaterialStream.h"
#include "mpp/PostEffectMaterialStream.h"
#include "mpp/ProgrammaticPostEffectMaterialStream.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticSamplerStream.h"
#include "mpp/ProgrammaticStringStream.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/MppException.h"

namespace mpp
{
	using namespace std;

	ResourceStreamSerializer::ResourceStreamSerializer(ResourceManager* resourceMgr)
		: mResourceMgr(resourceMgr)
	{
	}

	void ResourceStreamSerializer::setGlobalMeshSpecification(mesh::MeshSpecification const& meshSpec)
	{
		mUseGlobalMeshSpec = true;
		mMeshSpec = meshSpec;
	}

	void ResourceStreamSerializer::serialize(ResourceStreamPtr resourceStream, string const& filename)
	{
		// Open file
		ofstream fp(filename, ios::out | ios::binary);

		if (!fp.is_open())
		{
			THROW_MPP_IO("Could not open " + filename + " for writing", __LINE__, __FILE__, __func__);
		}

		serialize(resourceStream, fp);

		fp.close();
	}

	void ResourceStreamSerializer::serialize(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		// RSE5 appends the PbrSurface water block to RSE4's layout. RSE4 added each texture's
		// colour space to RSE3's layout. RSE3 stores one definition per stream. RSER/RSE2 remain
		// strict compatibility inputs only when they contain one legacy definition. All earlier
		// versions still read; only RSE5 is written.
		char const* magic{ "RSE5" };
		fp.write(magic, 4);

		// Recursively write streams and their children
		writeStream(resourceStream, fp);
	}

	ResourceStreamPtr ResourceStreamSerializer::deserialize(string const& filename)
	{
		// Open file
		ifstream fp(filename, ios::in | ios::binary);

		if (!fp.is_open())
		{
			THROW_MPP_IO("Could not open " + filename + " for reading", __LINE__, __FILE__, __func__);
		}

		return deserialize(fp);
	}

	ResourceStreamPtr ResourceStreamSerializer::deserialize(ifstream& fp)
	{
		char magic[4];
		fp.read(magic, 4);

		bool const v1 = magic[0] == 'R' && magic[1] == 'S' && magic[2] == 'E' && magic[3] == 'R';
		bool const v2 = magic[0] == 'R' && magic[1] == 'S' && magic[2] == 'E' && magic[3] == '2';
		bool const v3 = magic[0] == 'R' && magic[1] == 'S' && magic[2] == 'E' && magic[3] == '3';
		bool const v4 = magic[0] == 'R' && magic[1] == 'S' && magic[2] == 'E' && magic[3] == '4';
		bool const v5 = magic[0] == 'R' && magic[1] == 'S' && magic[2] == 'E' && magic[3] == '5';
		if (!v1 && !v2 && !v3 && !v4 && !v5)
		{
			THROW_MPP_IO("Could not open stream for reading. Not a valid or supported format.", __LINE__, __FILE__, __func__);
		}

		mReadVersion = v5 ? 5u : (v4 ? 4u : (v3 ? 3u : (v2 ? 2u : 1u)));
		return readStream(fp);
	}

	void ResourceStreamSerializer::writeValue(string const& value, ofstream& fp)
	{
		uint32_t len = (uint32_t)value.length();
		fp.write((char const*)&len, sizeof(len));

		if (len > 0)
		{
			fp.write(value.c_str(), len);
		}
	}

	void ResourceStreamSerializer::writeValue(int32_t value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ResourceStreamSerializer::writeValue(uint32_t value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ResourceStreamSerializer::writeValue(float value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ResourceStreamSerializer::writeValue(bool value, ofstream& fp)
	{
		char vi = value ? 1 : 0;
		fp.write((char const*)&vi, sizeof(vi));
	}

	void ResourceStreamSerializer::writeMeshSpecification(mesh::MeshSpecification const& meshSpec, ofstream& fp)
	{
		auto const& ms = mUseGlobalMeshSpec ? mMeshSpec : meshSpec;

		writeValue((uint32_t)ms.getPrimitiveType(), fp);
		writeValue((uint32_t)ms.getStorageType(), fp);
		writeValue(ms.verticesIndexed(), fp);

		// Write layouts
		writeValue((uint32_t)ms.getNumVertexBufferAttributeLayouts(), fp);
		for (size_t i = 0; i < ms.getNumVertexBufferAttributeLayouts(); ++i)
		{
			auto const& layout = ms.getVertexBufferAttributeLayout((uint32_t)i);

			writeValue(layout.isStatic(), fp);

			// Write attributes
			writeValue((uint32_t)layout.getNumAttributes(), fp);
			for (size_t j = 0; j < layout.getNumAttributes(); ++j)
			{
				auto const& attrib = layout.getAttribute(j);

				writeValue((uint32_t)attrib.component, fp);
				writeValue((uint32_t)attrib.dataType, fp);
				writeValue((uint32_t)attrib.padToBoundary, fp);
				writeValue(attrib.normalised, fp);
			}
		}
	}

	void ResourceStreamSerializer::writeUniformCollection(UniformCollection const& uniforms, ofstream& fp)
	{
		writeValue((uint32_t)uniforms.getNumUniforms(), fp);

		auto const& uniformData = uniforms.getUniformData();

		for (auto const& kvp: uniformData)
		{
			auto const& data = kvp.second;

			writeValue(data.name, fp);
			writeValue((uint32_t)data.type, fp);
			writeValue((uint32_t)data.size, fp);

			fp.write(data.data, 64);
		}
	}

	void ResourceStreamSerializer::writeParser(program::Parser const& parser, ofstream& fp)
	{
		writeValue(parser.getName(), fp);

		// Shader stages
		for (uint32_t i = 0; i < (uint32_t)program::ShaderStage::Type::NumStages; ++i)
		{
			auto const& stage = parser.getStage(i);

			writeValue(stage.inputSource, fp);
		}

		writeMeshSpecification(parser.getMeshSpecification(), fp);
	}

	void ResourceStreamSerializer::writeBasicMaterialStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<BasicMaterialStream*>(resourceStream.get());
		auto const& spec = stream->mSpecification;
		writeValue(spec.program.resourceExists, fp); writeValue(spec.program.existingResource, fp); writeValue(spec.program.isChild, fp); writeValue(spec.program.is2d, fp);
		writeMeshSpecification(spec.program.spec, fp);
		writeValue((uint32_t)spec.program.vertexShader.type, fp); writeValue(spec.program.vertexShader.data, fp);
		writeValue((uint32_t)spec.program.geometryShader.type, fp); writeValue(spec.program.geometryShader.data, fp);
		writeValue((uint32_t)spec.program.fragmentShader.type, fp); writeValue(spec.program.fragmentShader.data, fp);
		writeUniformCollection(spec.uniforms, fp);
		writeValue((uint32_t)spec.textures.size(), fp);
		auto writeTextureOptions = [&](auto const& texture)
		{
			writeValue(texture.resourceExists, fp); writeValue(texture.existingResource, fp); writeValue(texture.isChild, fp);
			writeValue(texture.sampler, fp); writeValue(texture.source, fp); writeValue((uint32_t)texture.target, fp);
			writeValue(texture.params.minFilter, fp); writeValue(texture.params.magFilter, fp); writeValue(texture.params.wrap, fp);
			writeValue(texture.params.useMipmaps, fp); writeValue(texture.params.lodBaseLevel, fp); writeValue(texture.params.lodMaxLevel, fp);
			writeValue(texture.params.lodBias, fp); writeValue(texture.params.maxAnisotropy, fp);
			// RSE4: colour space. Without it an embedded sRGB base-colour or emissive map reloads
			// as TextureParams' Linear default and shades incorrectly.
			writeValue((uint32_t)texture.params.colourSpace, fp);
		};
		for (auto const& texture : spec.textures) writeTextureOptions(texture);
	}

	void ResourceStreamSerializer::writePbrMaterialStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<PbrMaterialStream*>(resourceStream.get());
		auto const& spec = stream->mSpecification;
		writeValue(spec.program.resourceExists, fp); writeValue(spec.program.existingResource, fp); writeValue(spec.program.isChild, fp); writeValue(spec.program.is2d, fp);
		writeMeshSpecification(spec.program.spec, fp);
		writeValue((uint32_t)spec.program.vertexShader.type, fp); writeValue(spec.program.vertexShader.data, fp);
		writeValue((uint32_t)spec.program.geometryShader.type, fp); writeValue(spec.program.geometryShader.data, fp);
		writeValue((uint32_t)spec.program.fragmentShader.type, fp); writeValue(spec.program.fragmentShader.data, fp);
		writeUniformCollection(spec.uniforms, fp);
		writeValue(spec.pbr.enabled, fp);
		writeValue(spec.pbr.baseColourFactor.x, fp); writeValue(spec.pbr.baseColourFactor.y, fp); writeValue(spec.pbr.baseColourFactor.z, fp); writeValue(spec.pbr.baseColourFactor.w, fp);
		writeValue(spec.pbr.metallicFactor, fp); writeValue(spec.pbr.roughnessFactor, fp);
		writeValue(spec.pbr.emissiveFactor.x, fp); writeValue(spec.pbr.emissiveFactor.y, fp); writeValue(spec.pbr.emissiveFactor.z, fp);
		writeValue(spec.pbr.normalScale, fp); writeValue(spec.pbr.occlusionStrength, fp); writeValue((uint32_t)spec.pbr.alphaMode, fp);
		writeValue(spec.pbr.alphaCutoff, fp); writeValue(spec.pbr.doubleSided, fp);
		// RSE5: the water block. Older readers stop before it and leave PbrWater's
		// defaults, which is the correct meaning for a stream authored without water.
		writeValue(spec.pbr.water.enabled, fp);
		writeValue(spec.pbr.water.distortionScale, fp); writeValue(spec.pbr.water.distortionStrength, fp);
		writeValue(spec.pbr.water.scrollSpeed.x, fp); writeValue(spec.pbr.water.scrollSpeed.y, fp);
		writeValue(spec.pbr.water.microRoughness, fp); writeValue(spec.pbr.water.ssrMaxDistance, fp);
		writeValue(spec.pbr.water.ssrSteps, fp); writeValue(spec.pbr.water.ssrThickness, fp);
		writeValue(spec.pbr.water.edgeFade, fp);
		writeValue(spec.pbr.water.grazingFallbackStart, fp); writeValue(spec.pbr.water.grazingFallbackEnd, fp);
		writeValue((uint32_t)spec.textures.size(), fp);
		auto writeTextureOptions = [&](auto const& texture)
		{
			writeValue(texture.resourceExists, fp); writeValue(texture.existingResource, fp); writeValue(texture.isChild, fp);
			writeValue(texture.sampler, fp); writeValue(texture.source, fp); writeValue((uint32_t)texture.target, fp);
			writeValue(texture.params.minFilter, fp); writeValue(texture.params.magFilter, fp); writeValue(texture.params.wrap, fp);
			writeValue(texture.params.useMipmaps, fp); writeValue(texture.params.lodBaseLevel, fp); writeValue(texture.params.lodMaxLevel, fp);
			writeValue(texture.params.lodBias, fp); writeValue(texture.params.maxAnisotropy, fp);
			// RSE4: colour space. Without it an embedded sRGB base-colour or emissive map reloads
			// as TextureParams' Linear default and shades incorrectly.
			writeValue((uint32_t)texture.params.colourSpace, fp);
		};
		for (auto const& texture : spec.textures) writeTextureOptions(texture);
	}

	void ResourceStreamSerializer::writePostEffectMaterialStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<PostEffectMaterialStream*>(resourceStream.get());
		auto const& spec = stream->mSpecification;
		writeValue(spec.program.resourceExists, fp); writeValue(spec.program.existingResource, fp); writeValue(spec.program.isChild, fp); writeValue(spec.program.is2d, fp);
		writeMeshSpecification(spec.program.spec, fp);
		writeValue((uint32_t)spec.program.vertexShader.type, fp); writeValue(spec.program.vertexShader.data, fp);
		writeValue((uint32_t)spec.program.geometryShader.type, fp); writeValue(spec.program.geometryShader.data, fp);
		writeValue((uint32_t)spec.program.fragmentShader.type, fp); writeValue(spec.program.fragmentShader.data, fp);
		writeUniformCollection(spec.uniforms, fp);
		writeValue((uint32_t)spec.samplerSlots.size(), fp);
		for (auto const& slot : spec.samplerSlots) writeValue(slot, fp);
	}

	void ResourceStreamSerializer::writeProgramStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<ProgramStream*>(resourceStream.get());
		writeValue(stream->mVertexSource, fp); writeValue(stream->mGeometrySource, fp); writeValue(stream->mFragmentSource, fp);
		writeParser(*stream->mParser, fp);
		writeValue((uint32_t)stream->mAttribs.size(), fp);
		for (auto const& attrib : stream->mAttribs) writeValue(attrib, fp);
	}

	void ResourceStreamSerializer::writeSamplerStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto const& params = static_cast<SamplerStream*>(resourceStream.get())->mParams;
		writeValue(params.minFilter, fp); writeValue(params.magFilter, fp); writeValue(params.wrap, fp);
		writeValue(params.lodMinLevel, fp); writeValue(params.lodMaxLevel, fp); writeValue(params.lodBias, fp); writeValue(params.maxAnisotropy, fp);
	}

	void ResourceStreamSerializer::writeStringStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<StringStream*>(resourceStream.get());
		writeValue(stream->mData, fp); writeValue(stream->mFile, fp); writeValue(stream->mIsFile, fp);
	}

	void ResourceStreamSerializer::writeTextureStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		auto stream = static_cast<TextureStream*>(resourceStream.get());
		writeValue((uint32_t)0, fp); // legacy tile-list placeholder
		auto const& definition = stream->mDefinition;
		writeValue(definition.target, fp); writeValue(definition.internalFormat, fp);
		writeValue(definition.params.minFilter, fp); writeValue(definition.params.magFilter, fp); writeValue(definition.params.wrap, fp);
		writeValue(definition.params.useMipmaps, fp); writeValue(definition.params.lodBaseLevel, fp); writeValue(definition.params.lodMaxLevel, fp);
		writeValue(definition.params.lodBias, fp); writeValue(definition.params.maxAnisotropy, fp);
		// RSE4: see the matching comment in the material texture-options writers.
		writeValue((uint32_t)definition.params.colourSpace, fp);
		writeValue(definition.sampler, fp); writeValue(definition.source, fp);
	}

	void ResourceStreamSerializer::writeStream(ResourceStreamPtr resourceStream, ofstream& fp)
	{
		// Write type
		auto const& streamType = resourceStream->getType();
		writeValue(streamType, fp);

		// Write children
		auto const& children = resourceStream->getChildren();

		writeValue((uint32_t)children.size(), fp);
		for (auto const& kvp: children)
		{
			auto const& name = kvp.first;
			auto const& child = kvp.second;

			writeValue(name, fp);
			writeStream(child, fp);
		}

		// Write type-specific data
		if (streamType == "BasicMaterial" || streamType == "Material")
		{
			writeBasicMaterialStream(resourceStream, fp);
		}
		else if (streamType == "PbrMaterial")
		{
			writePbrMaterialStream(resourceStream, fp);
		}
		else if (streamType == "PostEffectMaterial")
		{
			writePostEffectMaterialStream(resourceStream, fp);
		}
		else if (streamType == "Program")
		{
			writeProgramStream(resourceStream, fp);
		}
		else if (streamType == "Sampler")
		{
			writeSamplerStream(resourceStream, fp);
		}
		else if (streamType == "String")
		{
			writeStringStream(resourceStream, fp);
		}
		else if (streamType == "Texture")
		{
			writeTextureStream(resourceStream, fp);
		}
		else
		{
			string errMsg = "Cannot serialize ResourceStream of type '" + streamType + "'.";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}
	}

	string ResourceStreamSerializer::readString(ifstream& fp)
	{
		uint32_t len;
		fp.read((char*)&len, sizeof(len));

		if (len > 0)
		{
			char* buffer = new char[len + 1];
			fp.read(buffer, len);

			buffer[len] = '\0';

			string value(buffer);
			delete[] buffer;

			return value;
		}
		else
		{
			return "";
		}
	}

	int32_t ResourceStreamSerializer::readInt(ifstream& fp)
	{
		int32_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	uint32_t ResourceStreamSerializer::readUInt(ifstream& fp)
	{
		uint32_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	float ResourceStreamSerializer::readFloat(ifstream& fp)
	{
		float value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	bool ResourceStreamSerializer::readBool(ifstream& fp)
	{
		char value;

		fp.read((char*)&value, sizeof(value));
		return value != 0;
	}

	mesh::MeshSpecification ResourceStreamSerializer::readMeshSpecification(ifstream& fp)
	{
		mesh::MeshSpecification meshSpec;

		auto primitiveType = static_cast<mesh::Primitive::Type>(readUInt(fp));
		meshSpec.setPrimitiveType(primitiveType);

		auto storageType = static_cast<mesh::VertexBufferStorageType>(readUInt(fp));
		meshSpec.setStorageType(storageType);

		auto indexed = readBool(fp);
		meshSpec.setIndexedVertices(indexed);

		uint32_t numLayouts = readUInt(fp);
		for (uint32_t i = 0; i < numLayouts; ++i)
		{
			auto isStatic = readBool(fp);

			auto layout = meshSpec.createVertexBufferAttributeLayout(isStatic);

			uint32_t numAttribs = readUInt(fp);
			for (uint32_t j = 0; j < numAttribs; ++j)
			{
				auto component = static_cast<mesh::Vertex::Component>(readUInt(fp));
				auto datatype = static_cast<mesh::Vertex::DataType>(readUInt(fp));
				auto padToBoundary = readInt(fp);
				auto normalised = readBool(fp);

				// Need to sort out paddingBytes
				layout->createAttribute(component, datatype, normalised, padToBoundary);
			}
		}

		return mUseGlobalMeshSpec ? mMeshSpec : meshSpec;
	}

	UniformCollection ResourceStreamSerializer::readUniformCollection(ifstream& fp)
	{
		UniformCollection uniforms;

		uint32_t numUniforms = readUInt(fp);
		for (uint32_t i = 0; i < numUniforms; ++i)
		{
			string name = readString(fp);
			auto type = static_cast<program::GLSLType>(readUInt(fp));
			auto size = readUInt(fp);

			char data[64];
			fp.read(data, 64);

			// The legacy stream stores a value's byte size, rather than count and
			// component count. Decode a single scalar/vector value from that size.
			size_t componentSize = 0;
			switch (type)
			{
			case program::GLSLType::Bool:
			case program::GLSLType::Int:
			case program::GLSLType::Uint:
			case program::GLSLType::Float:
				componentSize = 4;
				break;
			case program::GLSLType::Double:
				componentSize = 8;
				break;
			default:
				THROW_MPP("Unsupported serialized uniform type.", __LINE__, __FILE__, __func__);
			}
			if (size == 0 || size > sizeof(data) || size % componentSize != 0)
			{
				THROW_MPP("Invalid serialized uniform size.", __LINE__, __FILE__, __func__);
			}
			size_t numElements = size / componentSize;

			// UniformData stores the generated GLSL name. setUniform() adds that
			// markup itself, so convert serialized names back to their authored
			// form rather than generating a double-prefixed uniform name.
			constexpr char uniformPrefix[] = "_mpp_u_";
			const size_t prefixLength = sizeof(uniformPrefix) - 1;
			if (name.size() > prefixLength && name.compare(0, prefixLength, uniformPrefix) == 0 && name.back() == '_')
			{
				name = name.substr(prefixLength, name.size() - prefixLength - 1);
			}
			uniforms.setUniform(name, type, 1, numElements, data);
		}

		return uniforms;
	}

	shared_ptr<program::Parser> ResourceStreamSerializer::readParser(ifstream& fp)
	{
		auto name = readString(fp);

		auto parser = make_shared<program::Parser>(name);

		// Shader stages
		for (uint32_t i = 0; i < (uint32_t)program::ShaderStage::Type::NumStages; ++i)
		{
			auto source = readString(fp);
			
			switch (i)
			{
			case (uint32_t)program::ShaderStage::Type::Vertex:
				parser->setVertexSource(source);
				break;

			case (uint32_t)program::ShaderStage::Type::Geometry:
				parser->setGeometrySource(source);
				break;

			case (uint32_t)program::ShaderStage::Type::Fragment:
				parser->setFragmentSource(source);
				break;
			}
		}

		auto meshSpec = readMeshSpecification(fp);
		parser->setMeshSpecification(meshSpec);

		return parser;
	}

	void ResourceStreamSerializer::readBasicMaterialStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy BasicMaterial contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		auto& spec = static_cast<ProgrammaticBasicMaterialStream*>(resourceStream.get())->mSpecification;
		spec = {};
		spec.program.resourceExists = readBool(fp); spec.program.existingResource = readString(fp); spec.program.isChild = readBool(fp); spec.program.is2d = readBool(fp);
		spec.program.spec = readMeshSpecification(fp);
		spec.program.vertexShader.type = static_cast<BasicMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.vertexShader.data = readString(fp);
		spec.program.geometryShader.type = static_cast<BasicMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.geometryShader.data = readString(fp);
		spec.program.fragmentShader.type = static_cast<BasicMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.fragmentShader.data = readString(fp);
		spec.uniforms = readUniformCollection(fp);
		auto readTextureOptions = [&]()
		{
			decltype(spec.textures)::value_type texture;
			texture.resourceExists = readBool(fp); texture.existingResource = readString(fp); texture.isChild = readBool(fp);
			texture.sampler = readString(fp); texture.source = readString(fp); texture.target = static_cast<TextureTarget>(readUInt(fp));
			texture.params.minFilter = readUInt(fp); texture.params.magFilter = readUInt(fp); texture.params.wrap = readUInt(fp);
			texture.params.useMipmaps = readBool(fp); texture.params.lodBaseLevel = readInt(fp); texture.params.lodMaxLevel = readInt(fp);
			texture.params.lodBias = readFloat(fp); texture.params.maxAnisotropy = readFloat(fp);
			// RSE4 onwards. Older streams simply lack the field and keep TextureParams' default.
			if (mReadVersion >= 4) texture.params.colourSpace = static_cast<TextureColourSpace>(readUInt(fp));
			return texture;
		};
		uint32_t count = readUInt(fp); for (uint32_t i = 0; i < count; ++i) spec.textures.push_back(readTextureOptions());
	}

	void ResourceStreamSerializer::readPbrMaterialStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy PbrMaterial contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		auto& spec = static_cast<ProgrammaticPbrMaterialStream*>(resourceStream.get())->mSpecification;
		spec = {};
		spec.program.resourceExists = readBool(fp); spec.program.existingResource = readString(fp); spec.program.isChild = readBool(fp); spec.program.is2d = readBool(fp);
		spec.program.spec = readMeshSpecification(fp);
		spec.program.vertexShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.vertexShader.data = readString(fp);
		spec.program.geometryShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.geometryShader.data = readString(fp);
		spec.program.fragmentShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.fragmentShader.data = readString(fp);
		spec.uniforms = readUniformCollection(fp);
		spec.pbr.enabled = readBool(fp);
		spec.pbr.baseColourFactor = { readFloat(fp), readFloat(fp), readFloat(fp), readFloat(fp) };
		spec.pbr.metallicFactor = readFloat(fp); spec.pbr.roughnessFactor = readFloat(fp);
		spec.pbr.emissiveFactor = { readFloat(fp), readFloat(fp), readFloat(fp) };
		spec.pbr.normalScale = readFloat(fp); spec.pbr.occlusionStrength = readFloat(fp);
		spec.pbr.alphaMode = static_cast<PbrMaterialSpecification::PbrAlphaMode>(readUInt(fp)); spec.pbr.alphaCutoff = readFloat(fp); spec.pbr.doubleSided = readBool(fp);
		// RSE5 onwards. Older streams simply lack the block and keep PbrWater's defaults.
		if (mReadVersion >= 5)
		{
			spec.pbr.water.enabled = readBool(fp);
			spec.pbr.water.distortionScale = readFloat(fp); spec.pbr.water.distortionStrength = readFloat(fp);
			spec.pbr.water.scrollSpeed = { readFloat(fp), readFloat(fp) };
			spec.pbr.water.microRoughness = readFloat(fp); spec.pbr.water.ssrMaxDistance = readFloat(fp);
			spec.pbr.water.ssrSteps = readInt(fp); spec.pbr.water.ssrThickness = readFloat(fp);
			spec.pbr.water.edgeFade = readFloat(fp);
			spec.pbr.water.grazingFallbackStart = readFloat(fp); spec.pbr.water.grazingFallbackEnd = readFloat(fp);
		}
		auto readTextureOptions = [&]()
		{
			decltype(spec.textures)::value_type texture;
			texture.resourceExists = readBool(fp); texture.existingResource = readString(fp); texture.isChild = readBool(fp);
			texture.sampler = readString(fp); texture.source = readString(fp); texture.target = static_cast<TextureTarget>(readUInt(fp));
			texture.params.minFilter = readUInt(fp); texture.params.magFilter = readUInt(fp); texture.params.wrap = readUInt(fp);
			texture.params.useMipmaps = readBool(fp); texture.params.lodBaseLevel = readInt(fp); texture.params.lodMaxLevel = readInt(fp);
			texture.params.lodBias = readFloat(fp); texture.params.maxAnisotropy = readFloat(fp);
			// RSE4 onwards. Older streams simply lack the field and keep TextureParams' default.
			if (mReadVersion >= 4) texture.params.colourSpace = static_cast<TextureColourSpace>(readUInt(fp));
			return texture;
		};
		uint32_t count = readUInt(fp); for (uint32_t i = 0; i < count; ++i) spec.textures.push_back(readTextureOptions());
	}

	void ResourceStreamSerializer::readPostEffectMaterialStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		auto& spec = static_cast<ProgrammaticPostEffectMaterialStream*>(resourceStream.get())->mSpecification;
		spec = {};
		spec.program.resourceExists = readBool(fp); spec.program.existingResource = readString(fp); spec.program.isChild = readBool(fp); spec.program.is2d = readBool(fp);
		spec.program.spec = readMeshSpecification(fp);
		spec.program.vertexShader.type = static_cast<PostEffectMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.vertexShader.data = readString(fp);
		spec.program.geometryShader.type = static_cast<PostEffectMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.geometryShader.data = readString(fp);
		spec.program.fragmentShader.type = static_cast<PostEffectMaterialSpecification::ProgramOptions::Shader::Type>(readUInt(fp)); spec.program.fragmentShader.data = readString(fp);
		spec.uniforms = readUniformCollection(fp);
		uint32_t count = readUInt(fp); for (uint32_t i = 0; i < count; ++i) spec.samplerSlots.push_back(readString(fp));
	}

	void ResourceStreamSerializer::readProgramStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		auto stream = static_cast<ProgrammaticProgramStream*>(resourceStream.get());
		stream->mVertexSource = readString(fp); stream->mGeometrySource = readString(fp); stream->mFragmentSource = readString(fp);
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy Program contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		stream->mParser = readParser(fp);
		uint32_t count = readUInt(fp); for (uint32_t i = 0; i < count; ++i) stream->mAttribs.insert(readString(fp));
	}

	void ResourceStreamSerializer::readSamplerStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy Sampler contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		auto& params = static_cast<ProgrammaticSamplerStream*>(resourceStream.get())->mParams;
		params.minFilter = readUInt(fp); params.magFilter = readUInt(fp); params.wrap = readUInt(fp);
		params.lodMinLevel = readFloat(fp); params.lodMaxLevel = readFloat(fp); params.lodBias = readFloat(fp); params.maxAnisotropy = readFloat(fp);
	}

	void ResourceStreamSerializer::readStringStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy String contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		auto stream = static_cast<ProgrammaticStringStream*>(resourceStream.get());
		stream->mData = readString(fp); stream->mFile = readString(fp); stream->mIsFile = readBool(fp);
	}

	void ResourceStreamSerializer::readTextureStream(ResourceStreamPtr resourceStream, ifstream& fp)
	{
		auto stream = static_cast<ProgrammaticTextureStream*>(resourceStream.get());
		uint32_t tiles = readUInt(fp);
		for (uint32_t i = 0; i < tiles; ++i) { readString(fp); readFloat(fp); readFloat(fp); readFloat(fp); readFloat(fp); }
		if (mReadVersion < 3 && readUInt(fp) != 1) THROW_MPP("Legacy Texture contains multiple quality definitions; split and re-export it.", __LINE__, __FILE__, __func__);
		auto& definition = stream->mDefinition;
		definition.target = readUInt(fp); definition.internalFormat = readUInt(fp);
		definition.params.minFilter = readUInt(fp); definition.params.magFilter = readUInt(fp); definition.params.wrap = readUInt(fp);
		definition.params.useMipmaps = readBool(fp); definition.params.lodBaseLevel = readInt(fp); definition.params.lodMaxLevel = readInt(fp);
		definition.params.lodBias = readFloat(fp); definition.params.maxAnisotropy = readFloat(fp);
		// RSE4 onwards. Older streams simply lack the field and keep TextureParams' default.
		if (mReadVersion >= 4) definition.params.colourSpace = static_cast<TextureColourSpace>(readUInt(fp));
		definition.sampler = readString(fp); definition.source = readString(fp);
	}

	ResourceStreamPtr ResourceStreamSerializer::convertLegacyMaterial(ResourceStreamPtr const& stream)
	{
		auto basic = static_cast<ProgrammaticBasicMaterialStream*>(stream.get());
		auto const& source = basic->mSpecification;
		if (!source.uniforms.getUniformData().contains("PBR_ENABLED"))
		{
			if (mResourceMgr) mResourceMgr->warnMessage("Loading deprecated Material stream as BasicMaterial; re-export this asset.");
			return stream;
		}
		auto converted = make_shared<ProgrammaticPbrMaterialStream>(mResourceMgr);
		for (auto const& [name, child] : basic->getChildren()) converted->addChild(name, child);
		auto& target = converted->mSpecification;
		target.legacyFullContract = true;
		target.program.resourceExists = source.program.resourceExists; target.program.existingResource = source.program.existingResource;
		target.program.isChild = source.program.isChild; target.program.is2d = source.program.is2d; target.program.spec = source.program.spec;
		target.program.vertexShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(source.program.vertexShader.type); target.program.vertexShader.data = source.program.vertexShader.data;
		target.program.geometryShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(source.program.geometryShader.type); target.program.geometryShader.data = source.program.geometryShader.data;
		target.program.fragmentShader.type = static_cast<PbrMaterialSpecification::ProgramOptions::Shader::Type>(source.program.fragmentShader.type); target.program.fragmentShader.data = source.program.fragmentShader.data;
		target.uniforms = source.uniforms;
		for (auto it = target.uniforms.mUniformData.begin(); it != target.uniforms.mUniformData.end(); )
			if (it->first.rfind("PBR_", 0) != 0) it = target.uniforms.mUniformData.erase(it); else ++it;
		target.pbr.enabled = true;
		auto const& values = source.uniforms.getUniformData();
		auto copy = [&](char const* field, void* destination, size_t size) { auto found = values.find(field); if (found != values.end() && found->second.size >= size) memcpy(destination, found->second.data, size); };
		copy("PBR_BASE_COLOUR_FACTOR", &target.pbr.baseColourFactor, sizeof(target.pbr.baseColourFactor));
		copy("PBR_METALLIC_FACTOR", &target.pbr.metallicFactor, sizeof(float)); copy("PBR_ROUGHNESS_FACTOR", &target.pbr.roughnessFactor, sizeof(float));
		copy("PBR_EMISSIVE_FACTOR", &target.pbr.emissiveFactor, sizeof(target.pbr.emissiveFactor)); copy("PBR_NORMAL_SCALE", &target.pbr.normalScale, sizeof(float));
		copy("PBR_OCCLUSION_STRENGTH", &target.pbr.occlusionStrength, sizeof(float)); copy("PBR_ALPHA_CUTOFF", &target.pbr.alphaCutoff, sizeof(float));
		int32_t alphaMode = 0, doubleSided = 0; copy("PBR_ALPHA_MODE", &alphaMode, sizeof(alphaMode)); copy("PBR_DOUBLE_SIDED", &doubleSided, sizeof(doubleSided));
		target.pbr.alphaMode = static_cast<PbrMaterialSpecification::PbrAlphaMode>(alphaMode); target.pbr.doubleSided = doubleSided != 0;
		for (auto const& texture : source.textures)
		{
			PbrMaterialSpecification::TextureOptions result;
			result.resourceExists = texture.resourceExists; result.sampler = texture.sampler; result.existingResource = texture.existingResource;
			result.isChild = texture.isChild; result.source = texture.source; result.target = texture.target; result.params = texture.params;
			target.textures.push_back(result);
		}
		if (mResourceMgr) mResourceMgr->warnMessage("Converted deprecated PBR-tagged Material stream to PbrMaterial; re-export this asset.");
		return converted;
	}

	ResourceStreamPtr ResourceStreamSerializer::readStream(ifstream& fp)
	{
		auto streamType = readString(fp);
		ResourceStreamPtr resourceStream;

		if (streamType == "BasicMaterial" || streamType == "Material")
		{
			resourceStream.reset(new ProgrammaticBasicMaterialStream(mResourceMgr));
		}
		else if (streamType == "PbrMaterial")
		{
			resourceStream.reset(new ProgrammaticPbrMaterialStream(mResourceMgr));
		}
		else if (streamType == "PostEffectMaterial")
		{
			resourceStream.reset(new ProgrammaticPostEffectMaterialStream(mResourceMgr));
		}
		else if (streamType == "Program")
		{
			resourceStream.reset(new ProgrammaticProgramStream(mResourceMgr));
		}
		else if (streamType == "Sampler")
		{
			resourceStream.reset(new ProgrammaticSamplerStream(mResourceMgr));
		}
		else if (streamType == "String")
		{
			resourceStream.reset(new ProgrammaticStringStream(mResourceMgr));
		}
		else if (streamType == "Texture")
		{
			resourceStream.reset(new ProgrammaticTextureStream(mResourceMgr));
		}
		else
		{
			string errMsg = "Cannot deserialize ResourceStream of type '" + streamType + "'.";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		// Read children
		uint32_t numChildren = readUInt(fp);
		for (uint32_t i = 0; i < numChildren; ++i)
		{
			auto name = readString(fp);
			auto res = readStream(fp);

			resourceStream->addChild(name, res);
		}

		if (mReadVersion < 3)
		{
			uint32_t definitionCount = readUInt(fp);
			if (definitionCount != 1) THROW_MPP("Legacy resource stream contains multiple embedded variants; split and re-export it.", __LINE__, __FILE__, __func__);
			for (uint32_t i = 0; i < definitionCount; ++i) { readString(fp); readUInt(fp); }
		}

		// Read type-specific data
		if (streamType == "BasicMaterial" || streamType == "Material")
		{
			readBasicMaterialStream(resourceStream, fp);
		}
		else if (streamType == "PbrMaterial")
		{
			readPbrMaterialStream(resourceStream, fp);
		}
		else if (streamType == "PostEffectMaterial")
		{
			readPostEffectMaterialStream(resourceStream, fp);
		}
		else if (streamType == "Program")
		{
			readProgramStream(resourceStream, fp);
		}
		else if (streamType == "Sampler")
		{
			readSamplerStream(resourceStream, fp);
		}
		else if (streamType == "String")
		{
			readStringStream(resourceStream, fp);
		}
		else if (streamType == "Texture")
		{
			readTextureStream(resourceStream, fp);
		}
		else
		{
			string errMsg = "Cannot deserialize ResourceStream of type '" + streamType + "'.";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		return streamType == "Material" ? convertLegacyMaterial(resourceStream) : resourceStream;
	}
}