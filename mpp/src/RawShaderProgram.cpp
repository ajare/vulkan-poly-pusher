#include <algorithm>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"
#include "mpp/RawShaderProgram.h"
#include "mpp/RenderSystem.h"

using namespace std;

namespace mpp
{
	namespace
	{
		struct ShaderHandle
		{
			GLuint id{ 0 };
			ShaderHandle() = default;
			ShaderHandle(ShaderHandle const&) = delete;
			ShaderHandle& operator =(ShaderHandle const&) = delete;
			~ShaderHandle() { if (id != 0) glDeleteShader(id); }
			GLuint release() { auto result = id; id = 0; return result; }
		};

		struct ProgramHandle
		{
			GLuint id{ 0 };
			ProgramHandle() = default;
			ProgramHandle(ProgramHandle const&) = delete;
			ProgramHandle& operator =(ProgramHandle const&) = delete;
			~ProgramHandle() { if (id != 0) glDeleteProgram(id); }
			GLuint release() { auto result = id; id = 0; return result; }
		};

		GLenum glStage(RawShaderStage stage)
		{
			switch (stage)
			{
			case RawShaderStage::Vertex: return GL_VERTEX_SHADER;
			case RawShaderStage::Fragment: return GL_FRAGMENT_SHADER;
			case RawShaderStage::Compute: return GL_COMPUTE_SHADER;
			}
			return 0;
		}

		char const* stageName(RawShaderStage stage)
		{
			switch (stage)
			{
			case RawShaderStage::Vertex: return "vertex";
			case RawShaderStage::Fragment: return "fragment";
			case RawShaderStage::Compute: return "compute";
			}
			return "unknown";
		}
	}

	RawShaderProgram::RawShaderProgram(string const& name, string const& type, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, type, renderSystem, resourceMgr, resourceStream)
	{
	}

	void RawShaderProgram::createImpl()
	{
		auto* stream = dynamic_cast<RawShaderStream*>(getResourceStream().get());
		if (!stream)
		{
			THROW_MPP("Could not cast to type 'RawShaderStream'.", __LINE__, __FILE__, __func__);
		}

		mDefines = stream->getDefines();
		mSources.clear();
		for (auto stage : { RawShaderStage::Vertex, RawShaderStage::Fragment, RawShaderStage::Compute })
		{
			if (stream->hasSource(stage)) mSources[stage] = stream->getSource(stage);
		}
	}

	void RawShaderProgram::destroyImpl()
	{
		mSources.clear();
		mDefines.clear();
	}

	void RawShaderProgram::unloadImpl()
	{
		mUniformLocations.clear();

		GLuint id = getId();
		if (id != 0)
		{
			GL_CHECK(glDeleteProgram(id));
			setId(0);
		}
	}

	bool RawShaderProgram::hasSource(RawShaderStage stage) const
	{
		auto const found = mSources.find(stage);
		return found != mSources.end() && !found->second.empty();
	}

	map<string, string> const& RawShaderProgram::getDefines() const
	{
		return mDefines;
	}

	/*
	 * Build the source the driver actually sees.  Definitions have to follow the
	 * #version directive, which GLSL requires to be the first non-comment token.
	 *
	 */
	string RawShaderProgram::specialise(RawShaderStage stage) const
	{
		auto const& source = mSources.at(stage);
		if (mDefines.empty()) return source;

		string definitions;
		for (auto const& [name, value] : mDefines)
		{
			definitions += "#define " + name + " " + value + "\n";
		}

		auto const version = source.find("#version");
		if (version == string::npos) return definitions + source;

		auto const endOfLine = source.find('\n', version);
		if (endOfLine == string::npos) return source + "\n" + definitions;

		return source.substr(0, endOfLine + 1) + definitions + source.substr(endOfLine + 1);
	}

