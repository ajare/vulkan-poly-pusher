#include <format>
#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <cassert>
#include <algorithm>
#include <numeric>
#include <regex>
#include <list>

#include <GL/glew.h>
#include <GL/gl.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

#include "utils/StringUtils.h"

#include "mpp/RenderSystem.h"
#include "mpp/Program.h"
#include "mpp/ProgramStream.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	namespace
	{
		// A surface material keeps its own fragment shader and vertex-attribute
		// contract when used by mesh particles.  Injecting this small opt-in path
		// into generated 3D vertex programs lets the dedicated mesh-particle pass
		// use that exact material while sourcing one model matrix per GPU instance.
		// Ordinary model draws leave MPP_PARTICLE_MESH_ENABLED at zero and retain
		// the generated program's original matrix uniforms.
		string addParticleMeshInstancing(string source)
		{
			string const modelDeclaration = string("uniform mat4 ") + MPP_PROGRAM_MMATRIX_NAME + ";";
			string const mcpDeclaration = string("uniform mat4 ") + MPP_PROGRAM_MCPMATRIX_NAME + ";";
			bool const hasModelMatrix = source.find(modelDeclaration) != string::npos;
			if (source.find(mcpDeclaration) == string::npos) return source;

			auto const mainPosition = source.find("void main");
			auto const bodyPosition = mainPosition == string::npos ? string::npos : source.find('{', mainPosition);
			if (bodyPosition == string::npos) return source;
			bool const hasNormalMatrix = source.find(string("uniform mat3 ") + MPP_PROGRAM_NORMALMATRIX_NAME + ";") != string::npos;

			string body = source.substr(bodyPosition + 1u);
			auto replaceAll = [](string& value, string const& search, string const& replacement)
			{
				size_t position = 0u;
				while ((position = value.find(search, position)) != string::npos)
				{
					value.replace(position, search.size(), replacement);
					position += replacement.size();
				}
			};
			replaceAll(body, MPP_PROGRAM_MCPMATRIX_NAME, "mppParticleModelCameraProjection");
			if (hasModelMatrix) replaceAll(body, MPP_PROGRAM_MMATRIX_NAME, "mppParticleModel");
			if (hasNormalMatrix) replaceAll(body, MPP_PROGRAM_NORMALMATRIX_NAME, "mppParticleNormalMatrix");

			string locals = "\n    mat4 mppParticleModel = _mpp_u_MPP_PARTICLE_MESH_ENABLED_ != 0 ? mppParticleMeshModel() : ";
			locals += hasModelMatrix ? string(MPP_PROGRAM_MMATRIX_NAME) : "mat4(1.0)";
			locals += string(";\n    mat4 mppParticleModelCameraProjection = _mpp_u_MPP_PARTICLE_MESH_ENABLED_ != 0 ? ") +
				"MPP_PARTICLE_PROJECTION_MATRIX * MPP_PARTICLE_VIEW_MATRIX * mppParticleModel : " +
				MPP_PROGRAM_MCPMATRIX_NAME + ";\n";
			if (hasNormalMatrix)
				locals += string("    mat3 mppParticleNormalMatrix = _mpp_u_MPP_PARTICLE_MESH_ENABLED_ != 0 ? ") +
					"transpose(inverse(mat3(mppParticleModel))) : " + MPP_PROGRAM_NORMALMATRIX_NAME + ";\n";
			source = source.substr(0u, bodyPosition + 1u) + locals + body;

			string const support = R"MPP(

// Engine-reserved mesh-particle instancing interface.
layout(std140, binding = 3) uniform MppParticleMeshCameraFrame
{
    mat4 MPP_PARTICLE_VIEW_MATRIX;
    mat4 MPP_PARTICLE_PROJECTION_MATRIX;
    mat4 MPP_PARTICLE_INVERSE_PROJECTION_MATRIX;
    vec4 MPP_PARTICLE_VIEWPORT_SIZE;
    vec4 MPP_PARTICLE_NEAR_FAR_TIME;
};
struct MppParticleMeshRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};
layout(std430, binding = 0) restrict readonly buffer MppParticleMeshPool
{
    MppParticleMeshRecord MPP_PARTICLE_MESH_PARTICLES[];
};
layout(std430, binding = 1) restrict readonly buffer MppParticleMeshRenderIndices
{
    uint MPP_PARTICLE_MESH_RENDER_INDICES[];
};
layout(std430, binding = 7) restrict readonly buffer MppParticleMeshCompaction
{
    uint MPP_PARTICLE_MESH_COMPACTION_VALUES[];
};
uniform int _mpp_u_MPP_PARTICLE_MESH_ENABLED_;
uniform uint _mpp_u_MPP_PARTICLE_MESH_TEMPLATE_;

mat4 mppParticleMeshModel()
{
    uint renderOrdinal = MPP_PARTICLE_MESH_COMPACTION_VALUES[_mpp_u_MPP_PARTICLE_MESH_TEMPLATE_] + uint(gl_InstanceID);
    MppParticleMeshRecord particle = MPP_PARTICLE_MESH_PARTICLES[MPP_PARTICLE_MESH_RENDER_INDICES[renderOrdinal]];
    vec3 axis = particle.velocityLifetime.xyz;
    float axisLengthSquared = dot(axis, axis);
    axis = axisLengthSquared > 1.0e-12 ? axis * inversesqrt(axisLengthSquared) : vec3(0.0, 1.0, 0.0);
    float sineRotation = sin(particle.rotation);
    float cosineRotation = cos(particle.rotation);
    float oneMinusCosine = 1.0 - cosineRotation;
    mat3 rotation = mat3(
        cosineRotation + axis.x * axis.x * oneMinusCosine,
        axis.y * axis.x * oneMinusCosine + axis.z * sineRotation,
        axis.z * axis.x * oneMinusCosine - axis.y * sineRotation,
        axis.x * axis.y * oneMinusCosine - axis.z * sineRotation,
        cosineRotation + axis.y * axis.y * oneMinusCosine,
        axis.z * axis.y * oneMinusCosine + axis.x * sineRotation,
        axis.x * axis.z * oneMinusCosine + axis.y * sineRotation,
        axis.y * axis.z * oneMinusCosine - axis.x * sineRotation,
        cosineRotation + axis.z * axis.z * oneMinusCosine);
    float scale = particle.baseSize;
    return mat4(vec4(rotation[0] * scale, 0.0), vec4(rotation[1] * scale, 0.0),
        vec4(rotation[2] * scale, 0.0), vec4(particle.positionAge.xyz, 1.0));
}
)MPP";
			auto const versionEnd = source.find('\n', source.find("#version"));
			if (versionEnd == string::npos) return source;
			source.insert(versionEnd + 1u, support);
			return source;
		}

		struct ShaderHandle
		{
			GLuint id{ 0 };
			~ShaderHandle() { if (id != 0) glDeleteShader(id); }
		};

		struct ProgramHandle
		{
			GLuint id{ 0 };
			~ProgramHandle() { if (id != 0) glDeleteProgram(id); }
			GLuint release() { auto result = id; id = 0; return result; }
		};
	}

	/*
	 * Constructor.
	 *
	 */
	Program::Program(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "Program", renderSystem, resourceMgr, resourceStream)
		, mSortId(0)
	{
	}

	/*
	 * Comparison operator.
	 *
	 */
	bool Program::operator==(Program const& other)
	{
		return this->getId() == other.getId();
	}

	/*
	 * Compile shader source.
	 *
	 */
	void Program::compileShader(uint32_t* id, string const& source, string const& sourceType)
	{
		char const* sourcePtr = source.c_str();
		glShaderSource(*id, 1, (const GLchar**)&sourcePtr, nullptr);
		glCompileShader(*id);

		// Check for errors
		GLint status;
		glGetShaderiv(*id, GL_COMPILE_STATUS, &status);

		if (status == GL_FALSE) 
		{
			string msg = "Could not compile " + sourceType + " shader for program '" + getName() + "'.\n";
			
			GLint infoLogLength = 0;
			glGetShaderiv(*id, GL_INFO_LOG_LENGTH, &infoLogLength);
			vector<char> infoLog(static_cast<size_t>(max(1, infoLogLength)), '\0');
			glGetShaderInfoLog(*id, infoLogLength, nullptr, infoLog.data());
			msg += infoLog.data();

			msg += "\n";
			msg += "--------------------------------\n";
			msg += source;
			
			glDeleteShader(*id); 
			*id = 0;

			THROW_MPP(msg, __LINE__, __FILE__, __func__);
		}

		getRenderSystem()->infoMessage("Compiled " + sourceType + " shader.");
	}
		
	/*
	 * Create the data required for the program from the resource stream.
	 *
	 */
	void Program::createImpl()
	{
		ProgramStream* pStr = dynamic_cast<ProgramStream*>(getResourceStream().get());
		if (!pStr)
		{
			THROW_MPP("Could not cast to type 'ProgramStream'.", __LINE__, __FILE__, __func__);
		}

		auto const& caps = getRenderSystem()->getCaps();
		mVertexSource = caps.supportsCompute && caps.maxShaderStorageBufferBindings >= 8u
			? addParticleMeshInstancing(pStr->getVertexSource()) : pStr->getVertexSource();
		mFragmentSource = pStr->getFragmentSource();
		mFlags = pStr->getFlags();
	}

	/*
	 * Destroy the program data.
	 *
	 */
	void Program::destroyImpl()
	{
		mVertexSource.clear();
		mFragmentSource.clear();
	}

	void Program::resetReflectionState()
	{
		mTextures.clear();
		mVertexAttributes.clear();
		mUniformIds.clear();
		mUniformTypes.clear();
		mViewPosId = -1;
		mMMatrixId = -1;
		mMcpMatrixId = -1;
		mNormalMatrixId = -1;
		mHalfWindowSizeId = -1;
		mPointSizeId = -1;
		mFragmentOutputLocationsKnown = false;
		mFragmentOutputLocationMask = 0;
	}

	mesh::MeshSpecification const& Program::getMeshSpecification() const
	{
		return mMeshSpecification;
	}

	/*
	 * Create a variable info instance.
	 *
	 */
	Program::VariableInfo Program::getVariableInfo(string const& def, string const& name, string const& type, ShaderType shaderType)
	{
		VariableInfo vi;
		vi.def = def;
		vi.name = name;
		vi.type = type;

		if (vi.type == "bool" || vi.type == "int" || vi.type == "uint" || vi.type == "float" || vi.type == "double" ||
			vi.type == "bvec2" || vi.type == "bvec3" || vi.type == "bvec4" ||
			vi.type == "ivec2" || vi.type == "ivec3" || vi.type == "ivec4" ||
			vi.type == "uvec2" || vi.type == "uvec3" || vi.type == "uvec4" ||
			vi.type == "vec2" || vi.type == "vec3" || vi.type == "vec4" ||
			vi.type == "dvec2" || vi.type == "dvec3" || vi.type == "dvec4")
		{
			if (vi.type == "bool" || vi.type == "int" || vi.type == "uint" || vi.type == "float" || vi.type == "double")
			{
				vi.numComponents = 1;
			}
			else if (vi.type == "bvec2" || vi.type == "ivec2" || vi.type == "uvec2" || vi.type == "vec2" || vi.type == "dvec2")
			{
				vi.numComponents = 2;
			}
			else if (vi.type == "bvec3" || vi.type == "ivec3" || vi.type == "uvec3" || vi.type == "vec3" || vi.type == "dvec3")
			{
				vi.numComponents = 3;
			}
			else if (vi.type == "bvec4" || vi.type == "ivec4" || vi.type == "uvec4" || vi.type == "vec4" || vi.type == "dvec4")
			{
				vi.numComponents = 4;
			}
		}

		if (shaderType == ShaderType::Vertex)
		{
			if (def == "in" || def == "passthrough")
			{
				if (mVertexAttributes.empty())
				{
					vi.streamOffset = 0;
				}
				else
				{
					vi.streamOffset = mVertexAttributes.back().streamOffset + mVertexAttributes.back().numComponents;
				}

				mVertexAttributes.push_back(vi);
			}
		}

		return vi;
	}

	/*
	 * Remove comments from GLSL source code.
	 *
	 */
	string Program::stripComments(std::string const& src)
	{
		string stripped = src;

		for (size_t i = 0; i < stripped.length(); ++i)
		{
			if (i < (stripped.length() - 1))
			{
				if (stripped[i] == '/')
				{
					if (stripped[i + 1] == '/')
					{
						// Find '\n'
						int commentStart = (int)i, commentEnd = -1;
						for (size_t j = (size_t)commentStart; j < stripped.length(); ++j)
						{
							if (stripped[j] == '\n')
							{
								commentEnd = (int)j;
								break;
							}
						}

						// Check if we hit the end of the file
						if (commentEnd == -1)
						{
							commentEnd = (int)(stripped.length() - 1);
						}

						stripped.erase(commentStart, commentEnd - commentStart + 1);
						--i;
					}
					if (stripped[i + 1] == '*')
					{
						// Find '*/'
						int commentStart = (int)i, commentEnd = -1;
						for (size_t j = (size_t)commentStart; j < stripped.length(); ++j)
						{
							if (stripped[j - 1] == '*' && stripped[j] == '/')
							{
								commentEnd = (int)j;
								break;
							}
						}

						// Check if we hit the end of the file
						if (commentEnd == -1)
						{
							THROW_MPP("Could not parse multi-line comment.", __LINE__, __FILE__, __func__);
						}

						stripped.erase(commentStart, commentEnd - commentStart + 1);
						--i;
					}
				}
			}
		}

		return stripped;
	}

	/*
	 * Split into lines by statement and newline
	 *
	 */
	vector<string> Program::splitSourceIntoLines(string const& src)
	{
		vector<string> lines;

		uint32_t i = 0, j = 0;
		while (i != src.length())
		{
			char ch = src[i];
			if (ch == ';' || ch == '\n')
			{
				string line = src.substr(j, i - j);
				if (!line.empty())
				{
					lines.push_back(line);
				}

				lines.push_back(string(&ch, 1));
				j = i + 1;
			}

			i++;
		}

		string line = src.substr(j, i - j);
		if (!line.empty())
		{
			lines.push_back(line);
		}

		return lines;
	}

	/*
	 * Create OpenGL program.
	 *
	 */
	void Program::loadImpl()
	{
		auto rs = getRenderSystem();
		resetReflectionState();
		try
		{
			ProgramStream* pStr = dynamic_cast<ProgramStream*>(getResourceStream().get());
			if (!pStr)
			{
				THROW_MPP("Could not cast to type 'ProgramStream'.", __LINE__, __FILE__, __func__);
			}

			auto meshSpecification = pStr->getMeshSpecification();
			vector<TextureInfo> reflectedTextures;
			vector<VariableInfo> reflectedVertexAttributes;
			map<string, int> reflectedUniformIds;
			map<string, uint32_t> reflectedUniformTypes;
			int viewPosId = -1, modelMatrixId = -1, modelCameraProjectionMatrixId = -1;
			int normalMatrixId = -1, halfWindowSizeId = -1, pointSizeId = -1;
			bool fragmentOutputLocationsKnown = false;
			uint64_t fragmentOutputLocationMask = 0;

			// Set up textures
			auto textures = pStr->getTextures();
			for (auto const& texture: textures)
			{
				TextureInfo ti;
				ti.samplerName = texture;
				ti.markedUpName = MPP_PROGRAM_MARKUP_TEXTURE(texture);
				ti.uniformId = -1;

				reflectedTextures.push_back(ti);
			}

			// Set up vertex attributes
			auto inAttribs = pStr->getInAttributes();
			for (auto const& inAttrib: inAttribs)
			{
				VariableInfo vi;
					
				vi.def = "in";
				vi.name = inAttrib.name;
				vi.type = inAttrib.type.name;
				vi.numComponents = (int)(inAttrib.type.size[0] * inAttrib.type.size[1]);
				vi.streamOffset = reflectedVertexAttributes.empty() ? 0 :
					reflectedVertexAttributes.back().streamOffset + reflectedVertexAttributes.back().numComponents;
					
				reflectedVertexAttributes.push_back(vi);
			}

			// Keep every GL name local until linking and reflection have succeeded.
			// The handles clean up automatically on every exception path.
			ShaderHandle vertexShader;
			GL_CHECK(vertexShader.id = glCreateShader(GL_VERTEX_SHADER));
			if (vertexShader.id == 0)
			{
				THROW_MPP("Could not create vertex shader id.", __LINE__, __FILE__, __func__);
			}

			compileShader(&vertexShader.id, mVertexSource, "vertex");

			// Set name for debugging
			auto label = "Vertex shader: " + getName();
			GL_CHECK(glObjectLabel(GL_SHADER, vertexShader.id, -1, label.c_str()));

			ShaderHandle fragmentShader;
			GL_CHECK(fragmentShader.id = glCreateShader(GL_FRAGMENT_SHADER));
			if (fragmentShader.id == 0)
			{
				THROW_MPP("Could not create fragment shader id.", __LINE__, __FILE__, __func__);
			}

			compileShader(&fragmentShader.id, mFragmentSource, "fragment");

			// Set name for debugging
			label = "Fragment shader: " + getName();
			GL_CHECK(glObjectLabel(GL_SHADER, fragmentShader.id, -1, label.c_str()));

			ProgramHandle linkedProgram;
			GL_CHECK(linkedProgram.id = glCreateProgram());
			if (linkedProgram.id == 0)
			{
				THROW_MPP("Could not create program id.", __LINE__, __FILE__, __func__);
			}

			auto const programId = linkedProgram.id;

			// Attach shaders
			GL_CHECK(glAttachShader(programId, vertexShader.id));
			GL_CHECK(glAttachShader(programId, fragmentShader.id));

			// Link shaders
			GL_CHECK(glLinkProgram(programId));

			// Set name for debugging
			label = "Program: " + getName();
			GL_CHECK(glObjectLabel(GL_PROGRAM, programId, -1, label.c_str()));

			GLint status;
			GL_CHECK(glGetProgramiv(programId, GL_LINK_STATUS, &status));

			if (status == GL_FALSE)
			{
				string msg = "Could not link program: ";

				GLint infoLogLength = 0;
				GL_CHECK(glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLogLength));
				vector<char> infoLog(static_cast<size_t>(max(1, infoLogLength)), '\0');

				GL_CHECK(glGetProgramInfoLog(programId, infoLogLength, nullptr, infoLog.data()));
				msg += infoLog.data();

				THROW_MPP(msg, __LINE__, __FILE__, __func__);
			}

			// Shaders are no longer needed after a successful link. Detaching now
			// lets their RAII handles delete them before reflection begins.
			GL_CHECK(glDetachShader(programId, vertexShader.id));
			GL_CHECK(glDetachShader(programId, fragmentShader.id));
			GL_CHECK(glDeleteShader(vertexShader.id));
			vertexShader.id = 0;
			GL_CHECK(glDeleteShader(fragmentShader.id));
			fragmentShader.id = 0;

			// Get attribute information
			int attribCount;
			GL_CHECK(glGetProgramiv(programId, GL_ACTIVE_ATTRIBUTES, &attribCount));
			for (int i = 0; i < attribCount; ++i)
			{
				//GLint location = glGetAttribLocation(programId, mVertexAttributes[i].name.c_str());

				const GLsizei bufSize = 256;
				GLsizei lengthWritten;
				GLint varSize;
				GLenum varType;
				char varNameBuffer[bufSize];

				GL_CHECK(glGetActiveAttrib(programId, i, bufSize - 1, &lengthWritten, &varSize, &varType, varNameBuffer));
				varNameBuffer[lengthWritten] = 0;

				int location = glGetAttribLocation(programId, varNameBuffer);
				string varName = varNameBuffer;

				string typeName;
				switch (varType)
				{
				case GL_FLOAT: typeName = "float"; break;
				case GL_FLOAT_VEC2: typeName = "vec2";  break;
				case GL_FLOAT_VEC3: typeName = "vec3"; break;
				case GL_FLOAT_VEC4: typeName = "vec4"; break;
				case GL_FLOAT_MAT2: typeName = "mat2"; break;
				case GL_FLOAT_MAT2x3: typeName = "mat2x3"; break;
				case GL_FLOAT_MAT2x4: typeName = "mat2x4"; break;
				case GL_FLOAT_MAT3x2: typeName = "mat3x2"; break;
				case GL_FLOAT_MAT3: typeName = "mat3"; break;
				case GL_FLOAT_MAT3x4: typeName = "mat3x4"; break;
				case GL_FLOAT_MAT4x2: typeName = "mat4x2"; break;
				case GL_FLOAT_MAT4x3: typeName = "mat4x3"; break;
				case GL_FLOAT_MAT4: typeName = "mat4"; break;
				case GL_INT: typeName = "int"; break;
				case GL_INT_VEC2: typeName = "ivec2";  break;
				case GL_INT_VEC3: typeName = "ivec3"; break;
				case GL_INT_VEC4: typeName = "ivec4"; break;
				case GL_UNSIGNED_INT: typeName = "uint"; break;
				case GL_UNSIGNED_INT_VEC2: typeName = "uvec2";  break;
				case GL_UNSIGNED_INT_VEC3: typeName = "uvec3"; break;
				case GL_UNSIGNED_INT_VEC4: typeName = "uvec4"; break;
				case GL_DOUBLE: typeName = "double"; break;
				case GL_DOUBLE_VEC2: typeName = "dvec2";  break;
				case GL_DOUBLE_VEC3: typeName = "dvec3"; break;
				case GL_DOUBLE_VEC4: typeName = "dvec4"; break;
				case GL_DOUBLE_MAT2: typeName = "dmat2"; break;
				case GL_DOUBLE_MAT2x3: typeName = "dmat2x3"; break;
				case GL_DOUBLE_MAT2x4: typeName = "dmat2x4"; break;
				case GL_DOUBLE_MAT3x2: typeName = "dmat3x2"; break;
				case GL_DOUBLE_MAT3: typeName = "dmat3"; break;
				case GL_DOUBLE_MAT3x4: typeName = "dmat3x4"; break;
				case GL_DOUBLE_MAT4x2: typeName = "dmat4x2"; break;
				case GL_DOUBLE_MAT4x3: typeName = "dmat4x3"; break;
				case GL_DOUBLE_MAT4: typeName = "dmat4"; break;
				default: typeName = "unknown type"; break;
				}
				
				rs->debugMessage(std::format("Vertex attribute {}: {} ({}[{}]).", location, varName, typeName, varSize));
			}

			// Get uniform information
			int uniformCount;
			GL_CHECK(glGetProgramiv(programId, GL_ACTIVE_UNIFORMS, &uniformCount));

			for (int i = 0; i < uniformCount; ++i)
			{
				const GLsizei bufSize = 256;
				GLsizei lengthWritten;
				GLint uniformSize;
				GLenum uniformType;
				char uniformNameBuffer[bufSize];

				GL_CHECK(glGetActiveUniform(programId, i, bufSize - 1, &lengthWritten, &uniformSize, &uniformType, uniformNameBuffer));
				uniformNameBuffer[lengthWritten] = 0;

				string uniformName = uniformNameBuffer;
				reflectedUniformTypes[uniformName] = uniformType;

				// Is this ViewPos, MMatrix, MCPMatrix, NormalMatrix, standard uniform or a texture?
				if (uniformName == MPP_PROGRAM_VIEWPOS_NAME)
				{
					GL_CHECK(viewPosId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform: ViewPosition id: {}", viewPosId));
				}
				if (uniformName == MPP_PROGRAM_MMATRIX_NAME)
				{
					GL_CHECK(modelMatrixId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform: Model matrix id: {}", modelMatrixId));
				}
				if (uniformName == MPP_PROGRAM_MCPMATRIX_NAME)
				{
					GL_CHECK(modelCameraProjectionMatrixId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform: ModelCameraProjection matrix id: {}", modelCameraProjectionMatrixId));
				}
				else if (uniformName == MPP_PROGRAM_NORMALMATRIX_NAME)
				{
					GL_CHECK(normalMatrixId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform normal matrix id: {}", normalMatrixId));
				}
				else if (uniformName == MPP_PROGRAM_HALFWINDOWSIZE_NAME)
				{
					GL_CHECK(halfWindowSizeId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform: half window size id: {}", halfWindowSizeId));
				}
				else if (uniformName == MPP_PROGRAM_POINTSIZE_NAME)
				{
					GL_CHECK(pointSizeId = glGetUniformLocation(programId, uniformNameBuffer));
					rs->debugMessage(std::format("- Uniform: point size id: {}", pointSizeId));
				}
				else
				{
					auto it = find_if(reflectedTextures.begin(), reflectedTextures.end(), [uniformName](TextureInfo const& ti) -> bool
					{
						return ti.markedUpName == uniformName;
					});

					if (it != reflectedTextures.end())
					{
						GL_CHECK(it->uniformId = glGetUniformLocation(programId, uniformNameBuffer));
						rs->debugMessage(std::format("- Texture: '{}' id: {}", uniformName, it->uniformId));
					}
					else
					{
						int32_t uniformId;
						GL_CHECK(uniformId = glGetUniformLocation(programId, uniformNameBuffer));
						reflectedUniformIds[uniformName] = uniformId;
						rs->debugMessage(std::format("- Texture: '{}' id: {}", uniformName, uniformId));
					}
				}
			}

			// Parser metadata includes declarations in GLSL preprocessor-disabled
			// branches. Bind and expose only samplers that survived linking.
			reflectedTextures.erase(remove_if(reflectedTextures.begin(), reflectedTextures.end(), [](TextureInfo const& texture)
			{
				return texture.uniformId < 0;
			}), reflectedTextures.end());

			// Program-interface reflection is optional on older contexts. Cache the
			// result once at link time so scene MRT validation never performs GL
			// queries while walking visible materials.
			if (GLEW_VERSION_4_3 || GLEW_ARB_program_interface_query)
			{
				fragmentOutputLocationsKnown = true;
				GLint resourceCount = 0;
				GL_CHECK(glGetProgramInterfaceiv(programId, GL_PROGRAM_OUTPUT, GL_ACTIVE_RESOURCES, &resourceCount));
				GLenum const property = GL_LOCATION;
				for (GLint resource = 0; resource < resourceCount; ++resource)
				{
					GLint location = -1;
					GL_CHECK(glGetProgramResourceiv(programId, GL_PROGRAM_OUTPUT, resource, 1, &property, 1, nullptr, &location));
					if (location >= 0 && location < 64) fragmentOutputLocationMask |= uint64_t{ 1 } << location;
				}
			}
			else
			{
				// glGetFragDataLocation is available on the engine's baseline context.
				// Query every generated output declaration so MRT contracts remain
				// enforceable even without ARB_program_interface_query.
				static regex const outputDeclaration(R"(layout\s*\(\s*location\s*=\s*\d+\s*\)\s*out\s+\w+\s+(\w+)\s*;)");
				for (sregex_iterator output(mFragmentSource.begin(), mFragmentSource.end(), outputDeclaration), end; output != end; ++output)
				{
					fragmentOutputLocationsKnown = true;
					auto const name = (*output)[1].str();
					GLint location = -1;
					GL_CHECK(location = glGetFragDataLocation(programId, name.c_str()));
					if (location >= 0 && location < 64) fragmentOutputLocationMask |= uint64_t{ 1 } << location;
				}
			}

			// Publish CPU reflection and the GL name only after every operation has
			// succeeded. Until this point all GL names are owned by local handles.
			mMeshSpecification = move(meshSpecification);
			mTextures = move(reflectedTextures);
			mVertexAttributes = move(reflectedVertexAttributes);
			mUniformIds = move(reflectedUniformIds);
			mUniformTypes = move(reflectedUniformTypes);
			mViewPosId = viewPosId;
			mMMatrixId = modelMatrixId;
			mMcpMatrixId = modelCameraProjectionMatrixId;
			mNormalMatrixId = normalMatrixId;
			mHalfWindowSizeId = halfWindowSizeId;
			mPointSizeId = pointSizeId;
			mFragmentOutputLocationsKnown = fragmentOutputLocationsKnown;
			mFragmentOutputLocationMask = fragmentOutputLocationMask;
			++mFragmentOutputRevision;
			setId(linkedProgram.release());
		}
		catch (exception&)
		{
			rs->errorMessage("Error creating program '" + getName() + "'.");
			throw;
		}

		rs->infoMessage("Created program '" + getName() + "'.");
	}

	/*
	 * Destroy the OpenGL program.
	 *
	 */
	void Program::unloadImpl()
	{
		resetReflectionState();
		++mFragmentOutputRevision;

		GLuint id = getId();
		if (id != 0)
		{
			GL_CHECK(glDeleteProgram(id));
			setId(0);
		}
	}

	/*
	 * Get index for specified uniform.
	 *
	 */
	int Program::getUniformId(string const& name, int index) const
	{
		string markedUpUniform = MPP_PROGRAM_MARKUP_UNIFORM(name);
		if (index >= 0)
		{
			markedUpUniform += std::format("[{}]", index);
		}

		if (mUniformIds.find(markedUpUniform) == mUniformIds.end())
		{
			return -1;
		}
		else
		{
			return mUniformIds.at(markedUpUniform);
		}
	}

	uint32_t Program::getUniformGlType(string const& name) const
	{
		auto const marked = MPP_PROGRAM_MARKUP_UNIFORM(name);
		auto found = mUniformTypes.find(marked);
		if (found != mUniformTypes.end()) return found->second;
		found = mUniformTypes.find(marked + "[0]");
		return found == mUniformTypes.end() ? 0u : found->second;
	}

	uint32_t Program::getSamplerGlType(string const& name) const
	{
		auto found = mUniformTypes.find(MPP_PROGRAM_MARKUP_TEXTURE(name));
		return found == mUniformTypes.end() ? 0u : found->second;
	}

	vector<string> Program::getUniformNames() const
	{
		vector<string> result;
		string const prefix = MPP_PROGRAM_UNIFORM_PREFIX;
		for (auto const& [name, id] : mUniformIds)
		{
			MPP_UNUSED(id);
			if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size()) continue;
			auto end = name.find("_[");
			if (end == string::npos && name.back() == '_') end = name.size() - 1;
			if (end != string::npos) result.push_back(name.substr(prefix.size(), end - prefix.size()));
		}
		return result;
	}

	int Program::getViewPosId() const
	{
		return mViewPosId;
	}

	/*
	 * Get index for model matrix, if using one, or -1.
	 *
	 */
	int Program::getModelMatrixId() const
	{
		return mMMatrixId;
	}

	/*
	 * Get index for model-camera-projection matrix, if using one, or -1.
	 *
	 */
	int Program::getModelCameraProjectionMatrixId() const
	{
		return mMcpMatrixId;
	}

	/*
	 * Get index for normal matrix, if using one, or -1.
	 *
	 */	
	int Program::getNormalMatrixId() const
	{
		return mNormalMatrixId;
	}

	/*
	 * Get index for half window size uniform, if using it, or -1.
	 *
	 */	
	int Program::getHalfWindowSizeId() const
	{
		return mHalfWindowSizeId;
	}

	/*
	 * Get index for point size uniform, if using it, or -1.
	 *
	 */
	int Program::getPointSizeId() const
	{
		return mPointSizeId;
	}

	/*
	 * Have a separate id for sorting to the GL id, as the GL id may not fit in however many bytes
	 * we have assigned to program in the sort key.
	 *
	 */
	void Program::setSortId(uint32_t sortId)
	{
		mSortId = sortId;
	}

	/*
	 * Get the sort id.
	 *
	 */
	uint32_t Program::getSortId() const
	{
		return mSortId;
	}

	/*
	 * Get sampler count.
	 *
	 */
	int Program::getNumSamplers() const
	{
		return (int)mTextures.size();
	}

	/*
	 * Get sampler name.
	 *
	 */
	string const& Program::getSamplerName(int index) const
	{
		return mTextures[index].samplerName;
	}

	int Program::getSamplerUnit(string const& name) const
	{
		for (size_t index = 0; index < mTextures.size(); ++index)
			if (mTextures[index].samplerName == name) return (int)index;
		return -1;
	}

	/*
	 * Get vertex attributes.
	 *
	 */
	bool Program::validateFragmentOutputLocations(size_t requiredCount, string& diagnostic) const
	{
		diagnostic.clear();
		if (requiredCount == 0 || !mFragmentOutputLocationsKnown) return true;
		for (size_t location = 0; location < requiredCount; ++location)
		{
			if (location >= 64 || (mFragmentOutputLocationMask & (uint64_t{ 1 } << location)) == 0)
			{
				diagnostic = "Program '" + getName() + "' has no active fragment output at required location " + to_string(location) + ".";
				return false;
			}
		}
		return true;
	}

	uint64_t Program::getFragmentOutputRevision() const
	{
		return mFragmentOutputRevision;
	}

	vector<Program::VariableInfo> const& Program::getVertexAttributes() const
	{
		return mVertexAttributes;
	}

	/*
	 * Activate program
	 *
	 */
	void Program::bind()
	{
		GL_CHECK(glUseProgram(getId()));

		// Bind texture unit locations to samplers
		for (uint32_t i = 0; i < mTextures.size(); ++i)
		{
			auto const& ti = mTextures[i];
			GL_CHECK(glUniform1i(ti.uniformId, i));
		}
	}

	/*
	 * How many GL names does this resource manage?
	 *
	 */
	int Program::getIdCount() const
	{
		return 1;
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Program::getLiveIdCount() const
	{
		int c;
		GL_CHECK(c = glIsProgram(getId()));
		return c;
	}
}

