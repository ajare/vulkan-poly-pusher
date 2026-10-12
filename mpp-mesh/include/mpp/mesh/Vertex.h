#pragma once

#include <string>

#include "Config.h"

namespace mpp
{
	namespace mesh
	{

		struct _MPPMESHAPI Vertex
		{
			enum class DataType
			{
				None,
				Byte,
				UnsignedByte,
				Short,
				UnsignedShort,
				Int,
				UnsignedInt,
				HalfFloat,
				Float,
				Double,
				Int_2_10_10_10_REV,
				UnsignedInt_2_10_10_10_REV
			};

			enum class Component
			{
				Unused = 0,
				Position2 = 1 << 0,
				Position3 = 1 << 1,
				Position4 = 1 << 2,
				Normal3 = 1 << 3,
				Normal4 = 1 << 4,
				TexCoord2 = 1 << 5,
				TexCoord3 = 1 << 6,
				TexCoord4 = 1 << 7,
				Colour1 = 1 << 8,
				Colour3 = 1 << 9,
				Colour4 = 1 << 10,
				UserDefined1 = 1 << 11,
				UserDefined2 = 1 << 12,
				UserDefined3 = 1 << 13,
				UserDefined4 = 1 << 14,
				Tangent4 = 1 << 15
			};

			static size_t getComponentSize(Component component);

			static size_t getDataTypeSize(DataType dataType);

			static bool isDataTypeNormalisable(DataType dataType);

			static bool isDataTypeFloatingPoint(DataType dataType);

			static std::string getComponentName(Component component);

			static std::string getDataTypeName(DataType dataType);

		};
		
	}
}