	uint32_t RawShaderProgram::compileStage(RawShaderStage stage) const
	{
		ShaderHandle shader;
		GL_CHECK(shader.id = glCreateShader(glStage(stage)));
		if (shader.id == 0)
		{
			THROW_MPP(string("Could not create ") + stageName(stage) + " shader id for program '" + getName() + "'.", __LINE__, __FILE__, __func__);
		}

		auto const source = specialise(stage);
		char const* sourcePtr = source.c_str();
		GL_CHECK(glShaderSource(shader.id, 1, (GLchar const**)&sourcePtr, nullptr));
		GL_CHECK(glCompileShader(shader.id));

		GLint status = GL_FALSE;
		GL_CHECK(glGetShaderiv(shader.id, GL_COMPILE_STATUS, &status));
		if (status == GL_FALSE)
		{
			string message = string("Could not compile ") + stageName(stage) + " shader for program '" + getName() + "'.\n";

			GLint infoLogLength = 0;
			GL_CHECK(glGetShaderiv(shader.id, GL_INFO_LOG_LENGTH, &infoLogLength));
			vector<char> infoLog(static_cast<size_t>(max(1, infoLogLength)), '\0');
			GL_CHECK(glGetShaderInfoLog(shader.id, infoLogLength, nullptr, infoLog.data()));

			message += infoLog.data();
			message += "\n--------------------------------\n";
			message += source;

			THROW_MPP(message, __LINE__, __FILE__, __func__);
		}

		auto const label = string(stageName(stage)) + " shader: " + getName();
		GL_CHECK(glObjectLabel(GL_SHADER, shader.id, -1, label.c_str()));

		return shader.release();
	}

	void RawShaderProgram::link(vector<RawShaderStage> const& stages)
	{
		mUniformLocations.clear();

		// Every GL name stays local until the link succeeds, so a failed compile
		// or link leaves this resource unloaded rather than half-loaded.
		vector<ShaderHandle> shaders(stages.size());
		ProgramHandle program;
		GL_CHECK(program.id = glCreateProgram());
		if (program.id == 0)
		{
			THROW_MPP("Could not create program id for '" + getName() + "'.", __LINE__, __FILE__, __func__);
		}

		for (size_t index = 0; index < stages.size(); ++index)
		{
			if (!hasSource(stages[index]))
			{
				THROW_MPP(string("Program '") + getName() + "' has no " + stageName(stages[index]) + " source.", __LINE__, __FILE__, __func__);
			}
			shaders[index].id = compileStage(stages[index]);
			GL_CHECK(glAttachShader(program.id, shaders[index].id));
		}

		GL_CHECK(glLinkProgram(program.id));

		GLint status = GL_FALSE;
		GL_CHECK(glGetProgramiv(program.id, GL_LINK_STATUS, &status));
		if (status == GL_FALSE)
		{
			string message = "Could not link program '" + getName() + "': ";

			GLint infoLogLength = 0;
			GL_CHECK(glGetProgramiv(program.id, GL_INFO_LOG_LENGTH, &infoLogLength));
			vector<char> infoLog(static_cast<size_t>(max(1, infoLogLength)), '\0');
			GL_CHECK(glGetProgramInfoLog(program.id, infoLogLength, nullptr, infoLog.data()));

			message += infoLog.data();

			THROW_MPP(message, __LINE__, __FILE__, __func__);
		}

		auto const label = "Program: " + getName();
		GL_CHECK(glObjectLabel(GL_PROGRAM, program.id, -1, label.c_str()));

		for (auto& shader : shaders)
		{
			GL_CHECK(glDetachShader(program.id, shader.id));
		}

		setId(program.release());
		getRenderSystem()->infoMessage("Created program '" + getName() + "'.");
	}

	void RawShaderProgram::use()
	{
		if (getId() == 0)
		{
			THROW_MPP("Program '" + getName() + "' is not loaded.", __LINE__, __FILE__, __func__);
		}
		GL_CHECK(glUseProgram(getId()));
	}

	int RawShaderProgram::getUniformLocation(string const& name)
	{
		auto const found = mUniformLocations.find(name);
		if (found != mUniformLocations.end()) return found->second;

		int location = -1;
		GL_CHECK(location = glGetUniformLocation(getId(), name.c_str()));
		mUniformLocations[name] = location;
		return location;
	}

	void RawShaderProgram::setUniform(string const& name, int32_t value)
	{
		auto const location = getUniformLocation(name);
		if (location >= 0) GL_CHECK(glUniform1i(location, value));
	}

	void RawShaderProgram::setUniform(string const& name, uint32_t value)
	{
		auto const location = getUniformLocation(name);
		if (location >= 0) GL_CHECK(glUniform1ui(location, value));
	}

	void RawShaderProgram::setUniform(string const& name, float value)
	{
		auto const location = getUniformLocation(name);
		if (location >= 0) GL_CHECK(glUniform1f(location, value));
	}
}
