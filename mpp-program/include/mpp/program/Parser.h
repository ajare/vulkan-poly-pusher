#pragma once

#include <string>
#include <set>
#include <map>
#include <algorithm>

#include <mpp/mesh/MeshSpecification.h>

#include "Config.h"
#include "ShaderStage.h"

namespace mpp
{
	namespace program
	{

		class _MPPPROGRAMAPI Parser
		{
			enum class VariableType
			{
				InAttribute,
				OutAttribute,
				Uniform,
				Texture
			};

		private:

			std::string mName;

			ShaderStage mStages[(int)ShaderStage::Type::NumStages];

			mesh::MeshSpecification mSpecification;

			std::vector<std::string> mErrors;

			std::vector<std::string> mWarnings;

		private:

			void parseSource(ShaderStage::Type stageType);

			void parseUniformUsage(ShaderStage::Type stageType);

			void parseTextureUsage(ShaderStage::Type stageType);

			void setInAttributesToMeshSpecification(ShaderStage::Type stageType);

			void setInAttributesToPreviousStage(ShaderStage::Type stageType);

			void setOutAttributesToUsage(ShaderStage::Type stageType);

			void parseInAttributeUsage(ShaderStage::Type stageType);

			bool evaluateShaderDirective(std::string const& expression, std::set<std::string> const& attribs);
				
			bool processShaderLine(std::string const& lineFragment, std::set<std::string> const& attribs, bool prev);

			void processConditionals(ShaderStage::Type stageType, std::set<std::string> const& attribs);

			void generateShader(ShaderStage::Type stageType);

			std::string stripComments(std::string const& src);

			std::string replaceCasts(ShaderStage::Type stageType, std::string const& src);

			std::string replaceUniformDeclaration(ShaderStage::Type stageType, std::string const& decl);

			std::string replaceUniformUsage(ShaderStage::Type stageType, std::string const& usage);

			std::string replaceTextureDeclaration(ShaderStage::Type stageType, std::string const& decl);

			std::string replaceTextureUsage(ShaderStage::Type stageType, std::string const& usage);

			std::vector<std::string> splitSourceIntoLines(std::string const& src);

			void addError(ShaderStage::Type stageType, std::string const& error);

			void addWarning(ShaderStage::Type stageType, std::string const& error);

		public:

			Parser();

			explicit Parser(std::string const& name);

			std::string const& getName() const;

			void setVertexSource(std::string const& src);

			void setGeometrySource(std::string const& src);

			void setFragmentSource(std::string const& src);

			void setMeshSpecification(mesh::MeshSpecification const& spec);

			void build(std::set<std::string> const& attribs);

			ShaderStage const& getStage(uint32_t index) const;

			mesh::MeshSpecification const& getMeshSpecification() const;

			std::string const& getInputVertexSource() const;

			std::string const& getInputGeometrySource() const;

			std::string const& getInputFragmentSource() const;

			std::string const& getGeneratedVertexSource() const;

			std::string const& getGeneratedGeometrySource() const;

			std::string const& getGeneratedFragmentSource() const;

			std::vector<Attribute> getInAttributes() const;

			std::vector<std::string> getUniforms() const;

			std::vector<std::string> getTextures() const;

			std::vector<std::string> const& getErrors() const;

			std::vector<std::string> const& getWarnings() const;
		};

	}
}