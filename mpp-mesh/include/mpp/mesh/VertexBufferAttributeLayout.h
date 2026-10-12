#pragma once

#include <vector>

#include "Config.h"
#include "Vertex.h"

namespace mpp
{
	namespace mesh
	{

		class _MPPMESHAPI VertexBufferAttributeLayout
		{
		public:

			struct Attribute
			{
				int attributeId{ -1 };
				std::string identifier;
				Vertex::Component component;
				Vertex::DataType dataType;
				size_t paddingBytes, padToBoundary; // padToBoundary required for (de)serialization
				bool normalised;

				int offsetInBytes;

			public:

				size_t sizeInBytes() const
				{
					return Vertex::getComponentSize(component) * Vertex::getDataTypeSize(dataType) + paddingBytes;
				}
			};

		private:

			std::vector<Attribute> mAttributes;

			int mVertexSize;

			int mBaseId;

			bool mStatic;

		public:

			VertexBufferAttributeLayout(int baseId, bool staticData);

			void createAttribute(Vertex::Component component, Vertex::DataType dataType, bool normalised, size_t padToBoundary = 1);

			void createAttribute(Vertex::Component component, std::string const& identifier, Vertex::DataType dataType, bool normalised, size_t padToBoundary = 1);

			int getBaseId() const;

			size_t getNumAttributes() const;

			bool isStatic() const;

			Attribute const& getAttribute(size_t index) const;

			Attribute& getAttribute(size_t index);

			int getVertexSize() const;
		};

	}
}