#include <cassert>

#include "mpp/mesh/VertexBufferDefinition.h"

using namespace std;

namespace mpp
{
	namespace mesh
	{

		/*
		 * VertexBufferDefinition constructor.
		 *
		 */
		VertexBufferDefinition::VertexBufferDefinition(VertexBufferAttributeLayout const& spec, int vertexCount, int vertexStride, bool streaming, shared_ptr<const int8_t> vertexData)
			: mVertexCount(vertexCount)
			, mVertexStride(vertexStride)
			, mStreaming(streaming)
			, mData(vertexData)
			, mSpecification(spec)
		{
		}

		/*
		 * Get vertex stride.
		 *
		 */
		int VertexBufferDefinition::getVertexStride() const
		{
			return mVertexStride;
		}

		/*
		 * Get vertex count.
		 *
		 */
		int VertexBufferDefinition::getVertexCount() const
		{
			return mVertexCount;
		}

		/*
		 * Get whether streaming or not.
		 *
		 */
		bool VertexBufferDefinition::getStreaming() const
		{
			return mStreaming;
		}

		/*
		 * Get data size.
		 *
		 */
		int VertexBufferDefinition::getDataSize() const
		{
			return getVertexStride() * getVertexCount();
		}

		/*
		 * Get vertex data.
		 *
		 */
		shared_ptr<const int8_t> VertexBufferDefinition::getData() const
		{
			return mData;
		}

		/*
		 * Get attribute layout.
		 *
		 */
		VertexBufferAttributeLayout const& VertexBufferDefinition::getAttributeLayout() const
		{
			return mSpecification;
		}

		/*
		 * Get the number of attributes in this buffer.
		 *
		 */
		size_t VertexBufferDefinition::getNumAttributes() const
		{
			return mSpecification.getNumAttributes();
		}

		/*
		 * Get the specified attribute.
		 *
		 */
		VertexBufferAttributeLayout::Attribute const& VertexBufferDefinition::getAttribute(int index) const
		{
			return mSpecification.getAttribute(index);
		}

	}
}