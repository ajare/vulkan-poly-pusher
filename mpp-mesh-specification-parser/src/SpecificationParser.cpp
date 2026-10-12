#include <format>
#include <stdexcept>
#include <stack>

#include "utils/StringUtils.h"
#include "utils/XmlReader.h"

#include "mpp/mesh-specification-parser/SpecificationParser.h"

namespace mpp
{
	namespace mesh_specification_parser
	{

		using namespace std;

		SpecificationParser::SpecificationParser(string const& filename)
			: mFilename(filename)
		{
			mComponentTypes["position2"] = mpp::mesh::Vertex::Component::Position2;
			mComponentTypes["position3"] = mpp::mesh::Vertex::Component::Position3;
			mComponentTypes["position4"] = mpp::mesh::Vertex::Component::Position4;
			mComponentTypes["normal3"] = mpp::mesh::Vertex::Component::Normal3;
			mComponentTypes["normal4"] = mpp::mesh::Vertex::Component::Normal4;
			mComponentTypes["texcoord2"] = mpp::mesh::Vertex::Component::TexCoord2;
			mComponentTypes["texcoord3"] = mpp::mesh::Vertex::Component::TexCoord3;
			mComponentTypes["texcoord4"] = mpp::mesh::Vertex::Component::TexCoord4;
			mComponentTypes["colour1"] = mpp::mesh::Vertex::Component::Colour1;
			mComponentTypes["colour3"] = mpp::mesh::Vertex::Component::Colour3;
			mComponentTypes["colour4"] = mpp::mesh::Vertex::Component::Colour4;
			mComponentTypes["user1"] = mpp::mesh::Vertex::Component::UserDefined1;
			mComponentTypes["user2"] = mpp::mesh::Vertex::Component::UserDefined2;
			mComponentTypes["user3"] = mpp::mesh::Vertex::Component::UserDefined3;
			mComponentTypes["user4"] = mpp::mesh::Vertex::Component::UserDefined4;

			mDataTypes["float32"] = mpp::mesh::Vertex::DataType::Float;
			mDataTypes["float16"] = mpp::mesh::Vertex::DataType::HalfFloat;
			mDataTypes["uint8_t"] = mpp::mesh::Vertex::DataType::UnsignedByte;
			mDataTypes["uint16_t"] = mpp::mesh::Vertex::DataType::UnsignedShort;
		}

		map<string, mesh::MaterialInformation> SpecificationParser::parseMaterialInformation()
		{
			map<string, mesh::MaterialInformation> minfos;

			utils::XmlReader* reader = utils::XmlReader::fromFile(mFilename);

			auto materialNode = reader->getNode("Specification/Materials/Material");
			do
			{
				string name = materialNode->getAttribute("name");

				mesh::MaterialInformation mi(name);

				// Material stream parsing is performed by ModelspecStream.

				/*
				// Parse program
				auto programNode = materialNode->getChild("Program");

				// Position type: 2d or 3d
				string positionType = programNode->getAttribute("positionType");
				utils::StringUtils::toLower(positionType);

				if (positionType != "2d" && positionType != "3d")
				{
					string errMsg = "Invalid position type: " + programNode->getAttribute("positionType");
					throw runtime_error(errMsg);
				}

				mi.setPositionType(positionType == "2d" ? mesh::MaterialInformation::PositionType::p2D : mesh::MaterialInformation::PositionType::p3D);

				auto shaderNode = programNode->getChild("Shaders")->getChild("Shader");
				if (!shaderNode)
				{
					string errMsg = "No shaders specified";
					throw runtime_error(errMsg);
				}

				do
				{
					auto shaderTypeStr = shaderNode->getAttribute("type");
					utils::StringUtils::toLower(shaderTypeStr);

					auto shaderName = shaderNode->getValue();

					mesh::MaterialInformation::Shader::Type shaderType;
					if (shaderTypeStr == "vertex")
					{
						shaderType = mesh::MaterialInformation::Shader::Type::Vertex;
					}
					else if (shaderTypeStr == "geometry")
					{
						shaderType = mesh::MaterialInformation::Shader::Type::Geometry;
					}
					else if (shaderTypeStr == "fragment")
					{
						shaderType = mesh::MaterialInformation::Shader::Type::Fragment;
					}
					else
					{
						string errMsg = "Invalid shader stage: " + shaderNode->getAttribute("type");
						throw runtime_error(errMsg);
					}

					mi.addShader(shaderType, shaderName);
				} while (shaderNode->next());

				// Parse textures
				auto texturesNode = materialNode->getOptionalChild("Textures");
				if (texturesNode)
				{
					auto textureNode = texturesNode->getOptionalChild("Texture");
					if (textureNode)
					{
						do
						{
							auto binding = textureNode->getAttribute("binding");

							bool isResource;
							string resource;

							if (textureNode->getOptionalAttribute("filename", resource))
							{
								isResource = false;
							}
							else if (textureNode->getOptionalAttribute("resource", resource))
							{
								isResource = true;
							}
							else
							{
								string errMsg = "Either 'filename' or 'resource' must be specified for a texture";
								throw runtime_error(errMsg);
							}

							mi.addTexture(isResource, binding, resource);
						} while (textureNode->next());
					}

					auto uniformsNode = materialNode->getOptionalChild("Uniforms");
					if (uniformsNode)
					{
						auto uniformNode = uniformsNode->getOptionalChild("Uniform");
						if (uniformNode)
						{
							do
							{
								string uniformName = uniformNode->getAttribute("name");
								string uniformType = uniformNode->getAttribute("type");
								string uniformValue = uniformNode->getAttribute("value");

								if (uniformType != "int" && uniformType != "uint" && uniformType != "float")
								{
									string errMsg = std::format(
										"Found {}-type uniform '{}' while parsing material.  Only int/uint/float types are supported.",
										uniformType, uniformName);

									throw runtime_error(errMsg);
								}

								// Parse value: from [1, 5) components
								vector<string> tokens = utils::StringUtils::split(uniformValue, ",");

								size_t componentCount = tokens.size();

								if (componentCount < 1 || componentCount > 4)
								{
									string errMsg = std::format(
										"Found {}-dimension uniform '{}' while parsing material '{}'.  Only 1-4 dimensional types are supported.",
										componentCount, uniformName);

									throw runtime_error(errMsg);
								}

								if (uniformType == "int")
								{
									int32_t values[4];
									for (size_t i = 0; i < componentCount; ++i)
									{
										values[i] = utils::StringUtils::parseInt(tokens[i]);
									}

									mi.addUniform(uniformName, componentCount, values);
								}
								else if (uniformType == "uint")
								{
									uint32_t values[4];
									for (size_t i = 0; i < componentCount; ++i)
									{
										values[i] = utils::StringUtils::parseUInt(tokens[i]);
									}

									mi.addUniform(uniformName, componentCount, values);
								}
								else if (uniformType == "float")
								{
									float values[4];
									for (size_t i = 0; i < componentCount; ++i)
									{
										values[i] = utils::StringUtils::parseFloat(tokens[i]);
									}

									mi.addUniform(uniformName, componentCount, values);
								}
							} while (uniformNode->next());
						}
					}
				}
				*/

				// Add material
				minfos[name] = mi;
			} while (materialNode->next());

			delete reader;
	
			return minfos;
		}

