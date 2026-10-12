#include <format>
#include "utils/FileSystem.h"

#include "mpp/DefaultShaders.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticTextureStream.h"

#include "utils/StringUtils.h"

#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileBasicMaterialStream::FileBasicMaterialStream(ResourceManager* resourceMgr, string const& filepath, bool relativisePaths)
			: BasicMaterialStream(resourceMgr)
			, FileStream(filepath)
			, mUseSpecifiedMeshSpec(false)
			, mRelativisePaths(relativisePaths)
		{
		}

		FileBasicMaterialStream::FileBasicMaterialStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths)
			: BasicMaterialStream(resourceMgr)
			, FileStream(filepath, data)
			, mUseSpecifiedMeshSpec(false)
			, mRelativisePaths(relativisePaths)
		{
		}

		FileBasicMaterialStream::FileBasicMaterialStream(ResourceManager* resourceMgr, string const& filepath, mesh::MeshSpecification const& meshSpec, bool relativisePaths)
			: BasicMaterialStream(resourceMgr)
			, FileStream(filepath)
			, mUseSpecifiedMeshSpec(true)
			, mMeshSpec(meshSpec)
			, mRelativisePaths(relativisePaths)
		{
		}

		FileBasicMaterialStream::FileBasicMaterialStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data, mesh::MeshSpecification const& meshSpec, bool relativisePaths)
			: BasicMaterialStream(resourceMgr)
			, FileStream(filepath, data)
			, mUseSpecifiedMeshSpec(true)
			, mMeshSpec(meshSpec)
			, mRelativisePaths(relativisePaths)
		{
		}

		void FileBasicMaterialStream::parseForChildResourceStreams(mpp::data::StructuredData const& data, string const& filepath, bool useSpecifiedMesh, mesh::MeshSpecification const* meshSpec)
		{
			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "Program")
				{
					// This can either be a reference to another resource, or an actual program definition.
					auto const& programEntry = entry.second;
					if (programEntry.hasEntry("Resource"))
					{
						// Create FileProgramStream
						if (useSpecifiedMesh)
						{
							auto fpStream = new FileProgramStream(getResourceMgr(), filepath, programEntry.getEntry("Resource"), *meshSpec, mRelativisePaths);
							addChild("Program", ResourceStreamPtr(fpStream));
						}
						else
						{
							auto fpStream = new FileProgramStream(getResourceMgr(), filepath, programEntry.getEntry("Resource"), mRelativisePaths);
							addChild("Program", ResourceStreamPtr(fpStream));
						}
					}
				}
				else if (entry.first == "Textures")
				{
					int textureId = 0;
					auto const& textures = it->second;
					for (auto tit = textures.begin(); tit != textures.end(); ++tit, ++textureId)
					{
						auto const& entry = *tit;

						if (entry.first == "Texture")
						{
							auto const& textureEntry = entry.second;

							if (textureEntry.hasEntry("Resource"))
							{
								// Set texture options: peak sampler name
								auto samplerEntry = textureEntry.getEntry("Variable");
								auto samplerName = samplerEntry.getValue();

								// Create FileTextureStream
								auto ftStream = new FileTextureStream(getResourceMgr(), filepath, textureEntry.getEntry("Resource"), mRelativisePaths);
								addChild("Textures/" + samplerName, ResourceStreamPtr(ftStream));
							}
						}
					}
				}
			}
		}

		void FileBasicMaterialStream::createChildResourceStreamsImpl()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'Material'
			auto rootName = data.getName();

			if (rootName != "BasicMaterial" && rootName != "Material" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'BasicMaterial' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each material variant into a separate resource.", __LINE__, __FILE__, __func__);
			parseForChildResourceStreams(data, getFilepath(), mUseSpecifiedMeshSpec, &mMeshSpec);
		}

		void FileBasicMaterialStream::parseUniformVectorType(string const& name, string const& type, size_t count, string const& value, UniformCollection &uniforms, string const& filepath)
		{
			auto values = utils::StringUtils::split(value, " ,");
			if (values.size() != count)
			{
				string errMsg = std::format("Error loading {}.  '{}' specified for uniform '{}'  but {} values found.",
					filepath, type, name, values.size());
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (type == "vec2")
			{
				uniforms.setUniform(name, glm::vec2(
					utils::StringUtils::parseFloat(values[0]),
					utils::StringUtils::parseFloat(values[1])));
			}
			else if (type == "vec3")
			{
				uniforms.setUniform(name, glm::vec3(
					utils::StringUtils::parseFloat(values[0]),
					utils::StringUtils::parseFloat(values[1]),
					utils::StringUtils::parseFloat(values[2])));
			}
			else if (type == "vec4")
			{
				uniforms.setUniform(name, glm::vec4(
					utils::StringUtils::parseFloat(values[0]),
					utils::StringUtils::parseFloat(values[1]),
					utils::StringUtils::parseFloat(values[2]),
					utils::StringUtils::parseFloat(values[3])));
			}
		}

		void FileBasicMaterialStream::parseUniformMatrixType(string const& name, string const& type, size_t count, string const& value, UniformCollection &uniforms, string const& filepath)
		{
			auto values = utils::StringUtils::split(value, " ,");
			if (values.size() != count)
			{
				string errMsg = std::format("Error loading {}. '{}' specified for uniform {} but {} values found.", filepath, type, name, values.size());
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			auto fvalues = new float[count];
			for (size_t i = 0; i < count; ++i)
			{
				fvalues[i] = utils::StringUtils::parseFloat(values[i]);
			}

			uniforms.setUniform(name, count, 1, fvalues);
			delete[] fvalues;
		}

		void FileBasicMaterialStream::parseUniform(mpp::data::StructuredData const& data, UniformCollection& uniforms, string const& filepath)
		{
			string name, type, value;
			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;

				if (entry.first == "name")
				{
					name = entry.second.getValue();
				}
				else if (entry.first == "type")
				{
					type = entry.second.getValue();
				}
				else if (entry.first == "value")
				{
					value = entry.second.getValue();
				}
			}

			if (name == "")
			{
				string errMsg = "Error loading " + filepath + ".  'name' not specified for uniform '" + name + "'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (type == "")
			{
				string errMsg = "Error loading " + filepath + ".  'type' not specified for uniform '" + name + "'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (value == "")
			{
				string errMsg = "Error loading " + filepath + ".  'value' not specified for uniform '" + name + "'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (type == "int")
			{
				uniforms.setUniform(name, utils::StringUtils::parseInt(value));
			}
			else if (type == "uint")
			{
				uniforms.setUniform(name, utils::StringUtils::parseUInt(value));
			}
			else if (type == "float")
			{
				uniforms.setUniform(name, utils::StringUtils::parseFloat(value));
			}
			else if (type == "vec2")
			{
				parseUniformVectorType(name, type, 2, value, uniforms, filepath);
			}
			else if (type == "vec3")
			{
				parseUniformVectorType(name, type, 3, value, uniforms, filepath);
			}
			else if (type == "vec4")
			{
				parseUniformVectorType(name, type, 4, value, uniforms, filepath);
			}
			else if (type == "mat2")
			{
				parseUniformMatrixType(name, type, 2 * 2, value, uniforms, filepath);
			}
			else if (type == "mat2x3")
			{
				parseUniformMatrixType(name, type, 2 * 3, value, uniforms, filepath);
			}
			else if (type == "mat2x4")
			{
				parseUniformMatrixType(name, type, 2 * 4, value, uniforms, filepath);
			}
			else if (type == "mat3x2")
			{
				parseUniformMatrixType(name, type, 3 * 2, value, uniforms, filepath);
			}
			else if (type == "mat3")
			{
				parseUniformMatrixType(name, type, 3 * 3, value, uniforms, filepath);
			}
			else if (type == "mat3x4")
			{
				parseUniformMatrixType(name, type, 3 * 4, value, uniforms, filepath);
			}
			else if (type == "mat4x2")
			{
				parseUniformMatrixType(name, type, 4 * 2, value, uniforms, filepath);
			}
			else if (type == "mat4x3")
			{
				parseUniformMatrixType(name, type, 4 * 3, value, uniforms, filepath);
			}
			else if (type == "mat4")
			{
				parseUniformMatrixType(name, type, 4 * 4, value, uniforms, filepath);
			}
		}

		pair<string, BasicMaterialSpecification> FileBasicMaterialStream::parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, string const& filepath)
		{
			string name;
			BasicMaterialSpecification qs;

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "name")
				{
					name = entry.second.getValue();
				}
				else if (entry.first == "Program")
				{
					// This can either be a reference to another resource, or an actual program definition.
					auto const& programEntry = entry.second;
					if (programEntry.hasEntry("Resource"))
					{
						qs.program.resourceExists = true;
						qs.program.isChild = true;
						qs.program.existingResource = "Program";
					}
					else if (programEntry.hasEntry("Ref"))
					{
						auto refName = programEntry.getEntry("Resource").getValue();

						// Set program options
						qs.program.resourceExists = true;
						qs.program.isChild = false;
						qs.program.existingResource = refName;
					}
					else
					{
						string errMsg = "Error loading " + filepath + ".  Neither 'Resource' nor 'Ref' specified for material program.";
						THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
					}
				}
				else if (entry.first == "Textures")
				{
					auto const& textures = it->second;
					for (auto const& entry: textures)
					{
						if (entry.first == "Texture")
						{
							auto const& textureEntry = entry.second;

							// Set texture options: peak sampler name
							auto samplerEntry = textureEntry.getEntry("Variable");
							auto samplerName = samplerEntry.getValue();

							// This can either be a reference to another resource, or an actual texture definition.
							BasicMaterialSpecification::TextureOptions textureOptions;

							textureOptions.resourceExists = true;
							textureOptions.sampler = samplerName;

							if (textureEntry.hasEntry("Resource"))
							{
								textureOptions.isChild = true;
								textureOptions.existingResource = "Textures/" + samplerName;
							}
							else if (textureEntry.hasEntry("Ref"))
							{
								textureOptions.isChild = false;
								textureOptions.existingResource = textureEntry.getEntry("Ref").getValue();
							}
						
							qs.textures.push_back(textureOptions);
						}
					}
				}
				else if (entry.first == "Uniforms")
				{
					auto const& uniforms = it->second;
					for (auto uit = uniforms.begin(); uit != uniforms.end(); ++uit)
					{
						auto const& entry = *uit;

						if (entry.first == "Uniform")
						{
							// Parse uniform
							parseUniform(entry.second, qs.uniforms, filepath);
						}
					}
				}
			}

			return make_pair(name, qs);
		}

		void FileBasicMaterialStream::loadImpl()
		{
			auto const& data = getStructuredData();

			// Parse data. Root element must identify the concrete basic resource.
			auto rootName = data.getName();

			if (rootName != "BasicMaterial" && rootName != "Material" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ". Root element is neither 'BasicMaterial' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each material variant into a separate resource.", __LINE__, __FILE__, __func__);
			auto definition = parseDefinition(data, getResourceMgr(), getFilepath());
			mName = definition.first;
			mSpecification = definition.second;
			if (mUseSpecifiedMeshSpec) mSpecification.program.spec = mMeshSpec;
		}
	}
}