#pragma once

#include <map>
#include <string>

#include "mpp/ResourceStream.h"

namespace mpp
{
	enum class RawShaderStage
	{
		Vertex,
		Fragment,
		Compute
	};

	// Source for the raw-GLSL program family. Unlike ProgramStream these sources
	// carry no @Token markup, no mesh::MeshSpecification and no generated
	// _mpp_vs_in_* attribute plumbing: what is set here is what the driver
	// compiles, once the declared #defines have been injected.
	class _MPPAPI RawShaderStream : public ResourceStream
	{
	protected:

		std::map<RawShaderStage, std::string> mSources;

		std::map<std::string, std::string> mDefines;

		// Programmatic sources are supplied before declaration, so there is
		// nothing to load. A file-backed stream overrides this.
		void loadImpl() override {}

	public:

		RawShaderStream(ResourceManager* resourceMgr, std::string const& type);

		void setSource(RawShaderStage stage, std::string source);

		std::string const& getSource(RawShaderStage stage) const;

		bool hasSource(RawShaderStage stage) const;

		// Injected immediately after the #version directive, in name order, so a
		// program can be specialised without editing its source.
		void setDefine(std::string const& name, std::string const& value = "1");

		std::map<std::string, std::string> const& getDefines() const;
	};

	class _MPPAPI ComputeProgramStream : public RawShaderStream
	{
	public:

		explicit ComputeProgramStream(ResourceManager* resourceMgr);
	};

	class _MPPAPI ParticleDrawProgramStream : public RawShaderStream
	{
	public:

		explicit ParticleDrawProgramStream(ResourceManager* resourceMgr);
	};
}
