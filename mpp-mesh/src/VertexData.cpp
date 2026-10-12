#include <cassert>

#include "half/half.hpp"

#include "mpp/mesh/VertexData.h"
#include "mpp/mesh/MppMeshException.h"

using namespace std;

namespace mpp
{
	namespace mesh
	{

		VertexData::VertexData(MeshSpecification const& spec, size_t numVertices)
			: mOffset(0)
			, mSpec(spec)
		{
			mData.resize(getStride() * numVertices);
		}

		void VertexData::clear()
		{
			mData.clear();
			mOffset = 0;
		}

		MeshSpecification const& VertexData::getMeshSpecification() const
		{
			return mSpec;
		}

		size_t VertexData::getStride() const
		{
			return mSpec.getVertexStrideInBytes();
		}

		size_t VertexData::getNumComponents() const
		{
			return mSpec.getNumComponents();
		}

		size_t VertexData::getNumVertices() const
		{
			return mData.size() / getStride();
		}

		vector<int8_t> const& VertexData::getData() const
		{
			return mData;
		}

		vector<int8_t> VertexData::getVertexRange(uint32_t start, size_t count) const
		{
			return vector<int8_t>(mData.begin() + getStride() * start, mData.begin() + getStride() * (start + count));
		}

		VertexData::Reader VertexData::createReader() const
		{
			return Reader(this);
		}

		VertexData& VertexData::i8(int8_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(int8_t));
			mOffset += sizeof(int8_t);
			return *this;
		}

		VertexData& VertexData::i8(int8_t data1, int8_t data2)
		{
			return i8(data1).i8(data2);
		}

		VertexData& VertexData::i8(int8_t data1, int8_t data2, int8_t data3)
		{
			return i8(data1).i8(data2).i8(data3);
		}

		VertexData& VertexData::i8(int8_t data1, int8_t data2, int8_t data3, int8_t data4)
		{
			return i8(data1).i8(data2).i8(data3).i8(data4);
		}

