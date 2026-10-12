#pragma once

// Optional adapter for clients that explicitly use the external half library.
// The normal mesh API does not include or expose half's headers.
#include <half/half.hpp>

#include "VertexTypeSpecification.h"

namespace mpp::mesh
{
	template<>
	struct VertexDataType<half_float::half>
	{
		Vertex::DataType value = Vertex::DataType::HalfFloat;
	};

	struct DataTypeHalfFloat
	{
		static Vertex::DataType vertexDataType() { return Vertex::DataType::HalfFloat; }
		using builtin_type = half_float::half;

		template<typename T>
		static half_float::half value(T v, bool normalise = false)
		{
			(void)normalise;
			return half_float::half(v);
		}

		static half_float::half min_normalised() { return half_float::half(-1.0f); }
		static half_float::half max_normalised() { return half_float::half(1.0f); }

		template<typename T>
		static half_float::half* ptr(T* p) { return reinterpret_cast<half_float::half*>(p); }

		static size_t size() { return sizeof(half_float::half); }
	};
}