		mpp::mesh::MeshSpecification SpecificationParser::parseMeshSpecification(uint32_t& maxVerticesPerMesh)
		{
			mpp::mesh::MeshSpecification meshSpec;
			utils::XmlReader* reader = utils::XmlReader::fromFile(mFilename);

			auto buffersNode = reader->getNode("Specification/Buffers");

			// Is indexed?
			string indexed = utils::StringUtils::toLower(buffersNode->getAttribute("indexed"));
			bool isIndexed = indexed == "true" || indexed == "yes";

			if (isIndexed)
			{
				meshSpec.setIndexedVertices(true);
			}

			// Get primitive type
			string primitive = utils::StringUtils::toLower(buffersNode->getAttribute("primitive"));

			if (primitive == "points")
			{
				meshSpec.setPrimitiveType(mpp::mesh::Primitive::Type::Points);
			}
			else if (primitive == "lines")
			{
				meshSpec.setPrimitiveType(mpp::mesh::Primitive::Type::Lines);
			}
			else if (primitive == "triangles")
			{
				meshSpec.setPrimitiveType(mpp::mesh::Primitive::Type::Triangles);
			}
			else
			{
				string errMsg = "Invalid primitive: " + primitive;
				throw runtime_error(errMsg);
			}

			// Get storage type
			string storage = utils::StringUtils::toLower(buffersNode->getAttribute("storage"));

			if (storage == "static")
			{
				meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
			}
			else if (storage == "dynamic")
			{
				meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Dynamic);
			}
			else
			{
				string errMsg = "Invalid storage type: " + storage;
				throw runtime_error(errMsg);
			}

			// Get split size
			string splitSize;
			maxVerticesPerMesh = (uint32_t)-1;
			if (buffersNode->getOptionalAttribute("splitSize", splitSize))
			{
				maxVerticesPerMesh = utils::StringUtils::parseUInt(splitSize);
			}

			auto bufferNode = buffersNode->getChild("Buffer");
			uint32_t attribIndex = 0;
			if (bufferNode)
			{
				do
				{
					mpp::mesh::VertexBufferAttributeLayout* attribLayout = meshSpec.createVertexBufferAttributeLayout(false);

					auto channelNode = bufferNode->getChild("Channel");

					int channelId = 0;
					if (channelNode)
					{
						do
						{
							string data = utils::StringUtils::toLower(channelNode->getAttribute("data"));
							string type = utils::StringUtils::toLower(channelNode->getAttribute("type"));

							if (mComponentTypes.find(data) == mComponentTypes.end())
							{
								string errMsg = "Invalid data: " + data;
								throw runtime_error(errMsg);
							}
							if (mDataTypes.find(type) == mDataTypes.end())
							{
								string errMsg = "Invalid type: " + type;
								throw runtime_error(errMsg);
							}

							string normalised;
							bool isNormalised;

							if (channelNode->getOptionalAttribute("normalised", normalised))
							{
								utils::StringUtils::toLower(normalised);
								isNormalised = normalised == "true" || normalised == "yes";
							}
							else
							{
								isNormalised = false;
							}

							attribLayout->createAttribute(mComponentTypes[data], mDataTypes[type], isNormalised);
							channelId++;
						} while (channelNode->next());
					}
				} while (bufferNode->next());
			}

			delete reader;
			return meshSpec;
		}
	}
}