		VertexData& VertexData::u8(uint8_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(uint8_t));
			mOffset += sizeof(uint8_t);
			return *this;
		}

		VertexData& VertexData::u8(uint8_t data1, uint8_t data2)
		{
			return u8(data1).u8(data2);
		}

		VertexData& VertexData::u8(uint8_t data1, uint8_t data2, uint8_t data3)
		{
			return u8(data1).u8(data2).u8(data3);
		}

		VertexData& VertexData::u8(uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4)
		{
			return u8(data1).u8(data2).u8(data3).u8(data4);
		}

		VertexData& VertexData::i16(int16_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(int16_t));
			mOffset += sizeof(int16_t);
			return *this;
		}

		VertexData& VertexData::i16(int16_t data1, int16_t data2)
		{
			return i16(data1).i16(data2);
		}

		VertexData& VertexData::i16(int16_t data1, int16_t data2, int16_t data3)
		{
			return i16(data1).i16(data2).i16(data3);
		}

		VertexData& VertexData::i16(int16_t data1, int16_t data2, int16_t data3, int16_t data4)
		{
			return i16(data1).i16(data2).i16(data3).i16(data4);
		}

		VertexData& VertexData::u16(uint16_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(uint16_t));
			mOffset += sizeof(uint16_t);
			return *this;
		}

		VertexData& VertexData::u16(uint16_t data1, uint16_t data2)
		{
			return u16(data1).u16(data2);
		}

		VertexData& VertexData::u16(uint16_t data1, uint16_t data2, uint16_t data3)
		{
			return u16(data1).u16(data2).u16(data3);
		}

		VertexData& VertexData::u16(uint16_t data1, uint16_t data2, uint16_t data3, uint16_t data4)
		{
			return u16(data1).u16(data2).u16(data3).u16(data4);
		}

		VertexData& VertexData::i32(int32_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(int32_t));
			mOffset += sizeof(int32_t);
			return *this;
		}

		VertexData& VertexData::i32(int32_t data1, int32_t data2)
		{
			return i32(data1).i32(data2);
		}

		VertexData& VertexData::i32(int32_t data1, int32_t data2, int32_t data3)
		{
			return i32(data1).i32(data2).i32(data3);
		}

		VertexData& VertexData::i32(int32_t data1, int32_t data2, int32_t data3, int32_t data4)
		{
			return i32(data1).i32(data2).i32(data3).i32(data4);
		}

		VertexData& VertexData::u32(uint32_t data)
		{
			memcpy(&mData[mOffset], &data, sizeof(uint32_t));
			mOffset += sizeof(uint32_t);
			return *this;
		}

		VertexData& VertexData::u32(uint32_t data1, uint32_t data2)
		{
			return u32(data1).u32(data2);
		}

		VertexData& VertexData::u32(uint32_t data1, uint32_t data2, uint32_t data3)
		{
			return u32(data1).u32(data2).u32(data3);
		}

		VertexData& VertexData::u32(uint32_t data1, uint32_t data2, uint32_t data3, uint32_t data4)
		{
			return u32(data1).u32(data2).u32(data3).u32(data4);
		}

		VertexData& VertexData::f16(float data)
		{
			half_float::half hdata(data);

			memcpy(&mData[mOffset], &hdata, sizeof(half_float::half));
			mOffset += sizeof(half_float::half);
			return *this;
		}

		VertexData& VertexData::f16(float data1, float data2)
		{
			return f16(data1).f16(data2);
		}

		VertexData& VertexData::f16(float data1, float data2, float data3)
		{
			return f16(data1).f16(data2).f16(data3);
		}

		VertexData& VertexData::f16(float data1, float data2, float data3, float data4)
		{
			return f16(data1).f16(data2).f16(data3).f16(data4);
		}

		VertexData& VertexData::f32(float data)
		{
			memcpy(&mData[mOffset], &data, sizeof(float));
			mOffset += sizeof(float);
			return *this;
		}

		VertexData& VertexData::f32(float data1, float data2)
		{
			return f32(data1).f32(data2);
		}

		VertexData& VertexData::f32(float data1, float data2, float data3)
		{
			return f32(data1).f32(data2).f32(data3);
		}

		VertexData& VertexData::f32(float data1, float data2, float data3, float data4)
		{
			return f32(data1).f32(data2).f32(data3).f32(data4);
		}

		VertexData& VertexData::f64(double data)
		{
			memcpy(&mData[mOffset], &data, sizeof(double));
			mOffset += sizeof(double);
			return *this;
		}

		VertexData& VertexData::f64(double data1, double data2)
		{
			return f64(data1).f64(data2);
		}

		VertexData& VertexData::f64(double data1, double data2, double data3)
		{
			return f64(data1).f64(data2).f64(data3);
		}

		VertexData& VertexData::f64(double data1, double data2, double data3, double data4)
		{
			return f64(data1).f64(data2).f64(data3).f64(data4);
		}

		void VertexData::interpolateVertices(int8_t const* v0, int8_t const* v1, double t, vector<int8_t>& output)
		{
			for (size_t i = 0; i < mSpec.getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto const& layout = mSpec.getVertexBufferAttributeLayout((uint32_t)i);
				for (size_t j = 0; j < layout.getNumAttributes(); ++j)
				{
					auto const& attrib = layout.getAttribute(j);

					switch (attrib.dataType)
					{
					case Vertex::DataType::UnsignedByte:
					{
						uint8_t value0 = *(uint8_t const*)v0;
						uint8_t value1 = *(uint8_t const*)v1;
						uint8_t result = value0 + (uint8_t)((value1 - value0) * t);
						output.push_back((int8_t)result);
						break;
					}
					case Vertex::DataType::Byte:
					{
						int8_t value0 = *(int8_t const*)v0;
						int8_t value1 = *(int8_t const*)v1;
						int8_t result = value0 + (int8_t)((value1 - value0) * t);
						output.push_back(result);
						break;
					}
					case Vertex::DataType::UnsignedShort:
					{
						uint16_t value0 = *(uint16_t const*)v0;
						uint16_t value1 = *(uint16_t const*)v1;
						uint16_t result = value0 + (uint16_t)((value1 - value0) * t);
						output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(uint16_t)], &result, sizeof(uint16_t));
						break;
					}
					case Vertex::DataType::Short:
					{
						int16_t value0 = *(int16_t const*)v0;
						int16_t value1 = *(int16_t const*)v1;
						int16_t result = value0 + (int16_t)((value1 - value0) * t);
						output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(int16_t)], &result, sizeof(int16_t));
						break;
					}
					case Vertex::DataType::UnsignedInt:
					{
						uint32_t value0 = *(uint32_t const*)v0;
						uint32_t value1 = *(uint32_t const*)v1;
						uint32_t result = value0 + (uint32_t)((value1 - value0) * t);
						output.push_back(0); output.push_back(0); output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(uint32_t)], &result, sizeof(uint32_t));
						break;
					}
					case Vertex::DataType::Int:
					{
						int32_t value0 = *(int32_t const*)v0;
						int32_t value1 = *(int32_t const*)v1;
						int32_t result = value0 + (int32_t)((value1 - value0) * t);
						output.push_back(0); output.push_back(0); output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(int32_t)], &result, sizeof(int32_t));
						break;
					}
					case Vertex::DataType::HalfFloat:
					{
						half_float::half value0 = *(reinterpret_cast<half_float::half const*>(v0));
						half_float::half value1 = *(reinterpret_cast<half_float::half const*>(v1));
						half_float::half result = half_float::half_cast<half_float::half>((value1 - value0) * t);
						output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(half_float::half)], &result, sizeof(half_float::half));
						break;
					}
					case Vertex::DataType::Float:
					{
						float value0 = *(float const*)v0;
						float value1 = *(float const*)v1;
						float result = value0 + (float)((value1 - value0) * t);
						output.push_back(0); output.push_back(0); output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(float)], &result, sizeof(float));
						break;
					}
					case Vertex::DataType::Double:
					{
						double value0 = *(double const*)v0;
						double value1 = *(double const*)v1;
						double result = value0 + (value1 - value0) * t;
						output.push_back(0); output.push_back(0); output.push_back(0); output.push_back(0);
						output.push_back(0); output.push_back(0); output.push_back(0); output.push_back(0);
						memcpy(&output[output.size() - sizeof(double)], &result, sizeof(double));
						break;
					}
					default:
						throw MppMeshException("Invalid datatype for clipping.");
					}

					v0 += Vertex::getDataTypeSize(attrib.dataType);
					v1 += Vertex::getDataTypeSize(attrib.dataType);
				}
			}
		}

		bool VertexData::_inside(double px, double py, double p1x, double p1y, double p2x, double p2y) const
		{
			return (p2y - p1y) * px + (p1x - p2x) * py + (p2x * p1y - p1x * p2y) < 0;
		}

		void VertexData::_intersection(double cp1x, double cp1y, double cp2x, double cp2y, double sx, double sy, double ex, double ey, double& t) const
		{
			double dcx = cp2x - cp1x;
			double dcy = cp2y - cp1y;
			double dpx = ex - sx;
			double dpy = ey - sy;

			double det = dcx * dpy - dpx * dcy;
			t = (-dcy * (cp1x - sx) + dcx * (cp1y - sy)) / det;
		}

		void VertexData::_clipTriangleInputAgainstLine(double cp1x, double cp1y, double cp2x, double cp2y, vector<int8_t>& input, vector<int8_t>& output)
		{
			input = output;
			auto stride = getStride();

			// We rely on the first attribute being Position2.
			auto const& attrib = mSpec.getVertexBufferAttributeLayout(0).getAttribute(0);
			assert(attrib.component == Vertex::Component::Position2);
			auto positionType = attrib.dataType;

			size_t numVertices = output.size() / getStride();

			for (size_t i = 0; i < numVertices; ++i)
			{
				auto v0offset = stride * i;
				auto v1offset = stride * ((i + 1) % numVertices);

				int8_t const* v0ptr = &input[v0offset];
				int8_t const* v1ptr = &input[v1offset];

				double sx, sy, ex, ey;

				switch (positionType)
				{
				case Vertex::DataType::HalfFloat:
					sx = (double)(*(reinterpret_cast<half_float::half const*>(v0ptr))); 
					v0ptr += Vertex::getDataTypeSize(Vertex::DataType::HalfFloat);
					sy = (double)(*(reinterpret_cast<half_float::half const*>(v0ptr)));
					ex = (double)(*(reinterpret_cast<half_float::half const*>(v1ptr)));
					v1ptr += Vertex::getDataTypeSize(Vertex::DataType::HalfFloat);
					ey = (double)(*(reinterpret_cast<half_float::half const*>(v1ptr)));
					break;

				case Vertex::DataType::Float:
					sx = (double)(*(float const*)v0ptr);
					v0ptr += Vertex::getDataTypeSize(Vertex::DataType::Float);
					sy = (double)(*(float const*)v0ptr);
					ex = (double)(*(float const*)v1ptr);
					v1ptr += Vertex::getDataTypeSize(Vertex::DataType::Float);
					ey = (double)(*(float const*)v1ptr);
					break;

				case Vertex::DataType::Double:
					sx = *(double const*)v0ptr;
					v0ptr += Vertex::getDataTypeSize(Vertex::DataType::Double);
					sy = *(double const*)v0ptr;
					ex = *(double const*)v1ptr;
					v1ptr += Vertex::getDataTypeSize(Vertex::DataType::Double);
					ey = *(double const*)v1ptr;
					break;

				default:
					throw MppMeshException("Invalid datatype for clipping.");
				}

				// Case 1: Both vertices are inside:
				// Only the second vertex is added to the output list
				if (_inside(sx, sy, cp1x, cp1y, cp2x, cp2y) && _inside(ex, ey, cp1x, cp1y, cp2x, cp2y))
				{
					copy(input.begin() + v1offset, input.begin() + v1offset + stride, back_inserter(output));
				}

				// Case 2: First vertex is outside while second one is inside:
				// Both the point of intersection of the edge with the clip boundary
				// and the second vertex are added to the output list
				else if (!_inside(sx, sy, cp1x, cp1y, cp2x, cp2y) && _inside(ex, ey, cp1x, cp1y, cp2x, cp2y))
				{
					double t;
					_intersection(cp1x, cp1y, cp2x, cp2y, sx, sy, ex, ey, t);

					interpolateVertices(&input[v0offset], &input[v1offset], t, output);
					copy(input.begin() + v1offset, input.begin() + v1offset + stride, back_inserter(output));
				}

				// Case 3: First vertex is inside while second one is outside:
				// Only the point of intersection of the edge with the clip boundary
				// is added to the output list
				else if (_inside(sx, sy, cp1x, cp1y, cp2x, cp2y) && !_inside(ex, ey, cp1x, cp1y, cp2x, cp2y))
				{
					double t;
					_intersection(cp1x, cp1y, cp2x, cp2y, sx, sy, ex, ey, t);

					interpolateVertices(&input[v0offset], &input[v1offset], t, output);
				}
			}
		}

		void VertexData::clipTrianglesAgainstBoundingBox(float x0, float y0, float ex, float ey)
		{
			vector<int8_t> clippedData;
			for (size_t i = 0; i < getNumVertices(); i += 3)
			{
				// We pass in the data for 3 vertices into output.
				vector<int8_t> input, output;

				copy(mData.begin() + i * getStride(), 
					mData.begin() + (i + 3) * getStride(), 
					back_inserter(output));

				_clipTriangleInputAgainstLine(x0, y0, ex, y0, input, output);
				_clipTriangleInputAgainstLine(ex, y0, ex, ey, input, output);
				_clipTriangleInputAgainstLine(ex, ey, x0, ey, input, output);
				_clipTriangleInputAgainstLine(x0, ey, x0, y0, input, output);

				copy(output.begin(), output.end(), back_inserter(clippedData));
			}

			mData = clippedData;
			mOffset = (uint32_t)mData.size();
		}

		void VertexData::clipLinesAgainstBoundingBox(float x0, float y0, float ex, float ey)
		{

		}

		void VertexData::clipPointsAgainstBoundingBox(float x0, float y0, float ex, float ey)
		{

		}

		void VertexData::clipAgainstBoundingBox(float x0, float y0, float ex, float ey)
		{
			// First, check that the first attribute is Position2
			if (mSpec.getVertexBufferAttributeLayout(0).getAttribute(0).component != Vertex::Component::Position2)
			{
				throw MppMeshException("Mesh must start with Position2.");
			}

			if (mSpec.verticesIndexed())
			{
				throw MppMeshException("Mesh not be indexed.");
			}

			switch (mSpec.getPrimitiveType())
			{
			case Primitive::Type::Points:
				clipPointsAgainstBoundingBox(x0, y0, ex,  ey);
				break;

			case Primitive::Type::Lines:
				clipLinesAgainstBoundingBox(x0, y0, ex, ey);
				break;

			case Primitive::Type::Triangles:
				clipTrianglesAgainstBoundingBox(x0, y0, ex,  ey);
				break;

			default:
				break; // Should never get here
			}
		}
	}
}