#pragma once

#undef min
#undef max

#include <limits>

#include "Vertex.h"

namespace mpp
{
	namespace mesh
	{
		template<int N>
		struct VertexComponentSize
		{
			int value = N;
		};

		template<typename T>
		struct VertexDataType
		{
			Vertex::DataType value = Vertex::DataType::None;
		};

		template<>
		struct VertexDataType<int8_t>
		{
			Vertex::DataType value = Vertex::DataType::Byte;
		};

		template<>
		struct VertexDataType<uint8_t>
		{
			Vertex::DataType value = Vertex::DataType::UnsignedByte;
		};

		template<>
		struct VertexDataType<int16_t>
		{
			Vertex::DataType value = Vertex::DataType::Short;
		};

		template<>
		struct VertexDataType<uint16_t>
		{
			Vertex::DataType value = Vertex::DataType::UnsignedShort;
		};

		template<>
		struct VertexDataType<int32_t>
		{
			Vertex::DataType value = Vertex::DataType::Int;
		};

		template<>
		struct VertexDataType<uint32_t>
		{
			Vertex::DataType value = Vertex::DataType::UnsignedInt;
		};

		template<>
		struct VertexDataType<float>
		{
			Vertex::DataType value = Vertex::DataType::Float;
		};

		template<>
		struct VertexDataType<double>
		{
			Vertex::DataType value = Vertex::DataType::Double;
		};

		// Type definitions
		struct DataTypeNone
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::None; }
			typedef void builtin_type;
			
