#include "mpp/mesh/ModelSerializer.h"
#include "mpp/mesh/MppMeshException.h"

#define FLAG_INDEXED_VERTICES 0x0001

using namespace std;

namespace mpp
{
	namespace mesh
	{

		/*
		 * Constructor.
		 *
		 */
		ModelSerializer::ModelSerializer()
		{
		}

		void ModelSerializer::clear()
		{
			mMaterialLookup.clear();
			mMaterials.clear();
			mMeshSpecifications.clear();
			mVertexStreams.clear();
			mIndexStreamLookup.clear();
			mIndexStreams.clear();
			mMeshes.clear();
		}

		string ModelSerializer::readString(FILE* fp)
		{
			string ret;
			auto ch = fgetc(fp);
			while (ch != '\0')
			{
				ret.append(1, ch);
				ch = fgetc(fp);
			}

			return ret;
		}

		void ModelSerializer::writeString(FILE* fp, string const& str)
		{
			auto len = str.length();
			fwrite(str.c_str(), 1, str.length() + 1, fp);
		}

		/*
		 * Read Header
		 *
		 */
		void ModelSerializer::readHeader(FILE* fp)
		{
			/*
			4 bytes: id: value 'MMPM'
			2 bytes: version major
			2 bytes: version minor
			4 bytes: flags (including if indexed, etc).
			*/

			// Id
			char magic[4];
			fread(magic, 4, 1, fp);

			if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'P' || magic[3] != 'M')
			{
				throw MppMeshException("File is not a valid model file.");
			}

			// Version major
			uint16_t versionMajor;
			fread(&versionMajor, sizeof(uint16_t), 1, fp);

			mHeader.versionMajor = versionMajor;

			// Version minor
			uint16_t versionMinor;
			fread(&versionMinor, sizeof(uint16_t), 1, fp);

			mHeader.versionMinor = versionMinor;

			// Flags
			fread(&mHeader.flags, sizeof(uint32_t), 1, fp);
		}

		/*
		 * Write Header
		  *
		*/
		void ModelSerializer::writeHeader(FILE* fp)
		{
			/*
			4 bytes: id: value 'MMPM'
			2 bytes: version major
			2 bytes: version minor
			4 bytes: flags (including if indexed, etc).
			*/

			// Id
			char magic[4] = { 'M', 'P', 'P', 'M' };
			fwrite(magic, 4, 1, fp);

			// Version major
			uint16_t versionMajor = 1;
			fwrite(&versionMajor, sizeof(uint16_t), 1, fp);

			// Version minor
			uint16_t versionMinor = 1;
			fwrite(&versionMinor, sizeof(uint16_t), 1, fp);

			// Flags
			uint32_t flags = 0;
			flags |= FLAG_INDEXED_VERTICES;

			fwrite(&flags, sizeof(uint32_t), 1, fp);
		}

		/*
		 * Read directory.
		 *
		 */
		void ModelSerializer::readDirectory(FILE* fp)
		{
			mDirectory.entries[(size_t)Directory::Entry::Type::Unused] = readDirectoryEntry(fp);
			mDirectory.entries[(size_t)Directory::Entry::Type::Materials] = readDirectoryEntry(fp);
			mDirectory.entries[(size_t)Directory::Entry::Type::MeshSpecifications] = readDirectoryEntry(fp);
			mDirectory.entries[(size_t)Directory::Entry::Type::VertexData] = readDirectoryEntry(fp);
			mDirectory.entries[(size_t)Directory::Entry::Type::IndexData] = readDirectoryEntry(fp);
			mDirectory.entries[(size_t)Directory::Entry::Type::MeshMetadata] = readDirectoryEntry(fp);
		}

		/*
		 * Read directory entry.
		 *
		 */
		ModelSerializer::Directory::Entry ModelSerializer::readDirectoryEntry(FILE* fp)
		{
			ModelSerializer::Directory::Entry entry;

			fread(&entry.type, sizeof(entry.type), 1, fp);
			fread(&entry.startOffset, sizeof(entry.startOffset), 1, fp);
			fread(&entry.endOffset, sizeof(entry.endOffset), 1, fp);
			fread(&entry.count, sizeof(entry.count), 1, fp);

			return entry;
		}

