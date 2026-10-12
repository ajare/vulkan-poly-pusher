#pragma once

#include "mpp/Config.h"

#include <string>
#include <memory>
#include <map>
#include <array>
#include <fstream>

#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"

#include "mpp/mesh/MeshDefinition.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{

	class _MPPAPI ModelSerializer
	{
	public:

		class MetadataReader
		{
			friend class ModelSerializer;

			struct Mesh
			{
				std::string name;
				std::string material;
				mpp::mesh::Primitive::Type primitiveType;
			};

		private:

			ResourceManager* mResourceMgr;
			std::vector<std::string> mMaterialNames;
			std::vector<ResourceStreamPtr> mMaterials;
			std::vector<Mesh> mMeshes;

		private:

			MetadataReader(
				ResourceManager* resourceMgr,
				std::vector<std::string> materialNames,
				std::vector<ResourceStreamPtr> const& materials)
				: mResourceMgr(resourceMgr)
				, mMaterialNames(materialNames)
				, mMaterials(materials)
			{
			}

			void addMesh(std::string const& name, std::string const& material, mpp::mesh::Primitive::Type type)
			{
				Mesh mesh
				{
					name,
					material,
					type
				};

				mMeshes.push_back(mesh);
			}

		public:

			size_t getNumMeshes() const
			{
				return mMeshes.size();
			}

			ResourceStreamPtr const& getMaterialByMeshId(uint32_t id, std::string* matName)
			{
				for (size_t i = 0; i < mMaterialNames.size(); ++i)
				{
					auto const& name = mMaterialNames[i];
					if (mMeshes[id].material == name)
					{
						if (matName)
						{
							*matName = name;
						}

						return mMaterials[i];
					}
				}

				THROW_MPP("Could not find Material", __LINE__, __FILE__, __func__);
			}
		};

	private:

		struct Header
		{
			int versionMajor, versionMinor;
			uint32_t flags;
		};

		struct Directory
		{
			struct Entry
			{
				enum class Type
				{
					Unused,
					MaterialNames,
					Materials,
					VertexData,
					IndexData,
					MeshMetadata,
					COUNT
				};

				Type type{ Type::Unused };
				size_t startOffset{ 0 };
				size_t endOffset{ 0 };
				size_t count{ 0 };
			};

			std::array<Entry, static_cast<size_t>(Entry::Type::COUNT)> entries;
		};

		struct VertexStream
		{
			size_t vertexCount{ 0 }, vertexStride{ 0 };
			std::shared_ptr<const int8_t> vertexData;
		};

		struct IndexStream
		{
			size_t indexWidth{ 0 };
			std::shared_ptr<const uint8_t> indexData;
		};

		struct Mesh
		{
			std::string name;
			std::string material;
			mpp::mesh::Primitive::Type primitiveType;
			size_t primitiveCount;

			uint32_t indexStream;
			std::vector<uint32_t> vertexStreams;
		};

	private:

		ResourceManager* mResourceMgr;

		Header mHeader;

		Directory mDirectory;

		// Materials: a material gets added into an array, for writing, and
		// then the material name is mapped to the array index, for meshes
		// to index with.
		std::vector<std::string> mMaterialNames;

		std::vector<ResourceStreamPtr> mMaterials;

		std::vector<VertexStream> mVertexStreams;

		// Index buffers: we have a lookup which maps index buffer id
		// to mesh id, so we can get the primitive type etc, when writing
		std::map<uint32_t, uint32_t> mIndexStreamLookup;

		std::vector<IndexStream> mIndexStreams;

		std::vector<Mesh> mMeshes;

	private:

		void clear();

		// Write
		void writeValue(std::string const& value, std::ofstream& fp);

		void writeValue(char const* value, size_t count, std::ofstream& fp);

		void writeValue(int32_t value, std::ofstream& fp);

		void writeValue(uint32_t value, std::ofstream& fp);

		void writeValue(int16_t value, std::ofstream& fp);

		void writeValue(uint16_t value, std::ofstream& fp);

		void writeValue(float value, std::ofstream& fp);

		void writeValue(bool value, std::ofstream& fp);

		// Read
		std::string readString(std::ifstream& fp);

		void readBytes(int8_t* buffer, size_t count, std::ifstream& fp);

		size_t readSize(std::ifstream& fp);

		int32_t readInt32(std::ifstream& fp);

		uint32_t readUInt32(std::ifstream& fp);

		int16_t readInt16(std::ifstream& fp);

		uint16_t readUInt16(std::ifstream& fp);

		float readFloat(std::ifstream& fp);

		bool readBool(std::ifstream& fp);

		void readHeader(std::ifstream& fp);

		void writeHeader(std::ofstream& fp);

		void readDirectory(std::ifstream& fp);

		Directory::Entry readDirectoryEntry(std::ifstream& fp);

		void updateDirectoryEntry(std::ofstream& fp, Directory::Entry::Type type, size_t start, size_t end, size_t count);

		void writeDirectory(std::ofstream& fp);

		void writeDirectoryEntry(std::ofstream& fp, Directory::Entry const& entry);

		void readMaterialNames(std::ifstream& fp);

		void readMaterials(std::ifstream& fp);

		ResourceStreamPtr readMaterial(std::ifstream& fp);

		void writeMaterialNames(std::ofstream& fp);

		void writeMaterials(std::ofstream& fp);

		void writeMaterial(ResourceStreamPtr material, std::ofstream& fp);

		void writeUniformCollection(UniformCollection const& uniforms, std::ofstream& fp);

		UniformCollection readUniformCollection(std::ifstream& fp);

		void readVertexBuffers(std::ifstream& fp);

		VertexStream readVertexBuffer(std::ifstream& fp);
			
		void writeVertexBuffers(std::ofstream& fp);

		void writeVertexBuffer(VertexStream const& vertexStream, std::ofstream& fp);

		void readIndexBuffers(std::ifstream& fp);

		IndexStream readIndexBuffer(std::ifstream& fp);

		void writeIndexBuffers(std::ofstream& fp);

		void writeIndexBuffer(std::ofstream& fp, IndexStream const& indexStream, mpp::mesh::Primitive::Type primitiveType, size_t numPrimitives);

		void readMeshes(std::ifstream& fp);

		Mesh readMesh(std::ifstream& fp);
					
		void writeMeshes(std::ofstream& fp);

		void writeMesh(std::ofstream& fp, Mesh const& mesh, uint32_t index);

	public:

		explicit ModelSerializer(ResourceManager* resourceMgr = nullptr);

		void setName(size_t meshIndex, std::string const& name);

		std::string const& getName(size_t meshIndex) const;

		void setMeshCount(size_t count);

		size_t getMeshCount() const;

		void setPrimitiveType(size_t meshIndex, mpp::mesh::Primitive::Type primitiveType);

		mpp::mesh::Primitive::Type getPrimitiveType(size_t meshIndex) const;

		void setPrimitiveCount(size_t meshIndex, size_t primitiveCount);

		int getPrimitiveCount(size_t meshIndex) const;

		void addMaterial(std::string const& name, ResourceStreamPtr material);

		void setMaterial(size_t meshIndex, std::string const& material);

		std::string const& getMaterial(size_t meshIndex) const;

		std::vector<std::string> const& getMaterialNames() const;

		std::vector<ResourceStreamPtr> const& getMaterials() const;

		void addVertexStream(size_t meshIndex, size_t vertexCount, size_t vertexStride, std::shared_ptr<const int8_t> vertexData);

		void getVertexStream(size_t meshIndex, size_t index, size_t* vertexCount, size_t* vertexStride, std::shared_ptr<const int8_t>* vertexData);

		void setIndexBuffer(size_t meshIndex, std::shared_ptr<const uint8_t> indexData, size_t indexWidth);

		std::shared_ptr<const uint8_t> getIndexData(size_t meshIndex) const;

		int getIndexWidth(size_t meshIndex) const;

		void save(std::string const& filename);

		void load(std::string const& filename);

		MetadataReader getReader(std::string const& filename = "");
	};
}

