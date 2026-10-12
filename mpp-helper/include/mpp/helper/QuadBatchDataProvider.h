#pragma once

#include <mpp/BatchDataProvider.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "Config.h"

/*
There are two classes:
- QuadBatchDataProvider
- QuadBatchRenderer

QuadBatchDataProvider is templated with required position type, texture type and an optional colour type.
There is a specialisation for no colour which does not provide a colour() function.

To render a quad batch, you create a subclass of QuadBatchDataProvider, eg as follows:

class MyDataProvider : public QuadBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>

Or, if you want to be able to specify types:

template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
class MyDataProvider : public QuadBatchDataProvider<PosType, TexType, ColType> {};

// Specialization (to be used) for our data provider
template<>
class MyDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte> : public QuadBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
{
	// ...
};

The above means that you will need to create a custom implementation for each specialization, of course.

Which functions out of position(), angle() textureAtlasCoords() and colour() you need to implement depends on the options you pass in
to the QuadBatchRenderer.

*/

namespace mpp
{
	namespace helper
	{

		template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
		class QuadBatchDataProvider : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t index, typename PosType::builtin_type& x, typename PosType::builtin_type& y) = 0;

			virtual void angle(uint32_t index, float& angle) = 0;

			virtual void direction(uint32_t index, float&x, float& y) = 0;

			virtual void textureAtlasTexcoords(uint32_t index, typename TexType::builtin_type& u0, typename TexType::builtin_type& v0, typename TexType::builtin_type& u1, typename TexType::builtin_type& v1) = 0;

			virtual void dimensions(uint32_t index, float& halfWidth, float& halfHeight) = 0;

			virtual void colour(uint32_t index, typename ColType::builtin_type& red, typename ColType::builtin_type& green, typename ColType::builtin_type& blue, typename ColType::builtin_type& alpha) = 0;

			virtual mpp::Colour diffuse() = 0;
		};

		template<typename PosType, typename TexType>
		class QuadBatchDataProvider<PosType, TexType, mpp::mesh::DataTypeNone> : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t index, typename PosType::builtin_type& x, typename PosType::builtin_type& y) = 0;

			virtual void angle(uint32_t index, float& angle) = 0;

			virtual void direction(uint32_t index, float&x, float& y) = 0;

			virtual void textureAtlasTexcoords(uint32_t index, typename TexType::builtin_type& u0, typename TexType::builtin_type& v0, typename TexType::builtin_type& u1, typename TexType::builtin_type& v1) = 0;

			virtual void dimensions(uint32_t index, float& halfWidth, float& halfHeight) = 0;
			
			virtual mpp::Colour diffuse() = 0;
		};

	}
}