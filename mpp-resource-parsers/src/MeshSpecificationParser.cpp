#include "utils/StringUtils.h"
#include "mpp/data/StructuredData.h"

#include "mpp/resource-parsers/MeshSpecificationParser.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		MeshSpecificationParser::MeshSpecificationParser(string const& filepath)
			: mFilepath(filepath)
		{
			// Primitives
			mMeshSpecificationPrimitive["POINTS"] = mesh::Primitive::Type::Points;
			mMeshSpecificationPrimitive["LINES"] = mesh::Primitive::Type::Lines;
			mMeshSpecificationPrimitive["TRIANGLES"] = mesh::Primitive::Type::Triangles;

			// Storage
			mMeshSpecificationStorage["STATIC"] = mesh::VertexBufferStorageType::Static;
			mMeshSpecificationStorage["DYNAMIC"] = mesh::VertexBufferStorageType::Dynamic;

			// Components
			mComponentTypes["POSITION2"] = mpp::mesh::Vertex::Component::Position2;
			mComponentTypes["POSITION3"] = mpp::mesh::Vertex::Component::Position3;
			mComponentTypes["POSITION4"] = mpp::mesh::Vertex::Component::Position4;
			mComponentTypes["NORMAL3"] = mpp::mesh::Vertex::Component::Normal3;
			mComponentTypes["NORMAL4"] = mpp::mesh::Vertex::Component::Normal4;
			mComponentTypes["TEXCOORD2"] = mpp::mesh::Vertex::Component::TexCoord2;
			mComponentTypes["TEXCOORD3"] = mpp::mesh::Vertex::Component::TexCoord3;
			mComponentTypes["TEXCOORD4"] = mpp::mesh::Vertex::Component::TexCoord4;
			mComponentTypes["COLOUR1"] = mpp::mesh::Vertex::Component::Colour1;
			mComponentTypes["COLOUR3"] = mpp::mesh::Vertex::Component::Colour3;
			mComponentTypes["COLOUR4"] = mpp::mesh::Vertex::Component::Colour4;
			mComponentTypes["TANGENT4"] = mpp::mesh::Vertex::Component::Tangent4;
			mComponentTypes["USER1"] = mpp::mesh::Vertex::Component::UserDefined1;
			mComponentTypes["USER2"] = mpp::mesh::Vertex::Component::UserDefined2;
			mComponentTypes["USER3"] = mpp::mesh::Vertex::Component::UserDefined3;
			mComponentTypes["USER4"] = mpp::mesh::Vertex::Component::UserDefined4;

			// Datatypes
			mDataTypes["FLOAT16"] = mpp::mesh::Vertex::DataType::HalfFloat;
			mDataTypes["FLOAT32"] = mpp::mesh::Vertex::DataType::Float;
			mDataTypes["INT8"] = mpp::mesh::Vertex::DataType::Byte;
			mDataTypes["INT16"] = mpp::mesh::Vertex::DataType::Short;
			mDataTypes["INT32"] = mpp::mesh::Vertex::DataType::Int;
			mDataTypes["UINT8"] = mpp::mesh::Vertex::DataType::UnsignedByte;
			mDataTypes["UINT16"] = mpp::mesh::Vertex::DataType::UnsignedShort;
			mDataTypes["UINT32"] = mpp::mesh::Vertex::DataType::UnsignedInt;
		}

		mesh::Vertex::Component MeshSpecificationParser::parseMeshSpecificationBufferChannelComponent(string const& value)
		{
			auto it = mComponentTypes.find(value);

			if (it != mComponentTypes.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + mFilepath + ".  Unknown/unsupported component '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		mesh::Vertex::DataType MeshSpecificationParser::parseMeshSpecificationBufferChannelType(string const& value)
		{
			auto it = mDataTypes.find(value);

			if (it != mDataTypes.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + mFilepath + ".  Unknown/unsupported datatype '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		void MeshSpecificationParser::parseMeshSpecificationBufferChannel(mpp::data::StructuredData const& data, mesh::VertexBufferAttributeLayout* layout)
		{
			mesh::Vertex::Component component{ mesh::Vertex::Component::Unused };
			mesh::Vertex::DataType datatype{ mesh::Vertex::DataType::None };
			bool normalised{ false };

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "data")
				{
					component = parseMeshSpecificationBufferChannelComponent(value);
				}
				else if (entry.first == "type")
				{
					datatype = parseMeshSpecificationBufferChannelType(value);
				}
				else if (entry.first == "normalised")
				{
					normalised = utils::StringUtils::parseBool(value);
				}
			}

			if (component == mesh::Vertex::Component::Unused)
			{
				string errMsg = "Error loading " + mFilepath + ".  Invalid (or absent) component specified for MeshSpecification.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (datatype == mesh::Vertex::DataType::None)
			{
				string errMsg = "Error loading " + mFilepath + ".  Invalid (or absent) datatype specified for MeshSpecification.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			layout->createAttribute(component, datatype, normalised);
		}

		void MeshSpecificationParser::parseMeshSpecificationBuffer(mpp::data::StructuredData const& data, mesh::MeshSpecification& meshSpec)
		{
			auto buffer = meshSpec.createVertexBufferAttributeLayout(meshSpec.getStorageType() == mesh::VertexBufferStorageType::Static);

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "Channel")
				{
					parseMeshSpecificationBufferChannel(entry.second, buffer);
				}
			}
		}

		mesh::Primitive::Type MeshSpecificationParser::parseMeshSpecificationPrimitive(string const& value)
		{
			auto it = mMeshSpecificationPrimitive.find(value);

			if (it != mMeshSpecificationPrimitive.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + mFilepath + ".  Unknown/unsupported primitive type '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		mesh::VertexBufferStorageType MeshSpecificationParser::parseMeshSpecificationStorage(string const& value)
		{
			auto it = mMeshSpecificationStorage.find(value);

			if (it != mMeshSpecificationStorage.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + mFilepath + ".  Unknown/unsupported storage type '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		mesh::MeshSpecification MeshSpecificationParser::parse(mpp::data::StructuredData const& data)
		{
			if (data.getName() != "MeshSpecification")
			{
				string errMsg = "Error loading " + mFilepath + ".  MeshSpecification element is not 'MeshSpecification'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			mesh::MeshSpecification meshSpec;

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "primitive")
				{
					meshSpec.setPrimitiveType(parseMeshSpecificationPrimitive(value));
				}
				else if (entry.first == "indexed")
				{
					meshSpec.setIndexedVertices(utils::StringUtils::parseBool(value));
				}
				else if (entry.first == "storage")
				{
					meshSpec.setStorageType(parseMeshSpecificationStorage(value));
				}
				else if (entry.first == "Buffer")
				{
					parseMeshSpecificationBuffer(entry.second, meshSpec);
				}
			}

			return meshSpec;
		}
	}
}