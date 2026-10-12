#include "utils/StringUtils.h"

#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgramStream.h"

#include "mpp/resource-parsers/FilePostEffectMaterialStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/resource-parsers/MeshSpecificationParser.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{
		using namespace std;

		namespace
		{
			void parseUniform(mpp::data::StructuredData const& data, UniformCollection& uniforms, string const& filepath)
			{
				string name, type, value;
				for (auto const& entry : data)
				{
					if (entry.first == "name") name = entry.second.getValue();
					else if (entry.first == "type") type = entry.second.getValue();
					else if (entry.first == "value") value = entry.second.getValue();
				}
				if (name.empty() || type.empty() || value.empty())
					THROW_MPP_RESOURCE_PARSERS("Error loading " + filepath + ". PostEffectMaterial uniform requires name, type, and value.", __LINE__, __FILE__, __func__);
				auto split = [&] { return utils::StringUtils::split(value, " ,"); };
				if (type == "int") uniforms.setUniform(name, utils::StringUtils::parseInt(value));
				else if (type == "float") uniforms.setUniform(name, utils::StringUtils::parseFloat(value));
				else if (type == "vec2") { auto v = split(); if (v.size() != 2) THROW_MPP_RESOURCE_PARSERS("vec2 uniform '" + name + "' requires two values.", __LINE__, __FILE__, __func__); uniforms.setUniform(name, glm::vec2(utils::StringUtils::parseFloat(v[0]), utils::StringUtils::parseFloat(v[1]))); }
				else if (type == "vec3") { auto v = split(); if (v.size() != 3) THROW_MPP_RESOURCE_PARSERS("vec3 uniform '" + name + "' requires three values.", __LINE__, __FILE__, __func__); uniforms.setUniform(name, glm::vec3(utils::StringUtils::parseFloat(v[0]), utils::StringUtils::parseFloat(v[1]), utils::StringUtils::parseFloat(v[2]))); }
				else if (type == "vec4") { auto v = split(); if (v.size() != 4) THROW_MPP_RESOURCE_PARSERS("vec4 uniform '" + name + "' requires four values.", __LINE__, __FILE__, __func__); uniforms.setUniform(name, glm::vec4(utils::StringUtils::parseFloat(v[0]), utils::StringUtils::parseFloat(v[1]), utils::StringUtils::parseFloat(v[2]), utils::StringUtils::parseFloat(v[3]))); }
				else THROW_MPP_RESOURCE_PARSERS("Unknown PostEffectMaterial uniform type '" + type + "'.", __LINE__, __FILE__, __func__);
			}
		}

		FilePostEffectMaterialStream::FilePostEffectMaterialStream(ResourceManager* resourceMgr, string const& filepath)
			: PostEffectMaterialStream(resourceMgr), FileStream(filepath)
		{
		}

		FilePostEffectMaterialStream::FilePostEffectMaterialStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data)
			: PostEffectMaterialStream(resourceMgr), FileStream(filepath, data)
		{
		}

		pair<string, PostEffectMaterialSpecification> FilePostEffectMaterialStream::parseDefinition(mpp::data::StructuredData const& data, string const& filepath)
		{
			string name;
			PostEffectMaterialSpecification spec;
			for (auto const& entry : data)
			{
				if (entry.first == "name")
				{
					name = entry.second.getValue();
				}
				else if (entry.first == "Program")
				{
					auto const& programEntry = entry.second;
					if (programEntry.hasEntry("Ref"))
					{
						spec.program.resourceExists = true;
						spec.program.isChild = false;
						spec.program.existingResource = programEntry.getEntry("Ref").getValue();
					}
					else if (programEntry.hasEntry("Resource"))
					{
						spec.program.resourceExists = true;
						spec.program.isChild = true;
						spec.program.existingResource = "Program";
					}
					else
					{
						THROW_MPP_RESOURCE_PARSERS("Error loading " + filepath + ". PostEffectMaterial Program requires Ref or Resource.", __LINE__, __FILE__, __func__);
					}
				}
				else if (entry.first == "SamplerSlots")
				{
					for (auto const& slot : entry.second)
						if (slot.first == "Slot") spec.samplerSlots.push_back(slot.second.getValue());
				}
				else if (entry.first == "Uniforms")
				{
					for (auto const& uniform : entry.second)
						if (uniform.first == "Uniform") parseUniform(uniform.second, spec.uniforms, filepath);
				}
			}
			if (name.empty()) THROW_MPP_RESOURCE_PARSERS("Error loading " + filepath + ". PostEffectMaterial requires a name.", __LINE__, __FILE__, __func__);
			return make_pair(name, spec);
		}

		void FilePostEffectMaterialStream::createChildResourceStreamsImpl()
		{
			auto const& data = getStructuredData();
			auto const& rootName = data.getName();
			if (rootName != "PostEffectMaterial" && rootName != "Resource")
				THROW_MPP_RESOURCE_PARSERS("Error loading " + getFilepath() + ". Root element is not 'PostEffectMaterial'.", __LINE__, __FILE__, __func__);
			if (!data.hasEntry("Program") || !data.getEntry("Program").hasEntry("Resource")) return;
			auto const& programEntry = data.getEntry("Program").getEntry("Resource");
			if (!data.hasEntry("MeshSpecification"))
				THROW_MPP_RESOURCE_PARSERS("Error loading " + getFilepath() + ". An embedded PostEffectMaterial Program requires a MeshSpecification matching the engine's fullscreen quad layout.", __LINE__, __FILE__, __func__);
			MeshSpecificationParser meshParser(getFilepath());
			auto meshSpec = meshParser.parse(data.getEntry("MeshSpecification"));
			auto fpStream = new FileProgramStream(getResourceMgr(), getFilepath(), programEntry, meshSpec);
			addChild("Program", ResourceStreamPtr(fpStream));
		}

		void FilePostEffectMaterialStream::loadImpl()
		{
			auto const& data = getStructuredData();
			auto definition = parseDefinition(data, getFilepath());
			mName = definition.first;
			mSpecification = definition.second;
		}
	}
}
