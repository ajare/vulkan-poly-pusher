#include <format>
#include <cstring>
#include <utility>
#include <set>
#include <map>
#include <list>
#include <regex>
#include <sstream>

#include "utils/StringUtils.h"

#include "Parser.h"
#include "Attribute.h"
#include "Uniform.h"
#include "Texture.h"
#include "glslTypes.h"
#include "MppProgramException.h"

#define MPP_PROGRAM_VIEWPOS_TOKEN			"@ViewPos"
#define MPP_PROGRAM_MMATRIX_TOKEN			"@MMatrix"
#define MPP_PROGRAM_MCPMATRIX_TOKEN			"@MCPMatrix"
#define MPP_PROGRAM_NORMALMATRIX_TOKEN		"@NormalMatrix"
#define MPP_PROGRAM_HALFWINDOWSIZE_TOKEN	"@HalfWindowSize"
#define MPP_PROGRAM_POINTSIZE_TOKEN			"@PointSize"

#define MPP_PROGRAM_IN_PREFIX				"_mpp_i_"
#define MPP_PROGRAM_OUT_PREFIX				"_mpp_o_"

#define MPP_PROGRAM_UNIFORM_PREFIX			"_mpp_u_"
#define MPP_PROGRAM_TEXTURE_PREFIX			"_mpp_t_"

#define MPP_PROGRAM_VIEWPOS_NAME			(MPP_PROGRAM_UNIFORM_PREFIX "viewPos_")
#define MPP_PROGRAM_MMATRIX_NAME			(MPP_PROGRAM_UNIFORM_PREFIX "model_")
#define MPP_PROGRAM_MCPMATRIX_NAME			(MPP_PROGRAM_UNIFORM_PREFIX "modelCameraProjection_")
#define MPP_PROGRAM_NORMALMATRIX_NAME		(MPP_PROGRAM_UNIFORM_PREFIX "normal_")
#define MPP_PROGRAM_HALFWINDOWSIZE_NAME		(MPP_PROGRAM_UNIFORM_PREFIX "halfWindowSize_")
#define MPP_PROGRAM_POINTSIZE_NAME			(MPP_PROGRAM_UNIFORM_PREFIX "pointSize_")

#define MPP_PROGRAM_MARKUP_UNIFORM(token)	(MPP_PROGRAM_UNIFORM_PREFIX + token + "_")
#define MPP_PROGRAM_MARKUP_TEXTURE(token)	(MPP_PROGRAM_TEXTURE_PREFIX + token + "_")

using namespace std;

namespace mpp
{
	namespace program
	{

		extern map<string, GLSLTypeDecl> gsGLSLTypeDecls;

		/*
		 * Constructor.
		 *
		 */
		Parser::Parser()
			: Parser("<unnamed program>")
		{
		}

		/*
		 * Constructor.
		 *
		 */
		Parser::Parser(std::string const& name)
			: mName(name)
		{
			// Shader stages
			mStages[(int)ShaderStage::Type::Vertex].type = ShaderStage::Type::Vertex;
			mStages[(int)ShaderStage::Type::Geometry].type = ShaderStage::Type::Geometry;
			mStages[(int)ShaderStage::Type::Fragment].type = ShaderStage::Type::Fragment;

		}

		/*
		 * Get program name.
		 *
		 */
		string const& Parser::getName() const
		{
			return mName;
		}

		/*
		 * Set vertex shader source.
		 *
		 */
		void Parser::setVertexSource(string const& src)
		{
			auto& stage = mStages[(int)ShaderStage::Type::Vertex];
			
			stage.inputSource = src; 
			stage.source = stripComments(src);
			stage.clear();
		}

		/*
		 * Set geometry shader source.
		 *
		 */
		void Parser::setGeometrySource(string const& src)
		{
			auto& stage = mStages[(int)ShaderStage::Type::Geometry];

			stage.inputSource = src;
			stage.source = stripComments(src);
			stage.inAttribs.clear();
			stage.outAttribs.clear();
		}

		/*
		 * Set fragment shader source.
		 *
		 */		
		void Parser::setFragmentSource(string const& src)
		{
			auto& stage = mStages[(int)ShaderStage::Type::Fragment];

			stage.inputSource = src;
			stage.source = stripComments(src);
			stage.inAttribs.clear();
			stage.outAttribs.clear();
		}

		/*
		 * Set mesh specification to build program with.
		 *
		 */
		void Parser::setMeshSpecification(mesh::MeshSpecification const& spec)
		{
			mSpecification = spec;
		}

