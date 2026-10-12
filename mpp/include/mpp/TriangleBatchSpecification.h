#pragma once

#include <vector>

#include "mpp/Config.h"

#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/VertexTypeSpecification.h"

namespace mpp
{
	template<typename PosType, typename TexType, typename ColType, int ColSize>
	class TriangleBatchSpecification
	{
		mesh::VertexDataType<PosType> mPositionType;
		mesh::VertexDataType<TexType> mTexcoordType;
		mesh::VertexDataType<ColType> mColourType;
		mesh::VertexComponentSize<ColSize> mColourSize;

		bool mUseDiffuse;

	public:

		explicit TriangleBatchSpecification(bool useDiffuse)
			: mUseDiffuse(useDiffuse)
		{
		}

		mesh::Vertex::DataType getPositionType() const
		{
			return mPositionType.value;
		}

		int getVertexMainStride() const
		{
			int colTypeSize = mesh::Vertex::getDataTypeSize(mColourType.value);
			int colCompSize = mColourSize.value;

			return getColourOffset() + colTypeSize * colCompSize;
		}

		mesh::Vertex::DataType getTexcoordType() const
		{
			return mTexcoordType.value;
		}

		int getVertexTexcoordStride() const
		{
			int texTypeSize = mesh::Vertex::getDataTypeSize(mTexcoordType.value);
			return texTypeSize * 2;
		}

		mesh::Vertex::DataType getColourType() const
		{
			return mColourType.value;
		}

		int getColourSize() const
		{
			return mColourSize.value;
		}

		int getColourOffset() const
		{
			int posTypeSize = mesh::Vertex::getDataTypeSize(mPositionType.value);
			return posTypeSize * 2;
		}

		bool useDiffuse() const
		{
			return mUseDiffuse;
		}
	};
}
