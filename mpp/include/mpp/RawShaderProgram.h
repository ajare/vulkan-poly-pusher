#pragma once

#include <map>
#include <string>
#include <vector>

#include "mpp/RawShaderStream.h"
#include "mpp/Resource.h"

namespace mpp
{
	// The minimal raw-GLSL program resource. Program compiles vertex and fragment
	// stages only and is bound to mesh::MeshSpecification, @Token markup
	// substitution and generated _mpp_vs_in_* attribute plumbing -- none of which
	// applies to a compute kernel or to an attribute-less particle draw. This is
	// what those two share instead: #define injection, compile and link with the
	// engine's error reporting, and ResourceManager lifetime and naming.
	class _MPPAPI RawShaderProgram : public Resource
	{
		std::map<RawShaderStage, std::string> mSources;

		std::map<std::string, std::string> mDefines;

		std::map<std::string, int> mUniformLocations;

	private:

		// Returns the source with every declared #define injected immediately
		// after the #version directive.
		std::string specialise(RawShaderStage stage) const;

		uint32_t compileStage(RawShaderStage stage) const;

	protected:

		RawShaderProgram(std::string const& name, std::string const& type, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		void createImpl() override;

		void destroyImpl() override;

		void unloadImpl() override;

		bool hasSource(RawShaderStage stage) const;

		// Compiles each supplied stage, links them, and adopts the linked program
		// as this resource's id. Nothing is adopted unless every step succeeded.
		void link(std::vector<RawShaderStage> const& stages);

	public:

		std::map<std::string, std::string> const& getDefines() const;

		void use();

		int getUniformLocation(std::string const& name);

		void setUniform(std::string const& name, int32_t value);

		void setUniform(std::string const& name, uint32_t value);

		void setUniform(std::string const& name, float value);
	};
}
