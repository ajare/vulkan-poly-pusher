#include <cassert>
#include <iomanip>
#include <sstream>

#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	namespace mesh
	{
		using namespace std;

		/*
		 * Constructor.
		 *
		 */
		MeshSpecification::MeshSpecification()
			: mPrimitiveType(Primitive::Type::Triangles)
			, mStorageType(VertexBufferStorageType::Static)
			, mIndexedVertices(false)
		{
			mVertexBufferAttributeLayouts.reserve(16);
		}

		/*
		 * Constructor.
		 *
		 */
		MeshSpecification::MeshSpecification(Primitive::Type primitiveType)
			: mPrimitiveType(primitiveType)
			, mStorageType(VertexBufferStorageType::Static)
			, mIndexedVertices(false)
		{
			mVertexBufferAttributeLayouts.reserve(16);
		}

		/*
		 * Constructor.
		 *
		 */
		MeshSpecification::MeshSpecification(Primitive::Type primitiveType, VertexBufferStorageType storageType)
			: mPrimitiveType(primitiveType)
			, mStorageType(storageType)
			, mIndexedVertices(false)
		{
			mVertexBufferAttributeLayouts.reserve(16);
		}

		/*
		 * Copy constructor.
		 *
		 */
		MeshSpecification::MeshSpecification(MeshSpecification const& other)
		{
			this->mPrimitiveType = other.mPrimitiveType;
			this->mStorageType = other.mStorageType;
			this->mIndexedVertices = other.mIndexedVertices;
			this->mVertexBufferAttributeLayouts = other.mVertexBufferAttributeLayouts;
		}

		/*
		 * Compare two MeshSpecification for equality.
		 *
		 */
		bool MeshSpecification::compare(MeshSpecification const& other) const
		{
			if (mPrimitiveType != other.mPrimitiveType)
			{
				return false;
			}
			if (mStorageType != other.mStorageType)
			{
				return false;
			}
			if (mIndexedVertices != other.mIndexedVertices)
			{
				return false;
			}
			if (getNumVertexBufferAttributeLayouts() != other.getNumVertexBufferAttributeLayouts())
			{
				return false;
			}

			for (size_t i = 0; i < getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto const& thisLayout = getVertexBufferAttributeLayout((uint32_t)i);
				auto const& otherLayout = other.getVertexBufferAttributeLayout((uint32_t)i);

				if (thisLayout.getBaseId() != otherLayout.getBaseId())
				{
					return false;
				}
				if (thisLayout.isStatic() != otherLayout.isStatic())
				{
					return false;
				}
				if (thisLayout.getNumAttributes() != otherLayout.getNumAttributes())
				{
					return false;
				}

				for (size_t j = 0; j < thisLayout.getNumAttributes(); ++j)
				{
					auto const& thisAttr = thisLayout.getAttribute(j);
					auto const& otherAttr = otherLayout.getAttribute(j);

					if (thisAttr.attributeId != otherAttr.attributeId)
					{
						return false;
					}
					if (thisAttr.component != otherAttr.component)
					{
						return false;
					}
					if (thisAttr.dataType != otherAttr.dataType)
					{
						return false;
					}
					if (thisAttr.identifier != otherAttr.identifier)
					{
						return false;
					}
					if (thisAttr.normalised != otherAttr.normalised)
					{
						return false;
					}
					if (thisAttr.offsetInBytes != otherAttr.offsetInBytes)
					{
						return false;
					}
					if (thisAttr.paddingBytes != otherAttr.paddingBytes)
					{
						return false;
					}
					if (thisAttr.padToBoundary != otherAttr.padToBoundary)
					{
						return false;
					}
				}
			}

			return true;
		}

		/*
		 * Comparison operator.
		 *
		 */
		bool MeshSpecification::operator==(MeshSpecification const& other) const
		{
			return compare(other);
		}

		/*
		 * Comparison operator.
		 *
		 */
		bool MeshSpecification::operator!=(MeshSpecification const& other) const
		{
			return !compare(other);
		}

		/*
		 * Set the primitive type, ie points/lines/triangles, etc.
		 *
		 */
		void MeshSpecification::setPrimitiveType(Primitive::Type primitiveType)
		{
			mPrimitiveType = primitiveType;
		}
		
		/*
		 * Get the primitive type, ie points/lines/triangles, etc.
		 *
		 */
		Primitive::Type MeshSpecification::getPrimitiveType() const
		{
			return mPrimitiveType;
		}

		/*
		 * Set the storage type, ie static or dynamic (for updating).
		 *
		 */
		void MeshSpecification::setStorageType(VertexBufferStorageType storageType)
		{
			mStorageType = storageType;
		}

		/*
		 * Get the storage type.
		 *
		 */
		VertexBufferStorageType MeshSpecification::getStorageType() const
		{
			return mStorageType;
		}

		/*
		 * Set vertices indexed.
		 *
		 */
		void MeshSpecification::setIndexedVertices(bool indexed)
		{
			mIndexedVertices = indexed;
		}

		/*
		 * Are vertices indexed?  If the primitive type is points, then there is no point in indexing them, it is just inefficient.
		 *
		 */
		bool MeshSpecification::verticesIndexed() const
		{
			return (mPrimitiveType != Primitive::Type::Points) && mIndexedVertices;
		}

		/*
		 * Create attribute layout.
		 *
		 */
		VertexBufferAttributeLayout* MeshSpecification::createVertexBufferAttributeLayout(bool staticData)
		{
			int baseId = mVertexBufferAttributeLayouts.empty() ? 0 
				: mVertexBufferAttributeLayouts.back().getBaseId() + (int)mVertexBufferAttributeLayouts.back().getNumAttributes();
			mVertexBufferAttributeLayouts.push_back(VertexBufferAttributeLayout(baseId, staticData));
			return &(mVertexBufferAttributeLayouts.back());
		}

		/*
		 * Get number of attribute layouts.
		 *
		 */
		size_t MeshSpecification::getNumVertexBufferAttributeLayouts() const
		{
			return mVertexBufferAttributeLayouts.size();
		}

		/*
	 	 * Get specified attribute layout.
		 *
		 */
		VertexBufferAttributeLayout const& MeshSpecification::getVertexBufferAttributeLayout(uint32_t index) const
		{
			assert(index < getNumVertexBufferAttributeLayouts() && "MeshSpecification::VertexBufferAttributeLayout::getVertexBufferAttributeLayout() 'index' argument out of range!");
			return mVertexBufferAttributeLayouts[index];
		}

		/*
		 * Get specified attribute layout.
		 *
		 */
		VertexBufferAttributeLayout& MeshSpecification::getVertexBufferAttributeLayout(uint32_t index)
		{
			assert(index < getNumVertexBufferAttributeLayouts() && "MeshSpecification::VertexBufferAttributeLayout::getVertexBufferAttributeLayout() 'index' argument out of range!");
			return mVertexBufferAttributeLayouts[index];
		}

		/*
		 * Get the Mesh's vertex stride.
		 *
		 */
		size_t MeshSpecification::getVertexStrideInBytes() const
		{
			size_t stride{ 0 };

			for (size_t i = 0; i < getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto layout = getVertexBufferAttributeLayout((uint32_t)i);

				for (size_t j = 0; j < layout.getNumAttributes(); ++j)
				{
					auto attrib = layout.getAttribute(j);
					stride += attrib.sizeInBytes();
				}
			}

			return stride;
		}

		/*
		 * Get the number of individual components that make up a vertex.
		 *
		 */
		size_t MeshSpecification::getNumComponents() const
		{
			size_t count{ 0 };

			for (size_t i = 0; i < getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto layout = getVertexBufferAttributeLayout((uint32_t)i);

				for (size_t j = 0; j < layout.getNumAttributes(); ++j)
				{
					auto attrib = layout.getAttribute(j);
					count += Vertex::getComponentSize(attrib.component);
				}
			}

			return count;

		}

		/*
		 * Get a name.
		 *
		 */
		string MeshSpecification::getDescriptor(string const& prefix) const
		{
			string descriptor = prefix;
			switch (mPrimitiveType)
			{
			case Primitive::Type::Points: descriptor += "points"; break;
			case Primitive::Type::Lines: descriptor += "lines"; break;
			case Primitive::Type::Triangles: descriptor += "tris"; break;
			default: descriptor += "primitive"; break;
			}

			// Keep a readable semantic summary for diagnostics, but append the hash
			// of the complete canonical key so data types, ordering and layout details
			// no longer collapse onto the same generated name.
			for (auto const& layout : mVertexBufferAttributeLayouts)
			{
				for (size_t index = 0; index < layout.getNumAttributes(); ++index)
				{
					auto const& attribute = layout.getAttribute(index);
					descriptor += "_" + Vertex::getComponentName(attribute.component);
				}
			}
			ostringstream suffix;
			suffix << '_' << hex << setw(8) << setfill('0') << getHashCode();
			return descriptor + suffix.str();
		}

		uint32_t MeshSpecification::getHashCode() const
		{
			// FNV-1a is deterministic across standard-library implementations. Cache
			// identity uses the full canonical string below; this digest is intended
			// for compact descriptors and hash containers, where equality must still
			// resolve collisions.
			uint32_t hash = 2166136261u;
			for (unsigned char value : getHashString())
			{
				hash ^= value;
				hash *= 16777619u;
			}
			return hash;
		}

		string MeshSpecification::getHashString() const
		{
			// This is a canonical, unambiguous serialization of every field used by
			// operator==. Delimiters separate numeric fields and identifiers are
			// length-prefixed, so e.g. [1, 11] cannot collide with [11, 1].
			ostringstream key;
			key << "mpp-mesh-v2"
				<< "|primitive=" << static_cast<uint32_t>(mPrimitiveType)
				<< "|storage=" << static_cast<uint32_t>(mStorageType)
				<< "|indexed=" << (mIndexedVertices ? 1 : 0)
				<< "|layouts=" << mVertexBufferAttributeLayouts.size();

			for (auto const& layout : mVertexBufferAttributeLayouts)
			{
				key << "|layout={base=" << layout.getBaseId()
					<< ",static=" << (layout.isStatic() ? 1 : 0)
					<< ",attributes=" << layout.getNumAttributes();
				for (size_t index = 0; index < layout.getNumAttributes(); ++index)
				{
					auto const& attribute = layout.getAttribute(index);
					key << "|attribute={id=" << attribute.attributeId
						<< ",component=" << static_cast<uint32_t>(attribute.component)
						<< ",type=" << static_cast<uint32_t>(attribute.dataType)
						<< ",identifier=" << attribute.identifier.size() << ':' << attribute.identifier
						<< ",normalised=" << (attribute.normalised ? 1 : 0)
						<< ",offset=" << attribute.offsetInBytes
						<< ",padding=" << attribute.paddingBytes
						<< ",boundary=" << attribute.padToBoundary << '}';
				}
				key << '}';
			}
			return key.str();
		}
	}
}