		void ModelSerializer::updateDirectoryEntry(FILE *fp, Directory::Entry::Type type, uint32_t start, uint32_t end, size_t count)
		{
			auto oldPos = ftell(fp);

			size_t newPos = sizeof(Header) + (size_t)type * sizeof(Directory::Entry);

			fseek(fp, newPos, SEEK_SET);
			writeDirectoryEntry(fp, { type, start, end, count });

			fseek(fp, oldPos, SEEK_SET);
		}

		/*
		 * Write directory.
		 *
		 */
		void ModelSerializer::writeDirectory(FILE* fp)
		{
			writeDirectoryEntry(fp, { Directory::Entry::Type::Unused, 0, 0, 0 });
			writeDirectoryEntry(fp, { Directory::Entry::Type::Materials, 0, 0, mMaterials.size() });
			writeDirectoryEntry(fp, { Directory::Entry::Type::MeshSpecifications, 0, 0, mMeshes.size() });
			writeDirectoryEntry(fp, { Directory::Entry::Type::VertexData, 0, 0, 0 });
			writeDirectoryEntry(fp, { Directory::Entry::Type::IndexData, 0, 0, 0 });
			writeDirectoryEntry(fp, { Directory::Entry::Type::MeshMetadata, 0, 0, 0 });
		}

		/*
		 * Write directory entry.
		 *
		 */
		void ModelSerializer::writeDirectoryEntry(FILE* fp, Directory::Entry const& entry)
		{
			fwrite(&entry.type, sizeof(entry.type), 1, fp);
			fwrite(&entry.startOffset, sizeof(entry.startOffset), 1, fp);
			fwrite(&entry.endOffset, sizeof(entry.endOffset), 1, fp);
			fwrite(&entry.count, sizeof(entry.count), 1, fp);
		}

		void ModelSerializer::readMaterials(FILE* fp)
		{
			auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::Materials];

			fseek(fp, entry.startOffset, SEEK_SET);

			for (size_t i = 0; i < entry.count; ++i)
			{
				auto material = readMaterial(fp);

				mMaterialLookup[material.getName()] = mMaterials.size();
				mMaterials.push_back(material);
			}