			static size_t size() { return 0; }
		};

		// Half-precision conversion support is optional. Include
		// VertexHalfTypeSpecification.h when this adapter is needed.
		struct DataTypeHalfFloat;

		struct DataTypeFloat
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Float; }
			typedef float builtin_type;

			template<typename T> static float value(T v, bool normalise=false) { (void)(normalise); return (float)v; }
			
			static float min_normalised() { return -1.0f; }
			static float max_normalised() { return 1.0f; }
			
			template<typename T> static float* ptr(T* p) { return (float*)p; }
			static size_t size() { return sizeof(float); }
		};

		struct DataTypeDouble
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Double; }
			typedef double builtin_type;

			template<typename T> static double value(T v, bool normalise=false) { (void)(normalise); return (double)v; }
			
			static double min_normalised() { return -1.0; }
			static double max_normalised() { return 1.0; }
			
			template<typename T> static double* ptr(T* p) { return (double*)p; }
			static size_t size() { return sizeof(double); }
		};

		struct DataTypeByte
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Byte; }
			typedef int8_t builtin_type;

			template<typename T> static int8_t value(T v, bool normalise) { return normalise ? (int8_t)(((double)v * 0.5 + 0.5) * 255.0) - 128 : (int8_t)v; }
			static int8_t value(float v, bool normalise)            { return normalise ? (int8_t)((v * 0.5f + 0.5f) * 255.0f) - 128 : (int8_t)v; }
			static int8_t value(double v, bool normalise)           { return normalise ? (int8_t)((v * 0.5 + 0.5) * 255.0) - 128    : (int8_t)v; }
			static int8_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (int8_t)v; }
			static int8_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (int8_t)v; }
			static int8_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (int8_t)v; }
			static int8_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (int8_t)v; }
			static int8_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (int8_t)v; }
			static int8_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (int8_t)v; }
			
			static int8_t min_normalised() { return std::numeric_limits<int8_t>::min(); }
			static int8_t max_normalised() { return std::numeric_limits<int8_t>::max(); }
			
			template<typename T> static int8_t* ptr(T* p) { return (int8_t*)p; }
			static size_t size() { return sizeof(int8_t); }
		};

		struct DataTypeUnsignedByte
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::UnsignedByte; }
			typedef uint8_t builtin_type;

			template<typename T> static uint8_t value(T v, bool normalise) { return normalise ? (uint8_t)((double)v * 255.0) : (uint8_t)v; }
			static uint8_t value(float v, bool normalise)            { return normalise ? (uint8_t)(v * 255.0f) : (uint8_t)v; }
			static uint8_t value(double v, bool normalise)           { return normalise ? (uint8_t)(v * 255.0)  : (uint8_t)v; }
			static uint8_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (uint8_t)v; }
			static uint8_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (uint8_t)v; }
			static uint8_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (uint8_t)v; }
			static uint8_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (uint8_t)v; }
			static uint8_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (uint8_t)v; }
			static uint8_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (uint8_t)v; }
			
			static uint8_t min_normalised() { return std::numeric_limits<uint8_t>::min(); }
			static uint8_t max_normalised() { return std::numeric_limits<uint8_t>::max(); }
			
			template<typename T> static uint8_t* ptr(T* p) { return (uint8_t*)p; }
			static size_t size() { return sizeof(uint8_t); }
		};

		struct DataTypeShort
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Short; }
			typedef int16_t builtin_type;

			template<typename T> static int16_t value(T v, bool normalise) { return normalise ? (int16_t)(((double)v * 0.5 + 0.5) * 65535.0) - 32768 : (int16_t)v; }
			static int16_t value(float v, bool normalise)            { return normalise ? (int16_t)((v * 0.5f + 0.5f) * 65535.0f) - 32768 : (int16_t)v; }
			static int16_t value(double v, bool normalise)           { return normalise ? (int16_t)((v * 0.5 + 0.5) * 65535.0) - 32768    : (int16_t)v; }
			static int16_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (int16_t)v; }
			static int16_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (int16_t)v; }
			static int16_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (int16_t)v; }
			static int16_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (int16_t)v; }
			static int16_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (int16_t)v; }
			static int16_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (int16_t)v; }
		
			static int16_t min_normalised() { return std::numeric_limits<int16_t>::min(); }
			static int16_t max_normalised() { return std::numeric_limits<int16_t>::max(); }
		
			template<typename T> static int16_t* ptr(T* p) { return (int16_t*)p; }
			static size_t size() { return sizeof(int16_t); }
		};

		struct DataTypeUnsignedShort
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::UnsignedShort; }
			typedef uint16_t builtin_type;

			template<typename T> static uint16_t value(T v, bool normalise) { return normalise ? (uint16_t)((double)v * 65535.0) : (uint16_t)v; }
			static uint16_t value(float v, bool normalise)            { return normalise ? (uint16_t)(v * 65535.0f) : (uint16_t)v; }
			static uint16_t value(double v, bool normalise)           { return normalise ? (uint16_t)(v * 65535.0)  : (uint16_t)v; }
			static uint16_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (uint16_t)v; }
			static uint16_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (uint16_t)v; }
			static uint16_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (uint16_t)v; }
			static uint16_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (uint16_t)v; }
			static uint16_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (uint16_t)v; }
			static uint16_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (uint16_t)v; }
			
			static uint16_t min_normalised() { return std::numeric_limits<uint16_t>::min(); }
			static uint16_t max_normalised() { return std::numeric_limits<uint16_t>::max(); }
			
			template<typename T> static uint16_t* ptr(T* p) { return (uint16_t*)p; }
			static size_t size() { return sizeof(uint16_t); }
		};

		struct DataTypeInt
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Int; }
			typedef int32_t builtin_type;

			template<typename T> static int32_t value(T v, bool normalise) { return normalise ? (int32_t)(((double)v * 0.5 + 0.5) * 4294967295.0) - 2147483648 : (int32_t)v; }
			static int32_t value(float v, bool normalise)            { return normalise ? (int32_t)((v * 0.5f + 0.5f) * 4294967295.0f) - 2147483648 : (int32_t)v; }
			static int32_t value(double v, bool normalise)           { return normalise ? (int32_t)((v * 0.5 + 0.5) * 4294967295.0) - 2147483648    : (int32_t)v; }
			static int32_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (int32_t)v; }
			static int32_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (int32_t)v; }
			static int32_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (int32_t)v; }
			static int32_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (int32_t)v; }
			static int32_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (int32_t)v; }
			static int32_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (int32_t)v; }
			
			static int32_t min_normalised() { return std::numeric_limits<int32_t>::min(); }
			static int32_t max_normalised() { return std::numeric_limits<int32_t>::max(); }
			
			template<typename T> static int32_t* ptr(T* p) { return (int32_t*)p; }
			static size_t size() { return sizeof(int32_t); }
		};

		struct DataTypeUnsignedInt
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::UnsignedInt; }
			typedef uint32_t builtin_type;

			template<typename T> static uint32_t value(T v, bool normalise) { return normalise ? (uint32_t)((double)v * 4294967295.0) : (uint32_t)v; }
			static uint32_t value(float v, bool normalise)            { return normalise ? (uint32_t)(v * 4294967295.0f) : (uint32_t)v; }
			static uint32_t value(double v, bool normalise)           { return normalise ? (uint32_t)(v * 4294967295.0)  : (uint32_t)v; }
			static uint32_t value(int8_t v, bool normalise=false)       { (void)(normalise); return (uint32_t)v; }
			static uint32_t value(int16_t v, bool normalise=false)      { (void)(normalise); return (uint32_t)v; }
			static uint32_t value(int32_t v, bool normalise=false)      { (void)(normalise); return (uint32_t)v; }
			static uint32_t value(uint8_t v, bool normalise=false)      { (void)(normalise); return (uint32_t)v; }
			static uint32_t value(uint16_t v, bool normalise=false)     { (void)(normalise); return (uint32_t)v; }
			static uint32_t value(uint32_t v, bool normalise=false)     { (void)(normalise); return (uint32_t)v; }
			
			static uint32_t min_normalised() { return std::numeric_limits<uint32_t>::min(); }
			static uint32_t max_normalised() { return std::numeric_limits<uint32_t>::max(); }
			
			template<typename T> static uint32_t* ptr(T* p) { return (uint32_t*)p; }
			static size_t size() { return sizeof(uint32_t); }
		};

		struct DataType2_10_10_10
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::Int_2_10_10_10_REV; }
			typedef int32_t builtin_type;

			template<typename T> static int32_t value(T v, bool normalise=false) { (void)(normalise); return (int32_t)v; }
			
			template<typename T> static int32_t* ptr(T* p) { return (int32_t*)p; }
			static size_t size() { return sizeof(int32_t); }
		};

		struct DataTypeUnsigned2_10_10_10
		{
			static Vertex::DataType vertexDataType() { return Vertex::DataType::UnsignedInt_2_10_10_10_REV; }
			typedef uint32_t builtin_type;

			template<typename T> static uint32_t value(T v, bool normalise=false) { (void)(normalise); return (uint32_t)v; }
			
			template<typename T> static uint32_t* ptr(T* p) { return (uint32_t*)p; }
			static size_t size() { return sizeof(uint32_t); }
		};
	}
}