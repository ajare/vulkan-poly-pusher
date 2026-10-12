#include "mpp/DefaultShaders.h"
#include "mpp/String.h"

#include "utils/StringUtils.h"

#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/MeshSpecificationParser.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileProgramStream::FileProgramStream(ResourceManager* resourceMgr, string const& filepath, bool relativisePaths)
			: ProgramStream(resourceMgr)
			, FileStream(filepath)
			, mMeshSpecRequired(true)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		FileProgramStream::FileProgramStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths)
			: ProgramStream(resourceMgr)
			, FileStream(filepath, data)
			, mMeshSpecRequired(true)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		FileProgramStream::FileProgramStream(ResourceManager* resourceMgr, string const& filepath, mesh::MeshSpecification const& meshSpec, bool relativisePaths)
			: ProgramStream(resourceMgr)
			, FileStream(filepath)
			, mMeshSpecRequired(false)
			, mMeshSpecification(meshSpec)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		FileProgramStream::FileProgramStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data, mesh::MeshSpecification const& meshSpec, bool relativisePaths)
			: ProgramStream(resourceMgr)
			, FileStream(filepath, data)
			, mMeshSpecRequired(false)
			, mMeshSpecification(meshSpec)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		void FileProgramStream::setup()
		{
			// Primitives
			mMeshSpecificationPrimitive["POINTS"] = mesh::Primitive::Type::Points;
			mMeshSpecificationPrimitive["LINES"] = mesh::Primitive::Type::Lines;
			mMeshSpecificationPrimitive["TRIANGLES"] = mesh::Primitive::Type::Triangles;

			// Storage
			mMeshSpecificationStorage["STATIC"] = mesh::VertexBufferStorageType::Static;
			mMeshSpecificationStorage["DYNAMIC"] = mesh::VertexBufferStorageType::Dynamic;

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
			mComponentTypes["USER1"] = mpp::mesh::Vertex::Component::UserDefined1;
			mComponentTypes["USER2"] = mpp::mesh::Vertex::Component::UserDefined2;
			mComponentTypes["USER3"] = mpp::mesh::Vertex::Component::UserDefined3;
			mComponentTypes["USER4"] = mpp::mesh::Vertex::Component::UserDefined4;

			mDataTypes["FLOAT16"] = mpp::mesh::Vertex::DataType::HalfFloat;
			mDataTypes["FLOAT32"] = mpp::mesh::Vertex::DataType::Float;
			mDataTypes["UINT8"] = mpp::mesh::Vertex::DataType::UnsignedByte;
			mDataTypes["UINT16"] = mpp::mesh::Vertex::DataType::UnsignedShort;
		}

		FileProgramStream::Shader FileProgramStream::parseShader(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, string const& filepath)
		{
			Shader shader;

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = entry.second.getValue();

				if (entry.first == "file")
				{
					shader.type = Shader::Type::File;
					shader.source = value;
					shader.data = readTextFile(value, filepath);
				}
				else if (entry.first == "resource")
				{
					shader.type = Shader::Type::Resource;
					shader.source = value;

					if (resourceMgr)
					{
						THROW_MPP_RESOURCE_PARSERS_NOTIMP("Engine shader resource lookup in the CPU-only subset", __LINE__, __FILE__, __func__);
					}
				}
			}

			if (shader.type == Shader::Type::Default)
			{
				string errMsg = "Error loading " + filepath + ".  Neither 'file' nor 'resource' specified for Shader.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			return shader;
		}

		pair<std::string, FileProgramStream::Definition> FileProgramStream::parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, string const& filepath, bool meshSpecRequired, mesh::MeshSpecification const* mainMeshSpec, bool relativisePaths)
		{
			string name;

			// Read settings required to create a Parser
			Shader vertexShader, geometryShader, fragmentShader;

			string positionType;
			mesh::MeshSpecification meshSpec;
			size_t numTextures{ 0 };

			bool useDiffuse{ false };
			bool useColours{ false };
			bool isAtlas{ false };
			bool useRotation{ false };
		
			enum class PrimitiveAttrib
			{
				Unspecified,
				Triangles,
				Lines,
				Points
			};

			PrimitiveAttrib primitiveAttib{ PrimitiveAttrib::Unspecified };

			bool parsedMeshSpec{ false };
			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "name")
				{
					name = entry.second.getValue();
				}
				else if (entry.first == "positionType")
				{
					positionType = value;
				}
				else if (entry.first == "textures")
				{
					numTextures = utils::StringUtils::parseUInt(value);
				}
				else if (entry.first == "primitive")
				{
					if (primitiveAttib != PrimitiveAttrib::Unspecified)
					{
						string errMsg = "Error loading " + filepath + ".  'primitive' cannot be specified more than once for program.";
						THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
					}
					
					if (value == "POINTS")
					{
						primitiveAttib = PrimitiveAttrib::Points;
					}
					else if (value == "LINES")
					{
						primitiveAttib = PrimitiveAttrib::Lines;
					}
					else if (value == "TRIANGLES")
					{
						primitiveAttib = PrimitiveAttrib::Triangles;
					}
				}
				else if (entry.first == "diffuse")
				{
					useDiffuse = utils::StringUtils::parseBool(value);
				}
				else if (entry.first == "colours")
				{
					useColours = utils::StringUtils::parseBool(value);
				}
				else if (entry.first == "atlas")
				{
					isAtlas = utils::StringUtils::parseBool(value);
				}
				else if (entry.first == "rotation")
				{
					useRotation = utils::StringUtils::parseBool(value);
				}
				else if (entry.first == "VertexShader")
				{
					vertexShader = parseShader(entry.second, resourceMgr, filepath);
				}
				else if (entry.first == "GeometryShader")
				{
					THROW_MPP_RESOURCE_PARSERS_NOTIMP("Geometry shaders", __LINE__, __FILE__, __func__);
				}
				else if (entry.first == "FragmentShader")
				{
					fragmentShader = parseShader(entry.second, resourceMgr, filepath);
				}
				else if (entry.first == "MeshSpecification")
				{
					MeshSpecificationParser parser(filepath);
					meshSpec = parser.parse(entry.second);
					parsedMeshSpec = true;
				}
			}

			if (!parsedMeshSpec && meshSpecRequired)
			{
				string errMsg = "Error loading " + filepath + ".  MeshSpecification not specified for program.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (positionType != "2D" && positionType != "3D")
			{
				string errMsg = "Error loading " + filepath + ".  Invalid (or absent) position type specified for program.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			// Set attribs
			set<string> attribs;

			if (numTextures > 0)
			{
				attribs.insert("Texture");
			}
			if (primitiveAttib != PrimitiveAttrib::Unspecified)
			{
				if (primitiveAttib == PrimitiveAttrib::Points)
				{
					attribs.insert("Points");
				}
				else if (primitiveAttib == PrimitiveAttrib::Lines)
				{
					attribs.insert("Lines");
				}
				else if (primitiveAttib == PrimitiveAttrib::Triangles)
				{
					attribs.insert("Triangles");
				}
			}
			if (useColours)
			{
				attribs.insert("Colours");
			}
			if (isAtlas)
			{
				attribs.insert("Atlas");
			}
			if (useRotation)
			{
				attribs.insert("Rotation");
			}

			Definition qs;
			qs.parser = make_shared<program::Parser>();
			qs.attribs = attribs;

			if (meshSpecRequired)
			{
				qs.parser->setMeshSpecification(meshSpec);
			}
			else
			{
				qs.parser->setMeshSpecification(*mainMeshSpec);
			}
			
			// Load source into parser
			if (vertexShader.type == Shader::Type::Default)
			{
				if (positionType == "2D")
				{
					qs.parser->setVertexSource(VertexShader2dTemplate);
				}
				else if (positionType == "3D")
				{
					qs.parser->setVertexSource(VertexShader3dTemplate);
				}
			}
			else
			{
				qs.parser->setVertexSource(vertexShader.data);
			}

			if (fragmentShader.type == Shader::Type::Default)
			{
				if (positionType == "2D")
				{
					qs.parser->setFragmentSource(FragmentShader2dTemplate);
				}
				else if (positionType == "3D")
				{
					qs.parser->setFragmentSource(FragmentShader3dTemplate);
				}
			}
			else
			{
				qs.parser->setFragmentSource(fragmentShader.data);
			}

			return make_pair(name, qs);
		}

		void FileProgramStream::loadImpl()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'Program'
			auto rootName = data.getName();

			if (rootName != "Program" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'Program' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each program variant into a separate resource.", __LINE__, __FILE__, __func__);
			auto definition = parseDefinition(data, getResourceMgr(), getFilepath(), mMeshSpecRequired, &mMeshSpecification).second;
			mParser = definition.parser;
			mAttribs = definition.attribs;
			ProgramStream::loadImpl();
		}
	}
}