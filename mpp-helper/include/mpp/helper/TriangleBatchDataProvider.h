#pragma once

#include <mpp/BatchDataProvider.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "Config.h"

namespace mpp
{
	namespace helper
	{

		template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
		class TriangleBatch2DDataProvider : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t batch, uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0,
				typename PosType::builtin_type& x1, typename PosType::builtin_type& y1,
				typename PosType::builtin_type& x2, typename PosType::builtin_type& y2) = 0;

			virtual void texcoords(uint32_t batch, uint32_t index, typename TexType::builtin_type& u0, typename TexType::builtin_type& v0,
				typename TexType::builtin_type& u1, typename TexType::builtin_type& v1,
				typename TexType::builtin_type& u2, typename TexType::builtin_type& v2) = 0;

			virtual void colour(uint32_t batch, uint32_t index, typename ColType::builtin_type& red, typename ColType::builtin_type& green, typename ColType::builtin_type& blue, typename ColType::builtin_type& alpha) = 0;

			virtual mpp::Colour diffuse(uint32_t batch) = 0;
		};

		template<typename PosType, typename TexType>
		class TriangleBatch2DDataProvider<PosType, TexType, mpp::mesh::DataTypeNone> : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t batch, uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0,
				typename PosType::builtin_type& x1, typename PosType::builtin_type& y1,
				typename PosType::builtin_type& x2, typename PosType::builtin_type& y2) = 0;

			virtual void texcoords(uint32_t batch, uint32_t index, typename TexType::builtin_type& u0, typename TexType::builtin_type& v0,
				typename TexType::builtin_type& u1, typename TexType::builtin_type& v1,
				typename TexType::builtin_type& u2, typename TexType::builtin_type& v2) {}

			virtual mpp::Colour diffuse(uint32_t batch) = 0;

		};

		template<typename PosType, typename TexType, typename ColType>
		class TriangleBatch3DDataProvider : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0, typename PosType::builtin_type& z0,
				typename PosType::builtin_type& x1, typename PosType::builtin_type& y1, typename PosType::builtin_type& z1,
				typename PosType::builtin_type& x2, typename PosType::builtin_type& y2, typename PosType::builtin_type& z2) = 0;

			virtual void normal(uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0, typename PosType::builtin_type& z0,
				typename PosType::builtin_type& x1, typename PosType::builtin_type& y1, typename PosType::builtin_type& z1,
				typename PosType::builtin_type& x2, typename PosType::builtin_type& y2, typename PosType::builtin_type& z2) = 0;

			virtual void texcoords(uint32_t index, typename TexType::builtin_type& u0, typename TexType::builtin_type& v0,
				typename TexType::builtin_type& u1, typename TexType::builtin_type& v1,
				typename TexType::builtin_type& u2, typename TexType::builtin_type& v2) = 0;

			virtual void colour(uint32_t index, typename ColType::builtin_type& red, typename ColType::builtin_type& green, typename ColType::builtin_type& blue, typename ColType::builtin_type& alpha) = 0;

			virtual mpp::Colour diffuse() = 0;
		};

		template<typename PosType, typename TexType, typename ColType>
		class TriangleBatch2DBufferDataProvider : public BatchDataProvider
		{
		public:

			virtual uint32_t getNumVertices() const = 0;

			virtual int8_t* getVertexData() const = 0;

			virtual uint32_t getVertexDataSize() const = 0;

			virtual int8_t* getIndexData() const = 0;

			virtual uint32_t getNumIndices() const = 0;

			virtual uint32_t getIndexWidth() const = 0;

			virtual mpp::Colour diffuse() = 0;
		};

		template<typename PosType, typename TexType, typename ColType>
		class TriangleBatch3DBufferDataProvider : public BatchDataProvider
		{
		public:

			virtual uint32_t getNumVertices() const = 0;

			virtual int8_t* getVertexData(uint32_t meshIndex) const = 0;

			virtual uint32_t getVertexDataSize(uint32_t meshIndex) const = 0;

			virtual int8_t* getIndexData(uint32_t meshIndex) const = 0;

			virtual uint32_t getNumIndices(uint32_t meshIndex) const = 0;

			virtual uint32_t getIndexWidth() const = 0;

			virtual mpp::Colour diffuse() = 0;
		};

	}
}