#if defined(_MSC_VER) && _MSC_VER < 1930
#  include <vld.h> // Memory tracking
#endif

#include <cassert>
#include <algorithm>

#include "mpp/mesh/MeshDefinition.h"

using namespace std;

namespace mpp
{
	namespace mesh
	{

		/*
		 * Constructor.
		 *
		 */
		MeshDefinition::MeshDefinition(string const& material, Primitive::Type type, VertexBufferStorageType storageType, int numPrimitives, int indexWidth, float pointSize)
			: mPrimitiveType(type)
			, mStorageType(storageType)
			, mIndexWidth(indexWidth)
			, mPointSize(pointSize)
			, mMaterial(material)
			, mNumPrimitives(numPrimitives)
			, mIsIndexed(false)
		{
		}

		/*
		 * Destructor.
		 *
		 */
		MeshDefinition::~MeshDefinition()
		{
			for (auto it: mVertexBufferDefinitions)
			{
				delete it;
			}
		}

		/*
		 * Set name.
		 *
		 */
		void MeshDefinition::setName(string const& name)
		{
			mName = name;
		}

		/*
		 * Get name.
		 *
		 */
		string const& MeshDefinition::getName() const
		{
			return mName;
		}

		/*
		 * Get primitive type.
		 *
		 */
		Primitive::Type MeshDefinition::getPrimitiveType() const
		{
			return mPrimitiveType;
		}

		/*
		 * Get storage type.
		 *
		 */
		VertexBufferStorageType MeshDefinition::getStorageType() const
		{
			return mStorageType;
		}

		/*
		 * Get index width, the size (in bytes) of an index.
		 *
		 */
		int MeshDefinition::getIndexWidth() const
		{
			return mIndexWidth;
		}

		/*
		 * Get point size: only used for points.
		 *
		 */
		float MeshDefinition::getPointSize() const
		{
			return mPointSize;
		}
		
		/*
		 * Get material name.
		 *
		 */
		string const& MeshDefinition::getMaterial() const
		{
			return mMaterial;
		}

		/*
		 * Set primitive count.
		 *
		 */
		void MeshDefinition::setNumPrimitives(int numPrimitives)
		{
			mNumPrimitives = numPrimitives;
		}

		/*
		 * Get primitive count.
		 *
		 */
		int MeshDefinition::getNumPrimitives() const
		{
			return mNumPrimitives;
		}

		/*
		 * Are the vertices indexed?
		 *
		 */
		bool MeshDefinition::isIndexed() const
		{
			return mIsIndexed;
		}

		/*
		 * Set as indexed.
		 *
		 */
		void MeshDefinition::setIndexed(bool indexed)
		{
			mIsIndexed = indexed;
		}

		/*
		 * Set index data.
		 *
		 */
		void MeshDefinition::setIndexData(shared_ptr<const uint8_t> indexData)
		{
			mIndexData = indexData;
		}

		/*
		 * Get index data
		 *
		 */
		std::shared_ptr<const uint8_t> MeshDefinition::getIndexData() const
		{
			return mIndexData;
		}

		/*
		 * Get the number of vertex buffers defined.
		 *
		 */
		size_t MeshDefinition::getNumVertexBufferDefinitions() const
		{
			return mVertexBufferDefinitions.size();
		}

		/*
		 * Add a vertex buffer.
		 *
		 */
		VertexBufferDefinition* MeshDefinition::createVertexBufferDefinition(VertexBufferAttributeLayout const& spec, int vertexCount, int vertexStride, std::shared_ptr<const int8_t> vertexData)
		{
			VertexBufferDefinition* vbd = new VertexBufferDefinition(spec, vertexCount, vertexStride, false, vertexData);
			mVertexBufferDefinitions.push_back(vbd);
			return vbd;
		}

		/*
		 * Get indexed buffer definition.
		 *
		 */
		VertexBufferDefinition* MeshDefinition::getVertexBufferDefinition(size_t index)
		{
			assert(index < getNumVertexBufferDefinitions() && "MeshDefinition::getVertexBufferDefinition() 'index' argument out of range!");
			return mVertexBufferDefinitions[index];
		}

	}
}