#include "mpp/ModelSerializer.h"
#include "mpp/ResourceStreamSerializer.h"
#include "mpp/MppException.h"

#define FLAG_INDEXED_VERTICES 0x0001

using namespace std;

namespace mpp
{
	using namespace mpp::mesh;

	/*
	 * Constructor.
	 *
	 */
	ModelSerializer::ModelSerializer(ResourceManager* resourceMgr)
		: mResourceMgr(resourceMgr)
	{
	}

	void ModelSerializer::clear()
	{
		mMaterialNames.clear();
		mMaterials.clear();
		mVertexStreams.clear();
		mIndexStreamLookup.clear();
		mIndexStreams.clear();
		mMeshes.clear();
	}

	void ModelSerializer::writeValue(string const& value, ofstream& fp)
	{
		auto len = (uint32_t)value.length();
		fp.write((char const*)&len, sizeof(len));

		if (len > 0)
		{
			fp.write(value.c_str(), len);
		}
	}

	void ModelSerializer::writeValue(char const* value, size_t count, ofstream& fp)
	{
		if (count > 0)
		{
			fp.write(value, count);
		}
	}

	void ModelSerializer::writeValue(int32_t value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ModelSerializer::writeValue(uint32_t value, ofstream& fp)
	{
		auto pos = fp.tellp();
		fp.write((char const*)&value, sizeof(value));
	}

	void ModelSerializer::writeValue(uint16_t value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ModelSerializer::writeValue(int16_t value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ModelSerializer::writeValue(float value, ofstream& fp)
	{
		fp.write((char const*)&value, sizeof(value));
	}

	void ModelSerializer::writeValue(bool value, ofstream& fp)
	{
		char vi = value ? 1 : 0;
		fp.write((char const*)&vi, sizeof(vi));
	}

	string ModelSerializer::readString(ifstream& fp)
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

	void ModelSerializer::readBytes(int8_t* buffer, size_t count, ifstream& fp)
	{
		if (count > 0)
		{
			fp.read((char*)buffer, count);
		}
	}

	size_t ModelSerializer::readSize(ifstream& fp)
	{
		uint32_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	int32_t ModelSerializer::readInt32(ifstream& fp)
	{
		int32_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	uint32_t ModelSerializer::readUInt32(ifstream& fp)
	{
		uint32_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	int16_t ModelSerializer::readInt16(ifstream& fp)
	{
		int16_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	uint16_t ModelSerializer::readUInt16(ifstream& fp)
	{
		uint16_t value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	float ModelSerializer::readFloat(ifstream& fp)
	{
		float value;

		fp.read((char*)&value, sizeof(value));
		return value;
	}

	bool ModelSerializer::readBool(ifstream& fp)
	{
		char value;

		fp.read((char*)&value, sizeof(value));
		return value != 0;
	}

	/*
	 * Read Header
	 *
	 */
	void ModelSerializer::readHeader(ifstream& fp)
	{
		/*
		4 bytes: id: value 'MMPM'
		2 bytes: version major
		2 bytes: version minor
		4 bytes: flags (including if indexed, etc).
		*/

		// Id
		char magic[4];
		readBytes((int8_t*)magic, 4, fp);

		if (magic[0] != 'M' || magic[1] != 'P' || magic[2] != 'P' || magic[3] != 'M')
		{
			THROW_MPP_IO("File is not a valid model file.", __LINE__, __FILE__, __func__);
		}

		// Version major
		mHeader.versionMajor = readUInt16(fp);
		mHeader.versionMinor = readUInt16(fp);

		// Flags
		mHeader.flags = readUInt32(fp);
	}

	/*
	 * Write Header
	 *
	 */
	void ModelSerializer::writeHeader(ofstream& fp)
	{
		/*
		4 bytes: id: value 'MMPM'
		2 bytes: version major
		2 bytes: version minor
		4 bytes: flags (including if indexed, etc).
		*/

		// Id
		char magic[4] = { 'M', 'P', 'P', 'M' };
		writeValue(magic, 4, fp);

		// Version major
		writeValue((uint16_t)1, fp);

		// Version minor
		writeValue((uint16_t)1, fp);

		// Flags
		uint32_t flags = 0;
		flags |= FLAG_INDEXED_VERTICES;

		writeValue(flags, fp);
	}

	/*
	 * Read directory.
	 *
	 */
	void ModelSerializer::readDirectory(ifstream& fp)
	{
		mDirectory.entries[(uint32_t)Directory::Entry::Type::Unused] = readDirectoryEntry(fp);
		mDirectory.entries[(uint32_t)Directory::Entry::Type::MaterialNames] = readDirectoryEntry(fp);
		mDirectory.entries[(uint32_t)Directory::Entry::Type::Materials] = readDirectoryEntry(fp);
		mDirectory.entries[(uint32_t)Directory::Entry::Type::VertexData] = readDirectoryEntry(fp);
		mDirectory.entries[(uint32_t)Directory::Entry::Type::IndexData] = readDirectoryEntry(fp);
		mDirectory.entries[(uint32_t)Directory::Entry::Type::MeshMetadata] = readDirectoryEntry(fp);
	}

	/*
	 * Read directory entry.
	 *
	 */
	ModelSerializer::Directory::Entry ModelSerializer::readDirectoryEntry(ifstream& fp)
	{
		ModelSerializer::Directory::Entry entry;

		entry.type = static_cast<Directory::Entry::Type>(readUInt32(fp));
		entry.startOffset = readUInt32(fp);
		entry.endOffset = readUInt32(fp);
		entry.count = readSize(fp);

		return entry;
	}

	void ModelSerializer::updateDirectoryEntry(ofstream& fp, Directory::Entry::Type type, size_t start, size_t end, size_t count)
	{
		auto oldPos = fp.tellp();

		constexpr size_t kOnDiskEntrySize = 4 * sizeof(uint32_t);
		size_t newPos = sizeof(Header) + (size_t)type * kOnDiskEntrySize;

		fp.seekp((streampos)newPos);
		writeDirectoryEntry(fp, { type, start, end, count });

		fp.seekp(oldPos);
	}

	/*
	 * Write directory.
	 *
	 */
	void ModelSerializer::writeDirectory(ofstream& fp)
	{
		writeDirectoryEntry(fp, { Directory::Entry::Type::Unused, 0, 0, 0 });
		writeDirectoryEntry(fp, { Directory::Entry::Type::MaterialNames, 0, 0, mMaterialNames.size() });
		writeDirectoryEntry(fp, { Directory::Entry::Type::Materials, 0, 0, mMaterials.size() });
		writeDirectoryEntry(fp, { Directory::Entry::Type::VertexData, 0, 0, 0 });
		writeDirectoryEntry(fp, { Directory::Entry::Type::IndexData, 0, 0, 0 });
		writeDirectoryEntry(fp, { Directory::Entry::Type::MeshMetadata, 0, 0, 0 });
	}

	/*
	 * Write directory entry.
	 *
	 */
	void ModelSerializer::writeDirectoryEntry(ofstream& fp, Directory::Entry const& entry)
	{
		writeValue((uint32_t)entry.type, fp);
		writeValue((uint32_t)entry.startOffset, fp);
		writeValue((uint32_t)entry.endOffset, fp);
		writeValue((uint32_t)entry.count, fp);
	}

	void ModelSerializer::readMaterialNames(ifstream& fp)
	{
		auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::MaterialNames];

		fp.seekg(entry.startOffset);

		for (size_t i = 0; i < entry.count; ++i)
		{
			mMaterialNames.push_back(readString(fp));
		}

		if (fp.tellg() != entry.endOffset)
		{
			THROW_MPP("Invalid file position.", __LINE__, __FILE__, __func__);
		}
	}

	void ModelSerializer::readMaterials(ifstream& fp)
	{
		auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::Materials];

		fp.seekg(entry.startOffset);

		for (size_t i = 0; i < entry.count; ++i)
		{
			mMaterials.push_back(readMaterial(fp));
		}

		if (fp.tellg() != entry.endOffset)
		{
			THROW_MPP("Invalid file position.", __LINE__, __FILE__, __func__);
		}
	}

	ResourceStreamPtr ModelSerializer::readMaterial(ifstream& fp)
	{
		ResourceStreamSerializer ser(mResourceMgr);
		auto rs = ser.deserialize(fp);


		// Set any children as having been created, so they're not
		// created again (overwritten) when we try to create/load the resource
		rs->_markChildrenCreated();

		return rs;
	}

	void ModelSerializer::writeUniformCollection(UniformCollection const& uniforms, ofstream& fp)
	{
		writeValue((uint32_t)uniforms.getNumUniforms(), fp);

		auto const& uniformData = uniforms.getUniformData();

		for (auto const& kvp : uniformData)
		{
			auto const& data = kvp.second;

			writeValue(data.name, fp);
			writeValue((uint32_t)data.type, fp);
			writeValue((uint32_t)data.size, fp);
			writeValue(data.data, 64, fp);
		}
	}

	UniformCollection ModelSerializer::readUniformCollection(ifstream& fp)
	{
		UniformCollection uniforms;

		size_t numUniforms = readUInt32(fp);
		for (size_t i = 0; i < numUniforms; ++i)
		{
			string name = readString(fp);
			auto type = static_cast<program::GLSLType>(readUInt32(fp));
			auto size = readSize(fp);

			char data[64];
			fp.read(data, 64);

			uniforms.setUniform(name, type, size, 1, data);
		}

		return uniforms;
	}

	void ModelSerializer::writeMaterialNames(ofstream& fp)
	{
		auto start = (size_t)fp.tellp();

		for (auto const& name: mMaterialNames)
		{
			writeValue(name, fp);
		}

		auto end = (size_t)fp.tellp();
		updateDirectoryEntry(fp, Directory::Entry::Type::MaterialNames, start, end, mMaterialNames.size());
	}


	/*
	 * Write all materials.
	 *
	 */
	void ModelSerializer::writeMaterials(ofstream& fp)
	{
		auto start = (size_t)fp.tellp();

		// Write and update mapping
		for (auto const& material: mMaterials)
		{
			writeMaterial(material, fp);
		}

		auto end = (size_t)fp.tellp();
		updateDirectoryEntry(fp, Directory::Entry::Type::Materials, start, end, mMaterials.size());
	}

	void ModelSerializer::writeMaterial(ResourceStreamPtr material, ofstream& fp)
	{
		ResourceStreamSerializer ser(nullptr);
		ser.serialize(material, fp);
	}

	void ModelSerializer::readVertexBuffers(ifstream& fp)
	{
		auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::VertexData];

		fp.seekg(entry.startOffset);

		for (size_t i = 0; i < entry.count; ++i)
		{
			mVertexStreams.push_back(readVertexBuffer(fp));
		}

		if (fp.tellg() != entry.endOffset)
		{
			THROW_MPP("Invalid file position.", __LINE__, __FILE__, __func__);
		}
	}

	/*
	 * Read vertex buffer
	 *
	 */
	ModelSerializer::VertexStream ModelSerializer::readVertexBuffer(ifstream& fp)
	{
		/*
		4 bytes: vertex data size in bytes
		4 bytes: vertex count
		4 bytes: vertex stride
		x bytes: vertex data
		*/

		size_t vertexDataSize, vertexCount, vertexStride;

		vertexDataSize = readSize(fp);
		vertexCount = readSize(fp);
		vertexStride = readSize(fp);

		int8_t* vertexData = new int8_t[vertexDataSize];
		readBytes(vertexData, vertexDataSize, fp);

		VertexStream vs;
		vs.vertexCount = vertexCount;
		vs.vertexStride = vertexStride;
		vs.vertexData = shared_ptr<const int8_t>(vertexData, [](int8_t *p) { delete[] p; });

		return vs;
	}

	/*
	 * Write all Vertex Buffers.
	 *
	 */
	void ModelSerializer::writeVertexBuffers(ofstream& fp)
	{
		auto start = (size_t)fp.tellp();

		for (size_t i = 0; i < mVertexStreams.size(); ++i)
		{
			writeVertexBuffer(mVertexStreams[i], fp);
		}

		auto end = (size_t)fp.tellp();
		updateDirectoryEntry(fp, Directory::Entry::Type::VertexData, start, end, mVertexStreams.size());
	}

	/*
	 * Write vertex buffer
	 *
	 */
	void ModelSerializer::writeVertexBuffer(VertexStream const& vertexStream, ofstream& fp)
	{
		/*
		4 bytes: vertex data size in bytes
		4 bytes: vertex count
		4 bytes: vertex stride
		x bytes: vertex data
		*/

		size_t vertexDataSize = vertexStream.vertexCount * vertexStream.vertexStride;
		writeValue((uint32_t)vertexDataSize, fp);

		writeValue((uint32_t)vertexStream.vertexCount, fp);
		writeValue((uint32_t)vertexStream.vertexStride, fp);
		writeValue((char*)vertexStream.vertexData.get(), vertexDataSize, fp);
	}

	void ModelSerializer::readIndexBuffers(ifstream& fp)
	{
		auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::IndexData];

		fp.seekg(entry.startOffset);

		for (size_t i = 0; i < entry.count; ++i)
		{
			mIndexStreams.push_back(readIndexBuffer(fp));
		}

		if (fp.tellg() != entry.endOffset)
		{
			THROW_MPP("Invalid file position.", __LINE__, __FILE__, __func__);
		}
	}

	ModelSerializer::IndexStream ModelSerializer::readIndexBuffer(ifstream& fp)
	{
		/*
		4 bytes: index data size in bytes
		4 bytes: index width
		x bytes: index data
		*/
		auto dataSize = readSize(fp);
		auto indexWidth = readSize(fp);

		uint8_t* indexData = new uint8_t[dataSize];
		readBytes((int8_t*)indexData, dataSize, fp);

		IndexStream is;
		is.indexWidth = indexWidth;
		is.indexData = shared_ptr<const uint8_t>(indexData, [](uint8_t *p) { delete[] p; });

		return is;
	}

	/*
	 * Write all Index Buffers.
	 *
	 */
	void ModelSerializer::writeIndexBuffers(ofstream& fp)
	{
		auto start = (size_t)fp.tellp();

		for (size_t i = 0; i < mIndexStreams.size(); ++i)
		{
			auto const& mesh = mMeshes[mIndexStreamLookup[(uint32_t)i]];
			writeIndexBuffer(fp, mIndexStreams[i], mesh.primitiveType, mesh.primitiveCount);
		}

		auto end = (size_t)fp.tellp();
		updateDirectoryEntry(fp, Directory::Entry::Type::IndexData, start, end, mIndexStreams.size());
	}

	void ModelSerializer::writeIndexBuffer(ofstream& fp, IndexStream const& indexStream, Primitive::Type primitiveType, size_t numPrimitives)
	{
		/*
		4 bytes: index data size in bytes
		4 bytes: index width
		x bytes: index data
		*/
		size_t indexDataSize = numPrimitives *
			mesh::Primitive::size(primitiveType) *
			indexStream.indexWidth / 8;

		writeValue((uint32_t)indexDataSize, fp);
		writeValue((uint32_t)indexStream.indexWidth, fp);
		writeValue((char const*)indexStream.indexData.get(), indexDataSize, fp);
	}

	void ModelSerializer::readMeshes(ifstream& fp)
	{
		auto const& entry = mDirectory.entries[(int)Directory::Entry::Type::MeshMetadata];

		fp.seekg(entry.startOffset);

		for (size_t i = 0; i < entry.count; ++i)
		{
			mMeshes.push_back(readMesh(fp));
		}

		if (fp.tellg() != entry.endOffset)
		{
			THROW_MPP("Invalid file position.", __LINE__, __FILE__, __func__);
		}
	}

	/*
	 * Read mesh definition
	 *
	 */
	ModelSerializer::Mesh ModelSerializer::readMesh(ifstream& fp)
	{
		/*
		zero-str: name
		4 bytes: primitive type
		4 bytes: primitive count
		X bytes: material name
		4 bytes: index buffer id (or -1 for none)
		4 bytes: number of vertex buffers
		...    : vertex buffer ids (4 bytes each)
		*/
		Mesh mesh;

		mesh.name = readString(fp);

		mesh.primitiveType = static_cast<mesh::Primitive::Type>(readUInt32(fp));
		mesh.primitiveCount = readUInt32(fp);
		mesh.material = readString(fp);

		auto numVertexBuffers = readSize(fp);

		for (size_t i = 0; i < numVertexBuffers; ++i)
		{
			auto vertexBufferId = readUInt32(fp);
			mesh.vertexStreams.push_back(vertexBufferId);
		}

		mesh.indexStream = readUInt32(fp);

		return mesh;
	}

	void ModelSerializer::writeMeshes(ofstream& fp)
	{
		auto start = (size_t)fp.tellp();

		for (uint32_t i = 0; i < mMeshes.size(); ++i)
		{
			writeMesh(fp, mMeshes[i], i);
		}

		auto end = (size_t)fp.tellp();
		updateDirectoryEntry(fp, Directory::Entry::Type::MeshMetadata, start, end, mMeshes.size());
	}

	/*
	 * Write mesh definition
	 *
	 */
	void ModelSerializer::writeMesh(ofstream& fp, Mesh const& mesh, uint32_t index)
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
		writeValue(mesh.name, fp);
		
		// Write material, meshspec, index buffer and vertex buffer IDs
		writeValue((uint32_t)mesh.primitiveType, fp);
		writeValue((uint32_t)mesh.primitiveCount, fp);
		writeValue(mesh.material, fp);

		writeValue((uint32_t)mesh.vertexStreams.size(), fp);
		for (auto vb: mesh.vertexStreams)
		{
			writeValue(vb, fp);
		}

		writeValue(mesh.indexStream, fp);
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
		return (int)mMeshes[meshIndex].primitiveCount;
	}

	void ModelSerializer::addMaterial(string const& name, ResourceStreamPtr material)
	{
		mMaterialNames.push_back(name);
		mMaterials.push_back(material);
	}

	/*
	 * Set material.
	 *
	 */
	void ModelSerializer::setMaterial(size_t meshIndex, string const& material)
	{
		mMeshes[meshIndex].material = material;
	}

	/*
	 * Get material.
	 *
	 */
	string const& ModelSerializer::getMaterial(size_t meshIndex) const
	{
		return mMeshes[meshIndex].material;
	}

	vector<string> const& ModelSerializer::getMaterialNames() const
	{
		return mMaterialNames;
	}

	vector<ResourceStreamPtr> const& ModelSerializer::getMaterials() const
	{
		return mMaterials;
	}

	/*
	 * Add a vertex stream.
	 *
	 */
	void ModelSerializer::addVertexStream(size_t meshIndex, size_t vertexCount, size_t vertexStride, std::shared_ptr<const int8_t> vertexData)
	{
		mMeshes[meshIndex].vertexStreams.push_back((uint32_t)mVertexStreams.size());

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
		mMeshes[meshIndex].indexStream = (uint32_t)streamIndex;
		mIndexStreamLookup[(uint32_t)streamIndex] = (uint32_t)meshIndex;

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
		return (int)mIndexStreams[mMeshes[meshIndex].indexStream].indexWidth;
	}

	/*
	 * Save file.
	 *
	 */
	void ModelSerializer::save(string const& filename)
	{
		ofstream fp(filename, ios::out | ios::binary);

		if (!fp.is_open())
		{
			THROW_MPP_IO("Could not open " + filename + " for writing.", __LINE__, __FILE__, __func__);
		}
		
		writeHeader(fp);
		writeDirectory(fp);

		writeMaterialNames(fp);
		writeMaterials(fp);
		writeVertexBuffers(fp);
		writeIndexBuffers(fp);
		writeMeshes(fp);
			
		fp.close();
	}

	/*
	 * Load file.
	 *
	 */
	void ModelSerializer::load(std::string const& filename)
	{
		clear();

		ifstream fp(filename, ios::in | ios::binary);

		if (!fp.is_open())
		{
			THROW_MPP_IO("Could not open " + filename + " for reading.", __LINE__, __FILE__, __func__);
		}

		readHeader(fp);
		readDirectory(fp);

		readMaterialNames(fp);
		readMaterials(fp);
		readVertexBuffers(fp);
		readIndexBuffers(fp);
		readMeshes(fp);

		fp.close();
	}

	ModelSerializer::MetadataReader ModelSerializer::getReader(string const& filename)
	{
		// If we pass in a filename, then load it in.  Otherwise, assume the
		// data has already been loaded (either from a file, or specified from API).
		if (filename != "")
		{
			clear();

			ifstream fp(filename, ios::in | ios::binary);

			if (!fp.is_open())
			{
				THROW_MPP_IO("Could not open " + filename + " for reading.", __LINE__, __FILE__, __func__);
			}

			readHeader(fp);
			readDirectory(fp);

			readMaterialNames(fp);
			readMaterials(fp);
			readMeshes(fp);

			fp.close();
		}

		MetadataReader reader(mResourceMgr, mMaterialNames, mMaterials);

		for (size_t i = 0; i < mMeshes.size(); ++i)
		{
			auto const& mesh = mMeshes[i];
			reader.addMesh(mesh.name, mesh.material, mesh.primitiveType);
		}

		return reader;
	}
}