		/*.
		 * Helper to add an error
		 *
		 */
		void Parser::addError(ShaderStage::Type stageType, string const& error)
		{
			switch (stageType)
			{
			case ShaderStage::Type::Vertex:
				mErrors.push_back(mName + ":: vertex shader: " + error);
				break;

			case ShaderStage::Type::Geometry:
				mErrors.push_back(mName + ":: geometry shader: " + error);
				break;

			case ShaderStage::Type::Fragment:
				mErrors.push_back(mName + ":: fragment shader: " + error);
				break;

			default:
				THROW_MPP_PROGRAM("Unknown shader stage for '" + mName + "'.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Helper to add a warning.
		 *
		 */
		void Parser::addWarning(ShaderStage::Type stageType, string const& error)
		{
			switch (stageType)
			{
			case ShaderStage::Type::Vertex:
				mWarnings.push_back(mName + ":: vertex shader: " + error);
				break;

			case ShaderStage::Type::Geometry:
				mWarnings.push_back(mName + ":: geometry shader: " + error);
				break;

			case ShaderStage::Type::Fragment:
				mWarnings.push_back(mName + ":: fragment shader: " + error);
				break;

			default:
				THROW_MPP_PROGRAM("Unknown shader stage for '" + mName + "'.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Remove comments from GLSL source code.
		 *
		 */
		string Parser::stripComments(string const& src)
		{
			enum class Mode
			{
				Copy,
				Single,
				Multi
			};

			string stripped;
			Mode mode = Mode::Copy;

			for (size_t i = 0; i < src.size(); ++i)
			{
				if (src[i] == '/' && i < (src.size() - 1))
				{
					if (src[i + 1] == '/' && mode == Mode::Copy)
					{
						mode = Mode::Single;
						i += 1;
					}
					else if (src[i + 1] == '*' && mode == Mode::Copy)
					{
						mode = Mode::Multi;
						i += 1;
					}
				}
				else if (src[i] == '\n')
				{
					if (mode == Mode::Single)
					{
						mode = Mode::Copy;
					}
				}
				else if (src[i] == '*' && i < (src.size() - 1))
				{
					if (src[i + 1] == '/' && mode == Mode::Multi)
					{
						mode = Mode::Copy;
						i += 2;
					}
				}

				if (mode == Mode::Copy)
				{
					stripped.push_back(src[i]);
				}
			}

			if (mode != Mode::Copy)
			{
				THROW_MPP_PROGRAM("Invalid comment found in '" + mName + "'.", __LINE__, __FILE__, __func__);
			}

			return stripped;
		}

		/*
		 * Replace casts such as @Vec4 with correct generated code.
		 *
		 */
		string Parser::replaceCasts(ShaderStage::Type stageType, string const& src)
		{
			auto& stage = mStages[(int)stageType];

			regex re(R"(@Vec([2-4])\s*\(\s*(.*?)\s*\))");
			smatch match;

			auto parsedSrc = src;
			while (regex_search(parsedSrc, match, re))
			{
				auto dim = utils::StringUtils::parseUInt(match.str(1));
				string bareVar, fullVar = match.str(2);
				string swizzle = "";

				bareVar = fullVar;
				auto dotPos = fullVar.find('.');
				if (dotPos != string::npos)
				{
					bareVar = fullVar.substr(0, dotPos);
					swizzle = fullVar.substr(dotPos + 1);
				}

				// Get basic type
				string varName;
				VariableType varType;
				if (utils::StringUtils::startsWith(bareVar, MPP_PROGRAM_IN_PREFIX))
				{
					varName = bareVar.substr(strlen(MPP_PROGRAM_IN_PREFIX));
					varType = VariableType::InAttribute;
				}
				else if (utils::StringUtils::startsWith(bareVar, MPP_PROGRAM_OUT_PREFIX))
				{
					varName = bareVar.substr(strlen(MPP_PROGRAM_OUT_PREFIX));
					varType = VariableType::OutAttribute;
				}
				else if (utils::StringUtils::startsWith(bareVar, MPP_PROGRAM_UNIFORM_PREFIX))
				{
					varName = bareVar.substr(strlen(MPP_PROGRAM_UNIFORM_PREFIX));
					varType = VariableType::Uniform;
				}
				else if (utils::StringUtils::startsWith(bareVar, MPP_PROGRAM_TEXTURE_PREFIX))
				{
					varName = bareVar.substr(strlen(MPP_PROGRAM_TEXTURE_PREFIX));
					varType = VariableType::Texture;
				}
				else
				{
					addError(stageType, "unknown variable in cast: " + match.str(0));
					string replacement = std::format("vec{}({})", dim, fullVar);
					parsedSrc = parsedSrc.substr(0, match.position()) + replacement + parsedSrc.substr(match.position() + match.length());
					continue;
				}

				// Look up token in list of vars
				auto varSize = stage.getVariableSize(bareVar);
				if (varSize == 0)
				{
					addError(stageType, "unknown variable in cast: " + match.str(0));
					string replacement = std::format("vec{}({})", dim, fullVar);
					parsedSrc = parsedSrc.substr(0, match.position()) + replacement + parsedSrc.substr(match.position() + match.length());
					continue;
				}

				if (swizzle.length() != 0)
				{
					varSize = swizzle.length();
				}

				// If variable size is greater than dimension, or its swizzle is greater,
				// truncate/set the swizzle, and modify the varSize to be dim.
				auto varSizeDiff = (int)varSize - (int)dim;
				if (varSizeDiff > 0)
				{
					if (swizzle == "")
					{
						// Set the swizzle
						char const* swizzleChars{ "xyzw" };
						for (size_t i = 0; i < dim; ++i)
						{
							swizzle += swizzleChars[i];
						}
					}
					else
					{
						// Truncate the swizzle
						swizzle = swizzle.substr(0, dim);
					}

					varSize = dim;
					fullVar = bareVar + "." + swizzle;
				}

				string replacement = std::format("vec{}({}", dim, fullVar);

				GLSLTypeDecl varDeclType;
				switch (varType)
				{
				case VariableType::InAttribute:
					varDeclType = find_if(stage.inAttribs.begin(), stage.inAttribs.end(), [varName](auto const& attrStruct)
					{
						return attrStruct.name == varName;
					})->type;
					break;

				case VariableType::OutAttribute:
					varDeclType = find_if(stage.outAttribs.begin(), stage.outAttribs.end(), [varName](auto const& attrStruct)
					{
						return attrStruct.name == varName;
					})->type;
					break;

				case VariableType::Uniform:
					varDeclType = find_if(stage.uniforms.begin(), stage.uniforms.end(), [varName](auto const& uniStruct)
					{
						return uniStruct.name == varName;
					})->type;
					break;

				case VariableType::Texture:
					varDeclType = find_if(stage.textures.begin(), stage.textures.end(), [varName](auto const& texStruct)
					{
						return texStruct.name == varName;
					})->type;
					break;
				}

				for (size_t i = varSize; i < dim; ++i)
				{
					auto value = getComponentIndexDefault(varName, varDeclType.isFloatingPoint, (int)i, varName + ".x");
					replacement += ", " + value;
				}

				replacement += ")";

				parsedSrc = parsedSrc.substr(0, match.position()) + replacement + parsedSrc.substr(match.position() + match.length());
			}

			return parsedSrc;
		}

		string Parser::replaceUniformDeclaration(ShaderStage::Type stageType, string const& decl)
		{
			regex re(R"(@@Uniform\s*\(\s*([\w\d]+\s+)([\w\d\[\]]+)\s*\))");
			smatch match;

			if (regex_search(decl, match, re))
			{
				auto type = utils::StringUtils::trim(match.str(1));
				string fullName, name = utils::StringUtils::trim(match.str(2));
				fullName = name;

				// Get count and modify name according
				size_t count = 1;

				auto firstBrace = name.find_first_of('[');
				auto secondBrace = name.find_first_of(']');
				if (firstBrace != string::npos && secondBrace != string::npos)
				{
					count = utils::StringUtils::parseInt(name.substr(firstBrace + 1, secondBrace - firstBrace - 1));
					name = name.substr(0, firstBrace);
				}

				// Find name in uniform list
				auto const& uniforms = mStages[(int)stageType].uniforms;
				auto uniformIt = find_if(uniforms.begin(), uniforms.end(), [name](auto const& u)
				{
					return u.name == name;
				});

				auto const& uniform = *uniformIt;
				string replacement = type + " " + MPP_PROGRAM_MARKUP_UNIFORM(name);

				if (count > 1)
				{
					replacement += std::format("[{}]", count);
				}

				if (!uniform.inBlock)
				{
					replacement = "uniform " + replacement;
				}

				// Insert the replacement
				auto const& fullMatch = match.str(0);
				auto startIndex = decl.find(fullMatch);

				auto endIndex = startIndex + fullMatch.length();
				string pre = decl.substr(0, startIndex);
				string post = decl.substr(endIndex);
				return pre + replacement + post;
			}
			else
			{
				return decl;
			}
		}

		string Parser::replaceUniformUsage(ShaderStage::Type stageType, string const& usage)
		{
			// This needs to parse multiple uniforms on one line, building up the return
			// replacement string.

			regex re(R"(@Uniform\s*\(\s*([\w\d\[\]]+)\s*\))");
			smatch match;

			string repl = usage, res;
			while (regex_search(repl, match, re))
			{
				string name = utils::StringUtils::trim(match.str(1));

				// Get count and modify name according
				string index;

				auto firstBrace = name.find_first_of('[');
				auto secondBrace = name.find_first_of(']');
				if (firstBrace != string::npos && secondBrace != string::npos)
				{
					index = name.substr(firstBrace + 1, secondBrace - firstBrace - 1);
					name = name.substr(0, firstBrace);
				}

				string replacement = MPP_PROGRAM_MARKUP_UNIFORM(name);
				if (index != "")
				{
					replacement += "[" + index + "]";
				}

				// Insert the replacement
				auto const& fullMatch = match.str(0);
				auto startIndex = repl.find(fullMatch);

				auto endIndex = startIndex + fullMatch.length();
				string pre = repl.substr(0, startIndex);
				string post = repl.substr(endIndex);
				
				res += pre + replacement;
				repl = post;
			}
			
			res += repl;
			return res;
		}

		string Parser::Parser::replaceTextureDeclaration(ShaderStage::Type stageType, string const& decl)
		{
			regex re(R"(@@Texture\s*\(\s*([\w\d]+\s+)([\w\d\[\]]+)\s*\))");
			smatch match;

			if (regex_search(decl, match, re))
			{
				auto type = utils::StringUtils::trim(match.str(1));
				string fullName, name = utils::StringUtils::trim(match.str(2));
				fullName = name;

				// Get count and modify name according
				size_t count = 1;

				auto firstBrace = name.find_first_of('[');
				auto secondBrace = name.find_first_of(']');
				if (firstBrace != string::npos && secondBrace != string::npos)
				{
					count = utils::StringUtils::parseInt(name.substr(firstBrace + 1, secondBrace - firstBrace - 1));
					name = name.substr(0, firstBrace);
				}

				// Find name in texture list
				auto const& textures = mStages[(int)stageType].textures;
				auto textureIt = find_if(textures.begin(), textures.end(), [name](auto const& t)
				{
					return t.name == name;
				});

				auto const& texture = *textureIt;
				string replacement = type + " " + MPP_PROGRAM_MARKUP_TEXTURE(name);

				if (count > 1)
				{
					replacement += std::format("[{}]", count);
				}

				replacement = "uniform " + replacement;

				// Insert the replacement
				auto const& fullMatch = match.str(0);
				auto startIndex = decl.find(fullMatch);

				auto endIndex = startIndex + fullMatch.length();
				string pre = decl.substr(0, startIndex);
				string post = decl.substr(endIndex);
				return pre + replacement + post;
			}
			else
			{
				return decl;
			}
		}

		string Parser::replaceTextureUsage(ShaderStage::Type stageType, string const& usage)
		{
			regex re(R"(@Texture\s*\(\s*([\w\d\[\]]+)\s*\))");
			smatch match;

			string repl = usage, res;
			while (regex_search(repl, match, re))
			{
				string name = utils::StringUtils::trim(match.str(1));

				// Get count and modify name according
				string index;

				auto firstBrace = name.find_first_of('[');
				auto secondBrace = name.find_first_of(']');
				if (firstBrace != string::npos && secondBrace != string::npos)
				{
					index = name.substr(firstBrace + 1, secondBrace - firstBrace - 1);
					name = name.substr(0, firstBrace);
				}

				string replacement = MPP_PROGRAM_MARKUP_TEXTURE(name);
				if (index != "")
				{
					replacement += "[" + index + "]";
				}

				// Insert the replacement
				auto const& fullMatch = match.str(0);
				auto startIndex = repl.find(fullMatch);

				auto endIndex = startIndex + fullMatch.length();
				string pre = repl.substr(0, startIndex);
				string post = repl.substr(endIndex);

				res += pre + replacement;
				repl = post;
			}

			res += repl;
			return res;
		}

		/*
		 * Parse source for basic information.
		 *
		 */
		void Parser::parseSource(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];
			
			// Find entry point
			regex entryPointRegex("void\\smain");
			smatch stringMatch;

			if (regex_search(stage.source, stringMatch, entryPointRegex))
			{
				if (stringMatch.size() > 1)
				{
					addError(stageType, "multiple 'main' definitions found.");
				}

				stage.mainLine = 0;
				int stringPos = (int)stringMatch.position(0);
				while (stringPos >= 0)
				{
					if (stage.source[stringPos] == '\n' || stage.source[stringPos] == ';')
					{
						stage.mainLine++;
					}

					stringPos--;
				}
			}
			else
			{
				addError(stageType, "no 'main' definition found.");
			}
		}

		/*
		 * Get uniform data.
		 *
		 */
		void Parser::parseUniformUsage(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			auto lines = utils::StringUtils::split(stage.source, "\n");

			regex re(R"((.*)?@@Uniform\s*\(\s*([\w\d]+\s+)([\w\d\[\]]+)\s*\))");
			smatch match;

			regex blockMatchRe(R"(.*uniform\s+[\w\d]+.*)");

			stage.uniforms.clear();

			bool looking{ false }, inBlock{ false };
			for (auto const& line: lines)
			{
				auto src = line;

				// See whether we're starting a block: expecting
				// "uniform <name> {" to start block and then the next "}" to end it
				// After "uniform <name>", see what comes next, "{", ";" or "=".
				if (regex_match(src, blockMatchRe))
				{
					auto braceIndex = src.find_first_of("{");
					auto semiIndex = src.find_first_of(";");
					auto equalsIndex = src.find_first_of("=");

					if (braceIndex < semiIndex && braceIndex < equalsIndex)
					{
						inBlock = true;
						looking = false;
					}
					else
					{
						looking = true;
					}
				}
				else if (looking)
				{
					auto braceIndex = src.find_first_of("{");
					auto semiIndex = src.find_first_of(";");
					auto equalsIndex = src.find_first_of("=");

					if (braceIndex < semiIndex && braceIndex < equalsIndex)
					{
						inBlock = true;
						looking = false;
					}
				}
				if (inBlock && src.find_first_of('}') != string::npos)
				{
					inBlock = false;
				}

				if (regex_search(src, match, re))
				{
					auto qualifier = utils::StringUtils::trim(match.str(1));
					auto type = utils::StringUtils::trim(match.str(2));
					auto name = utils::StringUtils::trim(match.str(3));

					// Get count and modify name according
					size_t count = 1;

					auto firstBrace = name.find_first_of('[');
					auto secondBrace = name.find_first_of(']');
					if (firstBrace != string::npos && secondBrace != string::npos)
					{ 
						count = utils::StringUtils::parseInt(name.substr(firstBrace + 1, secondBrace - firstBrace - 1));
						name = name.substr(0, firstBrace);
					}

					// If count is greater than 1, then uniforms will be expanded, eg
					// LIGHTS[2] will create uniforms LIGHTS[0] and LIGHTS[1]
					// TODO: what happens with LIGHTS[1]?  Is it valid, and if so, does
					// it expandto LIGHTS[0]?

					auto typeIt = gsGLSLTypeDecls.find(type);
					if (typeIt != gsGLSLTypeDecls.end())
					{
						stage.uniforms.push_back({ qualifier, name, typeIt->second, count, inBlock });
					}
					else
					{
						stage.uniforms.push_back({ qualifier, name, gsGLSLTypeDecls[""], count, inBlock });
					}
				}
			}
		}

		/*
		 * Get texture data.
		 *
		 */
		void Parser::parseTextureUsage(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			regex re(R"(@@Texture\s*\(\s*([\w\d]+\s+)([\w\d\[\]]+)\s*\))");
			smatch match;

			stage.textures.clear();

			auto src = stage.source;
			while (regex_search(src, match, re))
			{
				auto type = utils::StringUtils::trim(match.str(1));
				auto name = utils::StringUtils::trim(match.str(2));

				// Get count and modify name according
				size_t count = 1;

				auto firstBrace = name.find_first_of('[');
				auto secondBrace = name.find_first_of(']');
				if (firstBrace != string::npos && secondBrace != string::npos)
				{
					count = utils::StringUtils::parseInt(name.substr(firstBrace + 1, secondBrace - firstBrace - 1));
					name = name.substr(0, firstBrace);
				}

				stage.textures.push_back({ name, gsGLSLTypeDecls[type], count });

				// Look for next match
				src = match.suffix().str();
			}
		}

		/*
		 * Set in attributes based on mesh specification.
		 *
		 */
		void Parser::setInAttributesToMeshSpecification(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			stage.inAttribs.clear();

			for (size_t i = 0; i < mSpecification.getNumVertexBufferAttributeLayouts(); ++i)
			{
				auto const& layout = mSpecification.getVertexBufferAttributeLayout((uint32_t)i);
				for (size_t j = 0; j < layout.getNumAttributes(); ++j)
				{
					auto const& meshAttrib = layout.getAttribute(j);
					size_t size[2];

					switch (meshAttrib.component)
					{
					case mesh::Vertex::Component::Colour1:
					case mesh::Vertex::Component::UserDefined1:
						size[0] = 1; size[1] = 1;
						break;
					case mesh::Vertex::Component::Position2:
					case mesh::Vertex::Component::TexCoord2:
					case mesh::Vertex::Component::UserDefined2:
						size[0] = 1; size[1] = 2;
						break;
					case mesh::Vertex::Component::Position3:
					case mesh::Vertex::Component::Normal3:
					case mesh::Vertex::Component::TexCoord3:
					case mesh::Vertex::Component::Colour3:
					case mesh::Vertex::Component::UserDefined3:
						size[0] = 1; size[1] = 3;
						break;
					case mesh::Vertex::Component::Position4:
					case mesh::Vertex::Component::Normal4:
					case mesh::Vertex::Component::TexCoord4:
					case mesh::Vertex::Component::Tangent4:
					case mesh::Vertex::Component::Colour4:
					case mesh::Vertex::Component::UserDefined4:
						size[0] = 1; size[1] = 4;
						break;
					}

					Attribute inAttrib;
					
					inAttrib.name = meshAttrib.identifier;
					inAttrib.normalised = meshAttrib.normalised;
					inAttrib.type = gsGLSLTypeDecls[inAttrib.getGlslType(meshAttrib.dataType, size)];
					
					stage.inAttribs.push_back(inAttrib);
				}
			}
		}

		/*
		 * Set in attributes to previous stage's out attributes.
		 *
		 */
		void Parser::setInAttributesToPreviousStage(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			int prevStage = (int)stageType - 1;
			while (!mStages[prevStage].provided())
			{
				prevStage--;
			}

			stage.inAttribs = mStages[prevStage].outAttribs;
		}

		/*
		 * Set out attributes based on how they are used in the shader.
		 *
		 */
		void Parser::setOutAttributesToUsage(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			stage.outAttribs.clear();

			regex re(R"(@Out\s*\(\s*([\w\d]+\s+)?([\w\d]+)\s*\))");
			smatch match;

			auto src = stage.source;
			while (regex_search(src, match, re))
			{
				if (match.size() == 2)
				{
					auto attrib = utils::StringUtils::trim(match.str(1));

					// Type not defined: see if it's already been so.
					if (stage.outAttributeExists(attrib))
					{
						goto next_match;
					}
					else
					{
						addError(stageType, "out-attribute '" + attrib + "' has no type declaration.");
					}
				}
				else
				{
					auto type = utils::StringUtils::trim(match.str(1));
					auto attrib = utils::StringUtils::trim(match.str(2));

					// Type not defined: see if it's already been so.
					if (type == "")
					{
						if (stage.outAttributeExists(attrib))
						{
							goto next_match;
						}
						else
						{
							addError(stageType, "out-attribute '" + attrib + "' has no type declaration.");
						}
					}

					Attribute outAttrib;

					outAttrib.name = attrib;
					outAttrib.type = gsGLSLTypeDecls[type];

					stage.outAttribs.push_back(outAttrib);
				}

				// Look for next match
				next_match: src = match.suffix().str();
			}
		}

		/*
		 * Get attributes used from the source code.
		 *
		 */
		void Parser::parseInAttributeUsage(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];

			regex re(R"(@In\s*\(\s*([\w\d]+)\s*\))");
			smatch match;

			set<string> usedAttribs;
			auto src = stage.source;
			while (regex_search(src, match, re))
			{
				auto attrib = match.str(1);
				usedAttribs.insert(attrib);

				// Look for next match
				src = match.suffix().str();
			}

			for (auto const& attrib: stage.inAttribs)
			{
				if (usedAttribs.find(attrib.name) == usedAttribs.end())
				{
					addWarning(stageType, "in-attribute '" + attrib.name + "' is not used.");
				}
			}
		}


		/*
		 * Split into lines by statement and newline
		 *
		 */
		vector<string> Parser::splitSourceIntoLines(string const& src)
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
		 * Determine whether or not to write a shader line based on this fragment.
		 *
		 */
		bool Parser::evaluateShaderDirective(string const& expression, set<string> const& attribs)
		{
			bool invert = false;
			string trimmedLine = utils::StringUtils::trim(expression);

			if (trimmedLine[0] == '!')
			{
				trimmedLine = utils::StringUtils::trim(trimmedLine.substr(1));
				if (trimmedLine == "")
				{
					THROW_MPP_PROGRAM("Cannot have ! without an attribute in shader template.", __LINE__, __FILE__, __func__);
				}

				invert = true;
			}

			return (attribs.find(trimmedLine) != attribs.end()) != invert;
		}

		bool Parser::processShaderLine(string const& lineFragment, set<string> const& attribs, bool prev)
		{
			if (lineFragment == "Else")
			{
				return !prev;
			}

			uint32_t orPos = (uint32_t)lineFragment.find_first_of('|');
			uint32_t andPos = (uint32_t)lineFragment.find_first_of('&');

			if (orPos == -1 && andPos == -1)
			{
				return evaluateShaderDirective(lineFragment, attribs);
			}
			else if (orPos < andPos)
			{
				bool left = evaluateShaderDirective(lineFragment.substr(0, orPos), attribs);
				bool right = processShaderLine(lineFragment.substr(orPos + 1), attribs, prev);
				return left || right;
			}
			else if (andPos < orPos)
			{
				bool left = evaluateShaderDirective(lineFragment.substr(0, andPos), attribs);
				bool right = processShaderLine(lineFragment.substr(andPos + 1), attribs, prev);
				return left && right;
			}
			else
			{
				THROW_MPP_PROGRAM("Could not evaluate ## directive in shader template.", __LINE__, __FILE__, __func__);
			}
		}

		/*
		 * Generate shader for given attributes.
		 *
		 */
		void Parser::processConditionals(ShaderStage::Type stageType, set<string> const& attribs)
		{
			auto& stage = mStages[(int)stageType];

			stringstream ss(stage.source);
			string line, output;

			bool writeLine = true;
			while (getline(ss, line, '\n'))
			{
				string trimmedLine = utils::StringUtils::trim(as_const(line));
				if (trimmedLine.size() < 2 && writeLine)
				{
					output += line + "\n";
				}
				else if (trimmedLine[0] == '#' && trimmedLine[1] == '#')
				{
					trimmedLine = utils::StringUtils::trim(trimmedLine.substr(2));
					writeLine = trimmedLine != "" ? processShaderLine(trimmedLine, attribs, writeLine) : true;
				}
				else if (writeLine)
				{
					output += line + "\n";
				}
			}

			stage.source = output;
		}

		/*
		 * Do token replacement, and add uniform and attribute definitions.
		 *
		 */
		void Parser::generateShader(ShaderStage::Type stageType)
		{
			auto& stage = mStages[(int)stageType];
			bool usingGeometryShader = mStages[(int)ShaderStage::Type::Geometry].provided();
			
			stage.generated.clear();

			// Check for special uniforms
			bool vpUsed = stage.source.find(MPP_PROGRAM_VIEWPOS_TOKEN) != string::npos;
			bool mUsed = stage.source.find(MPP_PROGRAM_MMATRIX_TOKEN) != string::npos;
			bool mcpUsed = stage.source.find(MPP_PROGRAM_MCPMATRIX_TOKEN) != string::npos;
			bool normalUsed = stage.source.find(MPP_PROGRAM_NORMALMATRIX_TOKEN) != string::npos;
			bool halfWindowSizeUsed = stage.source.find(MPP_PROGRAM_HALFWINDOWSIZE_TOKEN) != string::npos;
			bool pointSizeUsed = stage.source.find(MPP_PROGRAM_POINTSIZE_TOKEN) != string::npos;
			
			// Parse line by line
			auto lines = splitSourceIntoLines(stage.source);
			list<string> parsedLines;
			bool versionFound{ false };
			for (auto const& line: lines)
			{
				auto trimmed = line;
				utils::StringUtils::trim(trimmed);

				// If it's semicolon or '\n', ignore
				if (trimmed == "" || line == ";" || line == "\n")
				{
					parsedLines.push_back(line);
					continue;
				}

				if (trimmed == "@@Version")
				{
					versionFound = true;

					parsedLines.push_back("#version 440");
					parsedLines.push_back("\n");
					parsedLines.push_back("\n");

					// Add in attributes
					int location = 0;
					for (auto const& attrib: stage.inAttribs)
					{
						string attribLine = std::format("layout(location = {}) in {} {};",
							location, 
							attrib.type.name,
							MPP_PROGRAM_IN_PREFIX + attrib.name);

						parsedLines.push_back(attribLine);
						parsedLines.push_back("\n");
						location += (int)attrib.type.size[0];
					}

					parsedLines.push_back("\n");

					// Add out attributes
					location = 0;
					for (auto const& attrib: stage.outAttribs)
					{
						string attribLine = std::format("layout(location = {}) out {} {};",
							location,
							attrib.type.name,
							MPP_PROGRAM_OUT_PREFIX + attrib.name);

						parsedLines.push_back(attribLine);
						parsedLines.push_back("\n");
						location += (int)attrib.type.size[0];
					}

					parsedLines.push_back("\n");
				
					// Add built-in uniforms
					if (vpUsed)
					{
						parsedLines.push_back(std::format("uniform vec3 {};", MPP_PROGRAM_VIEWPOS_NAME));
						parsedLines.push_back("\n");
					}
					if (mUsed)
					{
						parsedLines.push_back(std::format("uniform mat4 {};", MPP_PROGRAM_MMATRIX_NAME));
						parsedLines.push_back("\n");
					}
					if (mcpUsed)
					{
						parsedLines.push_back(std::format("uniform mat4 {};", MPP_PROGRAM_MCPMATRIX_NAME));
						parsedLines.push_back("\n");
					}
					if (normalUsed)
					{
						parsedLines.push_back(std::format("uniform mat3 {};", MPP_PROGRAM_NORMALMATRIX_NAME));
						parsedLines.push_back("\n");
					}
					if (halfWindowSizeUsed)
					{
						parsedLines.push_back(std::format("uniform vec2 {};", MPP_PROGRAM_HALFWINDOWSIZE_NAME));
						parsedLines.push_back("\n");
					}
					if (pointSizeUsed)
					{
						parsedLines.push_back(std::format("uniform float {};", MPP_PROGRAM_POINTSIZE_NAME));
						parsedLines.push_back("\n");
					}
				}
				else
				{
					// Parse in attributes
					auto replaced = regex_replace(line, 
						regex(R"(@In\s*\(\s*([\w\d]+)\s*\))"), 
						MPP_PROGRAM_IN_PREFIX "$1");

					// Parse out attributes
					replaced = regex_replace(replaced, 
						regex(R"(@Out\s*\(\s*([\w\d]+\s+)?([\w\d]+)\s*\))"), 
						MPP_PROGRAM_OUT_PREFIX "$2");

					// Parse built-in uniforms
					utils::StringUtils::replaceAll(replaced, "@ViewPos", MPP_PROGRAM_VIEWPOS_NAME);
					utils::StringUtils::replaceAll(replaced, "@MMatrix", MPP_PROGRAM_MMATRIX_NAME);
					utils::StringUtils::replaceAll(replaced, "@MCPMatrix", MPP_PROGRAM_MCPMATRIX_NAME);
					utils::StringUtils::replaceAll(replaced, "@NormalMatrix", MPP_PROGRAM_NORMALMATRIX_NAME);
					utils::StringUtils::replaceAll(replaced, "@HalfWindowSize", MPP_PROGRAM_HALFWINDOWSIZE_NAME);
					utils::StringUtils::replaceAll(replaced, "@PointSize", MPP_PROGRAM_POINTSIZE_NAME);

					// Parse user-defined uniforms
					replaced = replaceUniformDeclaration(stage.type, replaced);
					replaced = replaceUniformUsage(stage.type, replaced);
					replaced = replaceTextureDeclaration(stage.type, replaced);
					replaced = replaceTextureUsage(stage.type, replaced);


					// Add line
					parsedLines.push_back(replaced);
				}
			}

			if (!versionFound)
			{
				addError(stageType, "no @@Version directive found.");
			}

			// Now look for @Vec2/3/4, joining the lines as these expressions may be split across
			// multiple lines.
			string joined = utils::StringUtils::join(parsedLines.begin(), parsedLines.end(), "");

			stage.generated = replaceCasts(stageType, joined);
		}

		/*
		 * Parse files and get information, check against spec, and build final sources.
		 *
		 */
		void Parser::build(set<string> const& attribs)
		{
			// Clear
			for (int i = 0; i < (int)ShaderStage::Type::NumStages; ++i)
			{
				mStages[i].clear();
			}

			mErrors.clear();
			mWarnings.clear();

			// Check required stages are present
			if (mStages[(int)ShaderStage::Type::Vertex].required() && !mStages[(int)ShaderStage::Type::Vertex].provided())
			{
				THROW_MPP_PROGRAM("No vertex shader was given for program '" + mName + "'.", __LINE__, __FILE__, __func__);
			}
			if (mStages[(int)ShaderStage::Type::Geometry].required() && !mStages[(int)ShaderStage::Type::Geometry].provided())
			{
				THROW_MPP_PROGRAM("No geometry shader was given for program '" + mName + "'.", __LINE__, __FILE__, __func__);
			}
			if (mStages[(int)ShaderStage::Type::Fragment].required() && !mStages[(int)ShaderStage::Type::Fragment].provided())
			{
				THROW_MPP_PROGRAM("No fragment shader was given for program '" + mName + "'.", __LINE__, __FILE__, __func__);
			}

			// Set shader attributes and uniforms
			processConditionals(ShaderStage::Type::Vertex, attribs);
			setInAttributesToMeshSpecification(ShaderStage::Type::Vertex);
			setOutAttributesToUsage(ShaderStage::Type::Vertex);
			parseInAttributeUsage(ShaderStage::Type::Vertex);
			parseUniformUsage(ShaderStage::Type::Vertex);
			parseTextureUsage(ShaderStage::Type::Vertex);

			if (mStages[(int)ShaderStage::Type::Geometry].provided())
			{
				processConditionals(ShaderStage::Type::Geometry, attribs);
				setInAttributesToPreviousStage(ShaderStage::Type::Geometry);
				setOutAttributesToUsage(ShaderStage::Type::Geometry);
				parseInAttributeUsage(ShaderStage::Type::Geometry);
				parseUniformUsage(ShaderStage::Type::Geometry);
				parseTextureUsage(ShaderStage::Type::Geometry);
			}

			processConditionals(ShaderStage::Type::Fragment, attribs);
			setInAttributesToPreviousStage(ShaderStage::Type::Fragment);
			setOutAttributesToUsage(ShaderStage::Type::Fragment);
			parseInAttributeUsage(ShaderStage::Type::Fragment);
			parseUniformUsage(ShaderStage::Type::Fragment);
			parseTextureUsage(ShaderStage::Type::Fragment);

			// Generate shaders
			generateShader(ShaderStage::Type::Vertex);

			if (mStages[(int)ShaderStage::Type::Geometry].provided())
			{
				 generateShader(ShaderStage::Type::Geometry);
			}

			generateShader(ShaderStage::Type::Fragment);
		}

		ShaderStage const& Parser::getStage(uint32_t index) const
		{
			return mStages[index];
		}

		mesh::MeshSpecification const& Parser::getMeshSpecification() const
		{
			return mSpecification;
		}

		string const& Parser::getInputVertexSource() const
		{
			return mStages[(int)ShaderStage::Type::Vertex].inputSource;
		}

		string const& Parser::getInputGeometrySource() const
		{
			return mStages[(int)ShaderStage::Type::Geometry].inputSource;
		}

		string const& Parser::getInputFragmentSource() const
		{
			return mStages[(int)ShaderStage::Type::Fragment].inputSource;
		}

		string const& Parser::getGeneratedVertexSource() const
		{
			return mStages[(int)ShaderStage::Type::Vertex].generated;
		}

		string const& Parser::getGeneratedGeometrySource() const
		{
			return mStages[(int)ShaderStage::Type::Geometry].generated;
		}

		string const& Parser::getGeneratedFragmentSource() const
		{
			return mStages[(int)ShaderStage::Type::Fragment].generated;
		}

		vector<Attribute> Parser::getInAttributes() const
		{
			return mStages[(int)ShaderStage::Type::Vertex].inAttribs;
		}

		vector<string> Parser::getUniforms() const
		{
			set<string> uniformSet;

			for (int i = 0; i < (int)ShaderStage::Type::NumStages; ++i)
			{
				if (mStages[i].provided())
				{
					for (auto const& uniform : mStages[i].uniforms)
					{
						uniformSet.insert(uniform.name);
					}
				}
			}
			
			return vector<string>(uniformSet.begin(), uniformSet.end());
		}

		vector<string> Parser::getTextures() const
		{
			set<string> textureSet;

			for (int i = 0; i < (int)ShaderStage::Type::NumStages; ++i)
			{
				if (mStages[i].provided())
				{
					for (auto const& texture: mStages[i].textures)
					{
						textureSet.insert(texture.name);
					}
				}
			}

			return vector<string>(textureSet.begin(), textureSet.end());
		}

		vector<string> const& Parser::getErrors() const
		{
			return mErrors;
		}

		vector<string> const& Parser::getWarnings() const
		{
			return mWarnings;
		}

	}
}