#pragma once

#include <string>
#include <fstream>

#include "mpp/mesh/MeshSpecification.h"

#include "mpp/program/Parser.h"

#include "Config.h"
#include "ResourceStream.h"
#include "UniformCollection.h"

namespace mpp
{
	class _MPPAPI ResourceStreamSerializer
	{
		// Write
		void writeValue(std::string const& value, std::ofstream& fp);

		void writeValue(int32_t value, std::ofstream& fp);

		void writeValue(uint32_t value, std::ofstream& fp);

		void writeValue(float value, std::ofstream& fp);

		void writeValue(bool value, std::ofstream& fp);

		void writeMeshSpecification(mesh::MeshSpecification const& meshSpec, std::ofstream& fp);

		void writeUniformCollection(UniformCollection const& uniforms, std::ofstream& fp);

		void writeParser(program::Parser const& parser, std::ofstream& fp);

		void writeBasicMaterialStream(ResourceStreamPtr resourceStream, std::ofstream& fp);
		void writePbrMaterialStream(ResourceStreamPtr resourceStream, std::ofstream& fp);
		void writePostEffectMaterialStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		void writeProgramStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		void writeSamplerStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		void writeStringStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		void writeTextureStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		void writeStream(ResourceStreamPtr resourceStream, std::ofstream& fp);

		// Read
		std::string readString(std::ifstream& fp);

		int32_t readInt(std::ifstream& fp);

		uint32_t readUInt(std::ifstream& fp);

		float readFloat(std::ifstream& fp);

		bool readBool(std::ifstream& fp);

		mesh::MeshSpecification readMeshSpecification(std::ifstream& fp);
		
		UniformCollection readUniformCollection(std::ifstream& fp);

		std::shared_ptr<program::Parser> readParser(std::ifstream& fp);

		void readBasicMaterialStream(ResourceStreamPtr resourceStream, std::ifstream& fp);
		void readPbrMaterialStream(ResourceStreamPtr resourceStream, std::ifstream& fp);
		void readPostEffectMaterialStream(ResourceStreamPtr resourceStream, std::ifstream& fp);

		void readProgramStream(ResourceStreamPtr resourceStream, std::ifstream& fp);

		void readSamplerStream(ResourceStreamPtr resourceStream, std::ifstream& fp);

		void readStringStream(ResourceStreamPtr resourceStream, std::ifstream& fp);

		void readTextureStream(ResourceStreamPtr resourceStream, std::ifstream& fp);

		ResourceStreamPtr readStream(std::ifstream& fp);
		ResourceStreamPtr convertLegacyMaterial(ResourceStreamPtr const& stream);

	private:

		ResourceManager* mResourceMgr;

		mesh::MeshSpecification mMeshSpec;

		bool mUseGlobalMeshSpec{ false };
		uint32_t mReadVersion{ 3 };

	public:

		explicit ResourceStreamSerializer(ResourceManager* resourceMgr);

		virtual ~ResourceStreamSerializer() = default;

		void setGlobalMeshSpecification(mesh::MeshSpecification const& meshSpec);

		void serialize(ResourceStreamPtr resourceStream, std::string const& filename);

		void serialize(ResourceStreamPtr resourceStream, std::ofstream& fp);

		ResourceStreamPtr deserialize(std::string const& filename);

		ResourceStreamPtr deserialize(std::ifstream& fp);

	};

}