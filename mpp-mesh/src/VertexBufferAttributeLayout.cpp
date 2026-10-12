#include <cassert>

#include "mpp/mesh/VertexBufferAttributeLayout.h"
#include "mpp/mesh/MppMeshException.h"

namespace mpp
{
	namespace mesh
	{

		using namespace std;

		VertexBufferAttributeLayout::VertexBufferAttributeLayout(int baseId, bool staticData)
			: mVertexSize(0)
			, mBaseId(baseId)
			, mStatic(staticData)
		{
		}

		/*
		 * Create a channel in the vertex buffer specification.
		 *
		 */
		void VertexBufferAttributeLayout::createAttribute(Vertex::Component component, Vertex::DataType dataType, bool normalised, size_t padToBoundary)
		{
			// Get identifier name
			string identifier;

			switch (component)
			{
			case mesh::Vertex::Component::Position2:
			case mesh::Vertex::Component::Position3:
			case mesh::Vertex::Component::Position4:
				identifier = "POSITION";
				break;
			case mesh::Vertex::Component::Normal3:
			case mesh::Vertex::Component::Normal4:
				identifier = "NORMAL";
				break;
			case mesh::Vertex::Component::Tangent4:
				identifier = "TANGENT";
				break;
			case mesh::Vertex::Component::TexCoord2:
			case mesh::Vertex::Component::TexCoord3:
			case mesh::Vertex::Component::TexCoord4:
				identifier = "TEXCOORDS";
				break;
			case mesh::Vertex::Component::Colour1:
			case mesh::Vertex::Component::Colour3:
			case mesh::Vertex::Component::Colour4:
				identifier = "COLOUR";
				break;
			case mesh::Vertex::Component::UserDefined1:
			case mesh::Vertex::Component::UserDefined2:
			case mesh::Vertex::Component::UserDefined3:
			case mesh::Vertex::Component::UserDefined4:
				identifier = "USER";
				break;
			}

			createAttribute(component, identifier, dataType, normalised, padToBoundary);
		}

		/*
		 * Create a channel in the vertex buffer specification.
		 *
		 */
		void VertexBufferAttributeLayout::createAttribute(Vertex::Component component, string const& identifier, Vertex::DataType dataType, bool normalised, size_t padToBoundary)
		{
			// Certain datatypes must be normalised for glVertexAttribPointer
			if ((dataType == Vertex::DataType::UnsignedInt_2_10_10_10_REV || dataType == Vertex::DataType::Int_2_10_10_10_REV) && !normalised)
			{
				THROW_MPP_MESH(Vertex::getDataTypeName(dataType) + " vertex attributes must be normalised.", __LINE__, __FILE__, __func__);
			}

			// Certain datatypes must have a particular size
			if ((dataType == Vertex::DataType::UnsignedInt_2_10_10_10_REV || dataType == Vertex::DataType::Int_2_10_10_10_REV) && Vertex::getComponentSize(component) != 4)
			{
				THROW_MPP_MESH(Vertex::getDataTypeName(dataType) + " vertex attributes must have 4 components.", __LINE__, __FILE__, __func__);
			}

			// TODO: check caps to ensure GL_MAX_VERTEX_ATTRIBS and GL_MAX_VERTEX_ATTRIB_STRIDE
			// ...

			// Add attrib
			Attribute attrib;

			attrib.attributeId = mBaseId + (int)mAttributes.size();
			attrib.identifier = identifier;
			attrib.component = component;
			attrib.dataType = dataType;
			attrib.padToBoundary = padToBoundary;
			attrib.normalised = normalised;

			if (padToBoundary < 2)
			{
				attrib.paddingBytes = 0;
			}
			else
			{
				auto byteSize = Vertex::getComponentSize(component) * Vertex::getDataTypeSize(dataType);
				auto padding = padToBoundary;
				while (padding < byteSize)
				{
					padding += padToBoundary;
				}

				attrib.paddingBytes = padding - byteSize;
			}

			attrib.offsetInBytes = mVertexSize;

			mAttributes.push_back(attrib);
			mVertexSize += (int)attrib.sizeInBytes();
		}

		/*
		 * Get the base id.
		 *
		 */
		int VertexBufferAttributeLayout::getBaseId() const
		{
			return mBaseId;
		}

		/*
		 * Get the number of channels.
		 *
		 */
		size_t VertexBufferAttributeLayout::getNumAttributes() const
		{
			return mAttributes.size();
		}

		bool VertexBufferAttributeLayout::isStatic() const
		{
			return mStatic;
		}

		/*
		 * Get the specified channel.
		 *
		 */
		VertexBufferAttributeLayout::Attribute const& VertexBufferAttributeLayout::getAttribute(size_t index) const
		{
			assert(index < getNumAttributes() && "VertexBufferAttributeLayout::getAttribute() 'index' argument out of range!");
			return mAttributes[index];
		}

		/*
		 * Get the specified channel.
		 *
		 */
		VertexBufferAttributeLayout::Attribute& VertexBufferAttributeLayout::getAttribute(size_t index)
		{
			assert(index < getNumAttributes() && "VertexBufferAttributeLayout::getAttribute() 'index' argument out of range!");
			return mAttributes[index];
		}

		int VertexBufferAttributeLayout::getVertexSize() const
		{
			return mVertexSize;
		}
	}
}
