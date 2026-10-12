#pragma once

#include <vector>

#include "mpp/Config.h"

#include "mpp/RenderSystem.h"
#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/VertexTypeSpecification.h"

namespace mpp
{
	
	template<typename PosType, typename TexType, typename ColType, int ColSize>
	class QuadBatchSpecification
	{
		mesh::VertexDataType<PosType> mPositionType;
		mesh::VertexDataType<TexType> mTexcoordType;
		mesh::VertexDataType<ColType> mColourType;
		mesh::VertexComponentSize<ColSize> mColourSize;

		uint32_t mIndexWidth;

		bool mUseTriangles;

		bool mUseDiffuse;

	public:

		QuadBatchSpecification(
			RenderSystem* renderSystem,
			bool useDiffuse,
			uint32_t indexWidth,
			bool square,
			float maxDimX,
			float maxDimY,
			bool useTriangles)
			: mUseDiffuse(useDiffuse)
			, mIndexWidth(indexWidth)
			, mUseTriangles(useTriangles)
		{
			// Check caps to see if points are supported
			if (!useTriangles)
			{
				Caps const& caps = renderSystem->getCaps();
				bool canUsePointSprites = square &&
					caps.pointSizeRange[0] < maxDimX && caps.pointSizeRange[1] > maxDimY;

				if (!canUsePointSprites)
				{
					mUseTriangles = true;
				}
			}
		}

		mesh::Vertex::DataType getPositionType() const
		{
			return mPositionType.value;
		}

		int getPrimitiveMainSize() const
		{
			int colTypeSize = mesh::Vertex::getDataTypeSize(mColourType.value);
			int colCompSize = mColourSize.value;
			int colTypeCount = mUseTriangles ? 4 : 1;

			return getColourOffset() + colTypeSize * colCompSize * colTypeCount;
		}
		
		mesh::Vertex::DataType getTexcoordType() const
		{
			return mTexcoordType.value;
		}

		int getPrimitiveTexcoordSize() const
		{
			int texTypeSize = mesh::Vertex::getDataTypeSize(mTexcoordType.value);
			int texTypeCount = mUseTriangles ? 8 : 4;

			return texTypeSize * texTypeCount;
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
			int posTypeCount = mUseTriangles ? 8 : 4;

			return posTypeSize * posTypeCount;
		}

		uint32_t getIndexWidth() const
		{
			return mIndexWidth;
		}

		void setTriangles(bool useTriangles)
		{
			mUseTriangles = useTriangles;
		}

		bool useDiffuse() const
		{
			return mUseDiffuse;
		}

		bool useTriangles() const
		{
			return mUseTriangles;
		}
	};
}