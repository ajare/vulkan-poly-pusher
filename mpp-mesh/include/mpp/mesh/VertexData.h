#pragma once

#include <vector>

#include "Config.h"
#include "Vertex.h"
#include "MeshSpecification.h"

namespace mpp
{
	namespace mesh
	{

		class _MPPMESHAPI VertexData
		{
		public:

			class _MPPMESHAPI Reader
			{
				friend class VertexData;

				VertexData const* mVertexData;

				uint32_t mIndex, mOffset;

			private:

				explicit Reader(VertexData const* vertexData)
					: mVertexData(vertexData)
					, mIndex(0)
					, mOffset(0)
				{
				}

			public:

				void rewind()
				{
					mIndex = mOffset = 0;
				}

				void nextVertex()
				{
					uint32_t currentVertex = mIndex / (uint32_t)mVertexData->getNumComponents();
					mIndex = (uint32_t)mVertexData->getNumComponents() * (currentVertex + 1);
					mOffset = (uint32_t)mVertexData->getStride() * (currentVertex + 1);
				}
			};

		private:

			uint32_t mOffset{ 0 };

			MeshSpecification mSpec;

			std::vector<int8_t> mData;

		private:

			void interpolateVertices(int8_t const* v0, int8_t const* v1, double t, std::vector<int8_t>& output);

			bool _inside(double px, double py, double p1x, double p1y, double p2x, double p2y) const;

			void _intersection(double cp1x, double cp1y, double cp2x, double cp2y, double sx, double sy, double ex, double ey, double& t) const;

			void _clipTriangleInputAgainstLine(double cp1x, double cp1y, double cp2x, double cp2y, std::vector<int8_t>& input, std::vector<int8_t>& output);

			void clipTrianglesAgainstBoundingBox(float x0, float y0, float x1, float y1);

			void clipLinesAgainstBoundingBox(float x0, float y0, float x1, float y1);

			void clipPointsAgainstBoundingBox(float x0, float y0, float x1, float y1);

		public:

			VertexData(MeshSpecification const& spec, size_t numVertices);

			void clear();

			MeshSpecification const& getMeshSpecification() const;

			size_t getStride() const;

			size_t getNumComponents() const;

			size_t getNumVertices() const;

			std::vector<int8_t> const& getData() const;

			std::vector<int8_t> getVertexRange(uint32_t start, size_t count) const;

			Reader createReader() const;

			VertexData& i8(int8_t data);

			VertexData& i8(int8_t data1, int8_t data2);

			VertexData& i8(int8_t data1, int8_t data2, int8_t data3);

			VertexData& i8(int8_t data1, int8_t data2, int8_t data3, int8_t data4);

			VertexData& u8(uint8_t data);

			VertexData& u8(uint8_t data1, uint8_t data2);

			VertexData& u8(uint8_t data1, uint8_t data2, uint8_t data3);

			VertexData& u8(uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4);

			VertexData& i16(int16_t data);

			VertexData& i16(int16_t data1, int16_t data2);

			VertexData& i16(int16_t data1, int16_t data2, int16_t data3);

			VertexData& i16(int16_t data1, int16_t data2, int16_t data3, int16_t data4);

			VertexData& u16(uint16_t data);

			VertexData& u16(uint16_t data1, uint16_t data2);

			VertexData& u16(uint16_t data1, uint16_t data2, uint16_t data3);

			VertexData& u16(uint16_t data1, uint16_t data2, uint16_t data3, uint16_t data4);

			VertexData& i32(int32_t data);

			VertexData& i32(int32_t data1, int32_t data2);

			VertexData& i32(int32_t data1, int32_t data2, int32_t data3);

			VertexData& i32(int32_t data1, int32_t data2, int32_t data3, int32_t data4);

			VertexData& u32(uint32_t data);

			VertexData& u32(uint32_t data1, uint32_t data2);

			VertexData& u32(uint32_t data1, uint32_t data2, uint32_t data3);

			VertexData& u32(uint32_t data1, uint32_t data2, uint32_t data3, uint32_t data4);

			VertexData& f16(float data);

			VertexData& f16(float data1, float data2);

			VertexData& f16(float data1, float data2, float data3);

			VertexData& f16(float data1, float data2, float data3, float data4);

			VertexData& f32(float data);

			VertexData& f32(float data1, float data2);

			VertexData& f32(float data1, float data2, float data3);

			VertexData& f32(float data1, float data2, float data3, float data4);

			VertexData& f64(double data);

			VertexData& f64(double data1, double data2);

			VertexData& f64(double data1, double data2, double data3);

			VertexData& f64(double data1, double data2, double data3, double data4);

			void clipAgainstBoundingBox(float x0, float y0, float x1, float y1);
		};
	}
}