			if (ftell(fp) != entry.endOffset)
			{
				THROW_MPP_MESH("Invalid file position.", __LINE__, __FILE__, __func__);
			}
		}

		MaterialInformation ModelSerializer::readMaterial(FILE* fp)
		{
			auto name = readString(fp);

			MaterialInformation mi(name);

			mpp::mesh::MaterialInformation::PositionType positionType;
			fread(&positionType, sizeof(positionType), 1, fp);

			mi.setPositionType(positionType);

			// Shaders
			int shaderCount;
			fread(&shaderCount, sizeof(shaderCount), 1, fp);
			for (int i = 0; i < shaderCount; ++i)
			{
				mpp::mesh::MaterialInformation::Shader::Type shaderType;

				fread(&shaderType, sizeof(shaderType), 1, fp);
				auto shaderName = readString(fp);

				mi.addShader(shaderType, shaderName);
			}

			// Textures
			int textureCount;
			fread(&textureCount, sizeof(textureCount), 1, fp);
			for (int i = 0; i < textureCount; ++i)
			{
				int32_t isResource;
				fread(&isResource, sizeof(isResource), 1, fp);
				auto binding = readString(fp);
				auto resource = readString(fp);

				mi.addTexture(isResource, binding, resource);
			}

			// Uniforms
			int uniformCount;
			fread(&uniformCount, sizeof(uniformCount), 1, fp);
			for (int i = 0; i < uniformCount; ++i)
			{
				string name = readString(fp);
				string type = readString(fp);

				size_t numComponents;
				fread(&numComponents, sizeof(numComponents), 1, fp);

				if (type == "int")
				{
					int32_t values[4];
					fread(values, sizeof(int32_t), numComponents, fp);
					mi.addUniform(name, numComponents, values);
				}
				else if (type == "uint")
				{
					uint32_t values[4];
					fread(values, sizeof(uint32_t), numComponents, fp);
					mi.addUniform(name, numComponents, values);
				}
				if (type == "float")
				{
					float values[4];
					fread(values, sizeof(float), numComponents, fp);
					mi.addUniform(name, numComponents, values);
				}
			}

			return mi;
		}

		/*
		 * Write all materials.
		 *
		 */
		void ModelSerializer::writeMaterials(FILE* fp)
		{
			uint32_t start = ftell(fp);

			// Write and update mapping
			for (size_t i = 0; i < mMaterials.size(); ++i)
			{
				writeMaterial(fp, mMaterials[i]);
			}

			uint32_t end = ftell(fp);
			updateDirectoryEntry(fp, Directory::Entry::Type::Materials, start, end, mMaterials.size());
		}

		void ModelSerializer::writeMaterial(FILE* fp, MaterialInformation const& matInfo)
		{
			writeString(fp, matInfo.getName());

			auto positionType = matInfo.getPositionType();
			fwrite(&positionType, sizeof(positionType), 1, fp);

			// Shaders
			auto const& shaders = matInfo.getShaders();

			int shaderCount = (int)shaders.size();
			fwrite(&shaderCount, sizeof(shaderCount), 1, fp);
			for (auto const& shader: shaders)
			{
				fwrite(&shader.type, sizeof(shader.type), 1, fp);
				writeString(fp, shader.name);
			}

			// Textures
			auto const& textures = matInfo.getTextures();

			int textureCount = (int)textures.size();
			fwrite(&textureCount, sizeof(textureCount), 1, fp);
			for (auto const& texture: textures)
			{
				int32_t isResource = texture.isResource ? 1 : 0;
				fwrite(&isResource, sizeof(isResource), 1, fp);
				writeString(fp, texture.binding);
				writeString(fp, texture.resource);
			}

			// Uniforms
			auto const& uniforms = matInfo.getUniforms();

			int uniformCount = (int)uniforms.size();
			fwrite(&uniformCount, sizeof(uniformCount), 1, fp);
			for (auto const& uniform: uniforms)
			{
				writeString(fp, uniform.name);
				writeString(fp, uniform.type);
				fwrite(&uniform.numComponents, sizeof(uniform.numComponents), 1, fp);
				
				for (size_t i = 0; i < uniform.numComponents; ++i)
				{
					if (uniform.type == "int")
					{
						auto value = any_cast<int32_t>(uniform.values[i]);
						fwrite(&value, sizeof(int32_t), 1, fp);
					}
					else if (uniform.type == "uint")
					{
						auto value = any_cast<uint32_t>(uniform.values[i]);
						fwrite(&value, sizeof(uint32_t), 1, fp);
					}
					else if (uniform.type == "float")
					{
						auto value = any_cast<float>(uniform.values[i]);
						fwrite(&value, sizeof(float), 1, fp);
					}
				}
			}
		}

		void ModelSerializer::readMeshSpecifications(FILE* fp)
		{
			auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::MeshSpecifications];

			fseek(fp, entry.startOffset, SEEK_SET);

			for (size_t i = 0; i < entry.count; ++i)
			{
				mMeshSpecifications.push_back(readMeshSpecification(fp));
			}

			if (ftell(fp) != entry.endOffset)
			{
				THROW_MPP_MESH("Invalid file position.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Read model specification
		 *
		 */
		MeshSpecification ModelSerializer::readMeshSpecification(FILE* fp)
		{
			/*
			4 bytes: primitive type
			4 bytes: storage type
			1 byte : indexed vertices?
			2 bytes: layout count
			<buffer count> times: buffer data
			*/
			MeshSpecification meshSpec;

			mpp::mesh::Primitive::Type primitiveType;
			fread(&primitiveType, sizeof(mpp::mesh::Primitive::Type), 1, fp);
			meshSpec.setPrimitiveType(primitiveType);

			mpp::mesh::VertexBufferStorageType storageType;
			fread(&storageType, sizeof(mpp::mesh::VertexBufferStorageType), 1, fp);
			meshSpec.setStorageType(storageType);

			char indexed;
			fread(&indexed, sizeof(char), 1, fp);
			if (indexed == 1)
			{
				meshSpec.setIndexedVertices(true);
			}

			uint16_t layoutCount;
			fread(&layoutCount, sizeof(uint16_t), 1, fp);

			uint32_t attribOffset = 0;
			for (int i = 0; i < layoutCount; ++i)
			{
				auto layout = meshSpec.createVertexBufferAttributeLayout(false);
				readVertexBufferAttributeLayout(fp, layout, attribOffset);
				attribOffset += layout->getNumAttributes();
			}

			return meshSpec;
		}

		/*
		 * Write all MeshSpecifications.
		 *
		 */
		void ModelSerializer::writeMeshSpecifications(FILE* fp)
		{
			uint32_t start = ftell(fp);

			for (uint32_t i = 0; i < mMeshSpecifications.size(); ++i)
			{
				writeMeshSpecification(fp, mMeshSpecifications[i]);
			}

			uint32_t end = ftell(fp);
			updateDirectoryEntry(fp, Directory::Entry::Type::MeshSpecifications, start, end, mMeshSpecifications.size());
		}

		/*
		 * Write mesh specification
		 *
		 */
		void ModelSerializer::writeMeshSpecification(FILE* fp, MeshSpecification const& meshSpec)
		{
			/*
			4 bytes: primitive type
			4 bytes: storage type
			1 byte : indexed vertices?
			2 bytes: layout count
			<buffer count> times: buffer data
			*/
			mpp::mesh::Primitive::Type primitiveType = meshSpec.getPrimitiveType();
			fwrite(&primitiveType, sizeof(mpp::mesh::Primitive::Type), 1, fp);

			mpp::mesh::VertexBufferStorageType storageType = meshSpec.getStorageType();
			fwrite(&storageType, sizeof(mpp::mesh::VertexBufferStorageType), 1, fp);

			char indexed = meshSpec.verticesIndexed() ? 1 : 0;
			fwrite(&indexed, sizeof(char), 1, fp);

			uint16_t layoutCount = meshSpec.getNumVertexBufferAttributeLayouts();
			fwrite(&layoutCount, sizeof(uint16_t), 1, fp);

			for (int i = 0; i < meshSpec.getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto buffer = meshSpec.getVertexBufferAttributeLayout(i);
				writeVertexBufferAttributeLayout(fp, buffer);
			}
		}

		void ModelSerializer::readVertexBuffers(FILE* fp)
		{
			auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::VertexData];

			fseek(fp, entry.startOffset, SEEK_SET);

			for (size_t i = 0; i < entry.count; ++i)
			{
				mVertexStreams.push_back(readVertexBuffer(fp));
			}

			if (ftell(fp) != entry.endOffset)
			{
				THROW_MPP_MESH("Invalid file position.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Read vertex buffer
		 *
		 */
		ModelSerializer::VertexStream ModelSerializer::readVertexBuffer(FILE* fp)
		{
			/*
			4 bytes: vertex data size in bytes
			4 bytes: vertex count
			4 bytes: vertex stride
			x bytes: vertex data
			*/

			int vertexDataSize, vertexCount, vertexStride;

			fread(&vertexDataSize, sizeof(int), 1, fp);
			fread(&vertexCount, sizeof(int), 1, fp);
			fread(&vertexStride, sizeof(int), 1, fp);

			int8_t* vertexData = new int8_t[vertexDataSize];
			fread(vertexData, vertexDataSize, 1, fp);

			VertexStream vs;
			vs.vertexCount = vertexCount;
			vs.vertexStride = vertexStride;
			vs.vertexData = shared_ptr<const int8_t>(vertexData, [](int8_t *p) { delete[] p; });

			return vs;
		}

		/*
		 * Read Vertex Layout
		 *
		 */
		void ModelSerializer::readVertexBufferAttributeLayout(FILE* fp, mpp::mesh::VertexBufferAttributeLayout* layout, uint32_t attribOffset)
		{
			/*
			2 bytes: attribute count

			<attrib count> times:
			4 bytes : component
			4 bytes : data type
			4 bytes : padding bytes
			4 bytes : normalised
			*/

			uint16_t attribCount;
			fread(&attribCount, sizeof(uint16_t), 1, fp);

			for (int i = 0; i < attribCount; ++i)
			{
				mpp::mesh::Vertex::Component component;
				fread(&component, sizeof(mpp::mesh::Vertex::Component), 1, fp);

				mpp::mesh::Vertex::DataType dataType;
				fread(&dataType, sizeof(mpp::mesh::Vertex::DataType), 1, fp);

				int paddingBytes;
				fread(&paddingBytes, sizeof(int), 1, fp);

				char normalised;
				fread(&normalised, sizeof(char), 1, fp);

				layout->createAttribute(component, dataType, normalised == 1, paddingBytes);
			}
		}

		/*
		 * Write all Vertex Buffers.
		 *
		 */
		void ModelSerializer::writeVertexBuffers(FILE* fp)
		{
			uint32_t start = ftell(fp);

			for (size_t i = 0; i < mVertexStreams.size(); ++i)
			{
				writeVertexBuffer(fp, mVertexStreams[i]);
			}

			uint32_t end = ftell(fp);
			updateDirectoryEntry(fp, Directory::Entry::Type::VertexData, start, end, mVertexStreams.size());
		}

		/*
		 * Write vertex buffer
		 *
		 */
		void ModelSerializer::writeVertexBuffer(FILE* fp, VertexStream const& vertexStream)
		{
			/*
			4 bytes: vertex data size in bytes
			4 bytes: vertex count
			4 bytes: vertex stride
			x bytes: vertex data
			*/

			size_t vertexDataSize = vertexStream.vertexCount * vertexStream.vertexStride;
			fwrite(&vertexDataSize, sizeof(vertexDataSize), 1, fp);

			fwrite(&vertexStream.vertexCount, sizeof(vertexStream.vertexCount), 1, fp);
			fwrite(&vertexStream.vertexStride, sizeof(vertexStream.vertexStride), 1, fp);
			fwrite(vertexStream.vertexData.get(), vertexDataSize, 1, fp);
		}

		/*
		 * Write Vertex Layout
		 *
		 */
		void ModelSerializer::writeVertexBufferAttributeLayout(FILE* fp, mpp::mesh::VertexBufferAttributeLayout const& layout)
		{
			/*
			2 bytes: attribute count

			<attrib count> times:
			4 bytes : component
			4 bytes : data type
			4 bytes : padding bytes
			4 bytes : normalised
			*/

			uint16_t attribCount = layout.getNumAttributes();
			fwrite(&attribCount, sizeof(uint16_t), 1, fp);

			for (int i = 0; i < attribCount; ++i)
			{
				auto attrib = layout.getAttribute(i);

				fwrite(&attrib.component, sizeof(mpp::mesh::Vertex::Component), 1, fp);
				fwrite(&attrib.dataType, sizeof(mpp::mesh::Vertex::DataType), 1, fp);
				fwrite(&attrib.paddingBytes, sizeof(int), 1, fp);

				char normalised = attrib.normalised ? 1 : 0;
				fwrite(&normalised, sizeof(char), 1, fp);
			}
		}

		void ModelSerializer::readIndexBuffers(FILE* fp)
		{
			auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::IndexData];

			fseek(fp, entry.startOffset, SEEK_SET);

			for (size_t i = 0; i < entry.count; ++i)
			{
				mIndexStreams.push_back(readIndexBuffer(fp));
			}

			if (ftell(fp) != entry.endOffset)
			{
				THROW_MPP_MESH("Invalid file position.", __LINE__, __FILE__, __func__);
			}
		}

		ModelSerializer::IndexStream ModelSerializer::readIndexBuffer(FILE* fp)
		{
			/*
			4 bytes: index data size in bytes
			4 bytes: index width
			x bytes: index data
			*/
			size_t dataSize, indexWidth;

			fread(&dataSize, sizeof(dataSize), 1, fp);
			fread(&indexWidth, sizeof(indexWidth), 1, fp);

			uint8_t* indexData = new uint8_t[dataSize];
			fread(indexData, dataSize, 1, fp);

			IndexStream is;
			is.indexWidth = indexWidth;
			is.indexData = shared_ptr<const uint8_t>(indexData, [](uint8_t *p) { delete[] p; });

			return is;
		}

		/*
		 * Write all Index Buffers.
		 *
		 */
		void ModelSerializer::writeIndexBuffers(FILE* fp)
		{
			uint32_t start = ftell(fp);

			for (size_t i = 0; i < mIndexStreams.size(); ++i)
			{
				auto const& mesh = mMeshes[mIndexStreamLookup[i]];
				writeIndexBuffer(fp, mIndexStreams[i], mesh.primitiveType, mesh.primitiveCount);
			}

			uint32_t end = ftell(fp);
			updateDirectoryEntry(fp, Directory::Entry::Type::IndexData, start, end, mIndexStreams.size());
		}

		void ModelSerializer::writeIndexBuffer(FILE* fp, IndexStream const& indexStream, Primitive::Type primitiveType, size_t numPrimitives)
		{
			/*
			4 bytes: index data size in bytes
			4 bytes: index width
			x bytes: index data
			*/
			size_t indexDataSize = numPrimitives *
				mesh::Primitive::size(primitiveType) *
				indexStream.indexWidth / 8;

			fwrite(&indexDataSize, sizeof(indexDataSize), 1, fp);
			fwrite(&indexStream.indexWidth, sizeof(indexStream.indexWidth), 1, fp);
			fwrite(indexStream.indexData.get(), indexDataSize, 1, fp);
		}

		void ModelSerializer::readMeshes(FILE* fp)
		{
			auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::MeshMetadata];

			fseek(fp, entry.startOffset, SEEK_SET);

			for (size_t i = 0; i < entry.count; ++i)
			{
				mMeshes.push_back(readMesh(fp));
			}

			if (ftell(fp) != entry.endOffset)
			{
				THROW_MPP_MESH("Invalid file position.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Read mesh definition
		 *
		 */
		ModelSerializer::Mesh ModelSerializer::readMesh(FILE* fp)
		{
			/*
			zero-str: name
			4 bytes: primitive type
			4 bytes: primitive count
			4 bytes: material id
			4 bytes: index buffer id (or -1 for none)
			4 bytes: number of vertex buffers
			...    : vertex buffer ids (4 bytes each)
			*/
			Mesh mesh;

			mesh.name = readString(fp);

			fread(&mesh.primitiveType, sizeof(mesh.primitiveType), 1, fp);
			fread(&mesh.primitiveCount, sizeof(mesh.primitiveCount), 1, fp);

			fread(&mesh.material, sizeof(mesh.material), 1, fp);
			fread(&mesh.meshSpec, sizeof(mesh.meshSpec), 1, fp);

			size_t numVertexBuffers;
			fread(&numVertexBuffers, sizeof(numVertexBuffers), 1, fp);

			for (size_t i = 0; i < numVertexBuffers; ++i)
			{
				uint32_t vertexBufferId;
				fread(&vertexBufferId, sizeof(vertexBufferId), 1, fp);

				mesh.vertexStreams.push_back(vertexBufferId);
			}

			fread(&mesh.indexStream, sizeof(mesh.indexStream), 1, fp);

			return mesh;
		}

		void ModelSerializer::writeMeshes(FILE* fp)
		{
			uint32_t start = ftell(fp);

			for (uint32_t i = 0; i < mMeshes.size(); ++i)
			{
				writeMesh(fp, mMeshes[i], i);
			}

			uint32_t end = ftell(fp);
			updateDirectoryEntry(fp, Directory::Entry::Type::MeshMetadata, start, end, mMeshes.size());
		}

		/*
		 * Write mesh definition
		 *
		 */
		void ModelSerializer::writeMesh(FILE* fp, Mesh const& mesh, uint32_t index)
		{
			/*
			zero-str: name
			4 bytes: primitive type
			4 bytes: primitive count
			4 bytes: material id
			4 bytes: index buffer id (or -1 for none)
			4 bytes: number of vertex buffers
			...    : vertex buffer ids (4 bytes each)
			*/
			writeString(fp, mesh.name);
			fwrite(&mesh.primitiveType, sizeof(mesh.primitiveType), 1, fp);
			fwrite(&mesh.primitiveCount, sizeof(mesh.primitiveCount), 1, fp);

			// Write material, meshspec, index buffer and vertex buffer IDs
			fwrite(&mesh.material, sizeof(mesh.material), 1, fp);
			fwrite(&mesh.meshSpec, sizeof(index), 1, fp);

			auto numVertexBuffers = mesh.vertexStreams.size();
			fwrite(&numVertexBuffers, sizeof(numVertexBuffers), 1, fp);
			for (auto vb: mesh.vertexStreams)
			{
				fwrite(&vb, sizeof(vb), 1, fp);
			}

			fwrite(&mesh.indexStream, sizeof(uint32_t), 1, fp);
		}

		/*
		 * Set name.
		 *
		 */
		void ModelSerializer::setName(size_t meshIndex, string const& name)
		{
			mMeshes[meshIndex].name = name;
		}

		/*
		 * Get name.
		 *
		 */
		string const& ModelSerializer::getName(size_t meshIndex) const
		{
			return mMeshes[meshIndex].name;
		}

		/*
		 * Set the number of meshes in this file.
		 *
		 */
		void ModelSerializer::setMeshCount(size_t count)
		{
			mMeshes.resize(count);
		}

		/*
		 * Get the number of meshes in this file.
		 *
		 */
		size_t ModelSerializer::getMeshCount() const
		{
			return mMeshes.size();
		}

		/*
			 * Set primitive type.
			 *
			 */
		void ModelSerializer::setPrimitiveType(size_t meshIndex, Primitive::Type primitiveType)
		{
			mMeshes[meshIndex].primitiveType = primitiveType;
		}

		/*
		 * Get primitive type.
		 *
		 */
		Primitive::Type ModelSerializer::getPrimitiveType(size_t meshIndex) const
		{
			return mMeshes[meshIndex].primitiveType;
		}

		/*
		 * Set primitive count.
		 *
		 */
		void ModelSerializer::setPrimitiveCount(size_t meshIndex, size_t primitiveCount)
		{
			mMeshes[meshIndex].primitiveCount = primitiveCount;
		}

		/*
		 * Get primitive count.
		 *
		 */
		int ModelSerializer::getPrimitiveCount(size_t meshIndex) const
		{
			return mMeshes[meshIndex].primitiveCount;
		}

		/*
		 * Set mesh specification.
		 *
		 */
		void ModelSerializer::setMeshSpecification(size_t meshIndex, MeshSpecification const& specification)
		{
			mMeshes[meshIndex].meshSpec = mMeshSpecifications.size();
			mMeshSpecifications.push_back(specification);
		}

		/*
		 * Get mesh specification.
		 *
		 */
		MeshSpecification const& ModelSerializer::getMeshSpecification(size_t meshIndex) const
		{
			return mMeshSpecifications[mMeshes[meshIndex].meshSpec];
		}

		void ModelSerializer::addMaterial(string const& name, MaterialInformation const& matInfo)
		{
			mMaterialLookup[name] = mMaterials.size();
			mMaterials.push_back(matInfo);
		}

		/*
		 * Set material.
		 *
		 */
		void ModelSerializer::setMaterial(size_t meshIndex, string const& material)
		{
			mMeshes[meshIndex].material = mMaterialLookup[material];
		}

		/*
		 * Get material.
		 *
		 */
		string const& ModelSerializer::getMaterial(size_t meshIndex) const
		{
			return mMaterials[mMeshes[meshIndex].material].getName();
		}

		vector<MaterialInformation> const& ModelSerializer::getMaterials() const
		{
			return mMaterials;
		}

		/*
		 * Add a vertex stream.
		 *
		 */
		void ModelSerializer::addVertexStream(size_t meshIndex, size_t vertexCount, size_t vertexStride, std::shared_ptr<const int8_t> vertexData)
		{
			mMeshes[meshIndex].vertexStreams.push_back(mVertexStreams.size());

			VertexStream vs;
			vs.vertexCount = vertexCount;
			vs.vertexStride = vertexStride;
			vs.vertexData = vertexData;

			mVertexStreams.push_back(vs);
		}

		/*
		 * Get specified vertex stream.
		 *
		 */
		void ModelSerializer::getVertexStream(size_t meshIndex, size_t index, size_t* vertexCount, size_t* vertexStride, shared_ptr<const int8_t>* vertexData)
		{
			auto const& vertexStream = mVertexStreams[mMeshes[meshIndex].vertexStreams[index]];
			*vertexCount = vertexStream.vertexCount;
			*vertexStride = vertexStream.vertexStride;
			*vertexData = vertexStream.vertexData;
		}

		/*
		 * Set index data.
		 *
		 */
		void ModelSerializer::setIndexBuffer(size_t meshIndex, shared_ptr<const uint8_t> indexData, size_t indexWidth)
		{
			auto streamIndex = mIndexStreams.size();
			mMeshes[meshIndex].indexStream = streamIndex;
			mIndexStreamLookup[streamIndex] = meshIndex;

			IndexStream is;
			is.indexData = indexData;
			is.indexWidth = indexWidth;

			mIndexStreams.push_back(is);
		}

		/*
		 * Get index data.
		 *
		 */
		shared_ptr<const uint8_t> ModelSerializer::getIndexData(size_t meshIndex) const
		{
			return mIndexStreams[mMeshes[meshIndex].indexStream].indexData;
		}

		int ModelSerializer::getIndexWidth(size_t meshIndex) const
		{
			return mIndexStreams[mMeshes[meshIndex].indexStream].indexWidth;
		}

		/*
		 * Save file.
		 *
		 */
		void ModelSerializer::save(string const& filename)
		{
#pragma warning(suppress: 4996)
			FILE* fp = fopen(filename.c_str(), "wb");
		
			writeHeader(fp);
			writeDirectory(fp);

			writeMaterials(fp);
			writeMeshSpecifications(fp);
			writeVertexBuffers(fp);
			writeIndexBuffers(fp);
			writeMeshes(fp);
			
			fclose(fp);
		}

		/*
		 * Load file.
		 *
		 */
		void ModelSerializer::load(std::string const& filename)
		{
			clear();

#pragma warning(suppress: 4996)
			FILE* fp = fopen(filename.c_str(), "rb");

			readHeader(fp);
			readDirectory(fp);

			readMaterials(fp);
			readMeshSpecifications(fp);
			readVertexBuffers(fp);
			readIndexBuffers(fp);
			readMeshes(fp);

			fclose(fp);
		}

		ModelSerializer::MetadataReader ModelSerializer::getReader(string const& filename)
		{
			// If we pass in a filename, then load it in.  Otherwise, assume the
			// data has already been loaded (either from a file, or specified from API).
			if (filename != "")
			{
				clear();

#pragma warning(suppress: 4996)
				FILE* fp = fopen(filename.c_str(), "rb");

				readHeader(fp);
				readDirectory(fp);

				readMaterials(fp);
				readMeshSpecifications(fp);
				readMeshes(fp);

				fclose(fp);
			}

			MetadataReader reader(
				mMaterialLookup,
				mMaterials,
				mMeshSpecifications);

			for (size_t i = 0; i < mMeshes.size(); ++i)
			{
				auto const& mesh = mMeshes[i];
				reader.addMesh(mesh.name, mesh.meshSpec, mesh.material, mesh.primitiveType);
			}

			return reader;
		}
	}
}

