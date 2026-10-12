#include <format>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/gl.h>

#include <cassert>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstddef>
#include <string>
#include <regex>
#include <set>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

#include "utils/StringUtils.h"

#include "mpp/Config.h"
#include "mpp/ParticleSystem.h"
#include "mpp/ParticleShaders.h"
#include "mpp/TrailSystem.h"
#include "mpp/RenderSystem.h"
#include "mpp/Camera.h"
#include "mpp/ResourceManager.h"
#include "mpp/Screen.h"
#include "mpp/InternalFont.h"
#include "mpp/Program.h"
#include "mpp/ProgramStream.h"
#include "mpp/Texture.h"
#include "mpp/TextureStream.h"
#include "mpp/Model.h"
#include "mpp/ModelStream.h"
#include "mpp/ProgrammaticModelStream.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/ProgrammaticRenderTextureStream.h"
#include "mpp/RenderTexture.h"
#include "mpp/DefaultShaders.h"
#include "mpp/Profiler.h"
#include "mpp/MeshSortFlags.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/GpuDebugScope.h"

using namespace std;

namespace mpp
{
	namespace
	{
		struct alignas(16) ShadowFrameData
		{
			glm::mat4 lightViewProjection{ 1.0f };
			glm::vec4 mapTexelSizeAndRadius{ 0.0f };
			glm::vec4 biasAndEnabled{ 0.0f };
			glm::vec4 pointPositionAndRange{ 0.0f };
			glm::vec4 shadowTypeAndLightIndex{ 0.0f };
		};
		static_assert(offsetof(ShadowFrameData, lightViewProjection) == 0);

		struct ColouredGlyph
		{
			uint8_t character;
			Colour colour;
		};

		GLenum graphCompareOp(GraphCompareOp value)
		{
			switch (value) { case GraphCompareOp::Never: return GL_NEVER; case GraphCompareOp::Less: return GL_LESS; case GraphCompareOp::Equal: return GL_EQUAL; case GraphCompareOp::LessEqual: return GL_LEQUAL; case GraphCompareOp::Greater: return GL_GREATER; case GraphCompareOp::NotEqual: return GL_NOTEQUAL; case GraphCompareOp::GreaterEqual: return GL_GEQUAL; default: return GL_ALWAYS; }
		}

		GLenum graphBlendOp(GraphBlendOp value)
		{
			switch (value) { case GraphBlendOp::Add: return GL_FUNC_ADD; case GraphBlendOp::Subtract: return GL_FUNC_SUBTRACT; case GraphBlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT; case GraphBlendOp::Minimum: return GL_MIN; default: return GL_MAX; }
		}

		GLenum graphBlendFactor(GraphBlendFactor value)
		{
			switch (value) { case GraphBlendFactor::Zero: return GL_ZERO; case GraphBlendFactor::One: return GL_ONE; case GraphBlendFactor::SourceColour: return GL_SRC_COLOR; case GraphBlendFactor::OneMinusSourceColour: return GL_ONE_MINUS_SRC_COLOR; case GraphBlendFactor::DestinationColour: return GL_DST_COLOR; case GraphBlendFactor::OneMinusDestinationColour: return GL_ONE_MINUS_DST_COLOR; case GraphBlendFactor::SourceAlpha: return GL_SRC_ALPHA; case GraphBlendFactor::OneMinusSourceAlpha: return GL_ONE_MINUS_SRC_ALPHA; case GraphBlendFactor::DestinationAlpha: return GL_DST_ALPHA; default: return GL_ONE_MINUS_DST_ALPHA; }
		}

		GraphBlendFactor graphBlendFactor(BlendMode value)
		{
			switch (value) { case BlendMode::Zero: return GraphBlendFactor::Zero; case BlendMode::One: return GraphBlendFactor::One; case BlendMode::SrcColour: return GraphBlendFactor::SourceColour; case BlendMode::OneMinusSrcColour: return GraphBlendFactor::OneMinusSourceColour; case BlendMode::DstColour: return GraphBlendFactor::DestinationColour; case BlendMode::OneMinusDstColour: return GraphBlendFactor::OneMinusDestinationColour; case BlendMode::SrcAlpha: return GraphBlendFactor::SourceAlpha; case BlendMode::OneMinusSrcAlpha: return GraphBlendFactor::OneMinusSourceAlpha; case BlendMode::DstAlpha: return GraphBlendFactor::DestinationAlpha; default: return GraphBlendFactor::OneMinusDestinationAlpha; }
		}

		int hexDigit(char value)
		{
			if (value >= '0' && value <= '9') return value - '0';
			if (value >= 'A' && value <= 'F') return value - 'A' + 10;
			if (value >= 'a' && value <= 'f') return value - 'a' + 10;
			return -1;
		}

		vector<ColouredGlyph> parseColouredGlyphs(string const& text)
		{
			vector<ColouredGlyph> glyphs;
			glyphs.reserve(text.size());
			Colour colour = Colour::White;

			for (size_t index = 0; index < text.size();)
			{
				bool validTag = index + 10 < text.size() && text[index] == '[' && text[index + 1] == '#' && text[index + 10] == ']';
				uint8_t components[4]{};
				if (validTag)
				{
					for (size_t component = 0; component < 4; ++component)
					{
						int high = hexDigit(text[index + 2 + component * 2]);
						int low = hexDigit(text[index + 3 + component * 2]);
						if (high < 0 || low < 0)
						{
							validTag = false;
							break;
						}
						components[component] = static_cast<uint8_t>((high << 4) | low);
					}
				}

				if (validTag)
				{
					colour = Colour(
						components[0] / 255.0f,
						components[1] / 255.0f,
						components[2] / 255.0f,
						components[3] / 255.0f);
					index += 11;
					continue;
				}

				glyphs.push_back({ static_cast<uint8_t>(text[index]), colour });
				++index;
			}
			return glyphs;
		}
		static_assert(offsetof(ShadowFrameData, mapTexelSizeAndRadius) == 64);
		static_assert(offsetof(ShadowFrameData, biasAndEnabled) == 80);
		static_assert(offsetof(ShadowFrameData, pointPositionAndRange) == 96);
		static_assert(offsetof(ShadowFrameData, shadowTypeAndLightIndex) == 112);
		static_assert(sizeof(ShadowFrameData) == 128);
	}

	/*
	 * Constructor.
	 *
	 */
	RenderSystem::RenderSystem(size_t windowWidth, size_t windowHeight, Logger* logger, RenderSystemOptions options)
		: ResourceWrangler("RenderSystem")
		, mLogger(logger)
		, mOptions(std::move(options))
		, mWindowWidth(windowWidth)
		, mWindowHeight(windowHeight)
		, mViewportWidth(windowWidth)
		, mViewportHeight(windowHeight)
		, mResourceMgr(nullptr)
#ifdef MPP_PROFILE_BUILD
		, mProfiler(nullptr)
		, mProfileLines(nullptr)
		, mSamplesToRecord(300)
#endif
		, mRenderTarget(nullptr)
		, mScreen(nullptr)
		, mProjectionType(ProjectionType::Unknown)
		, mModelInstances(nullptr)
		, mMeshInstances(nullptr)
		, mGamma(2.2f)
		, mShowDebugPanel(false)
		, mTimeUnit(TimeUnit::Milliseconds)
		, mSizeUnit(SizeUnit::Megabytes)
		, mInternalFont(nullptr)
		, mInternalVBO(0)
		, mInternalIBO(0)
	{
		// Add scene factories
		mSceneFactories["Default"] = [this](RenderSystem* renderSystem) { return make_shared<Scene>(renderSystem); };

		// Initialise
		initialise();
	}

	/*
	 * Destructor.
	 *
	 */
	RenderSystem::~RenderSystem()
	{
		if (mInternalVBO != 0)
		{
			GL_CHECK(glDeleteBuffers(1, &mInternalVBO));
		}
		if (mInternalIBO != 0)
		{
			GL_CHECK(glDeleteBuffers(1, &mInternalIBO));
		}

		delete mModelInstances;
		delete mMeshInstances;
		delete mInternalFont;
		// Ahead of the core-resource sweep: this releases the programs it acquired
		// and deletes its GPU objects while the context is still current.
		mTrailSystem.reset();
		mParticleSystem.reset();
		destroyLightsData();
		destroyPbrLightsData();
		destroyCameraFrameData();
		destroyShadowDomains();

		// Deleting the above may have freed up more resources, so sweep once more
		for (auto res : mCoreResources)
		{
			if (!res->isReferenced())
			{
				res->destroy();
			}
		}

#ifdef MPP_PROFILE_BUILD
		delete mProfiler;
		delete mProfileLines;
#endif
	}

	/*
	 * Callback for ARB_debug_output
	 *
	 */
	void GLAPIENTRY debugOutputCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
	                                  GLsizei length, const GLchar* message, GLvoid const* userParam)
	{
		RenderSystem* renderSystem = const_cast<RenderSystem*>((RenderSystem const*)(userParam));

		string msg(message);
		
		// Tooling event markers and groups are capture annotations, not errors.
		// RenderDoc still receives them; suppress them from the engine error log.
		if (type == GL_DEBUG_TYPE_MARKER || type == GL_DEBUG_TYPE_PUSH_GROUP || type == GL_DEBUG_TYPE_POP_GROUP)
		{
			return;
		}

		// Ignore verbose driver information.
		if (msg.find("Buffer detailed info") != string::npos)
		{
			return;
		}

		string strMsg;
		switch (source)
		{
		case GL_DEBUG_SOURCE_API_ARB:
			strMsg = "OpenGL API error :: ";
			break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
			strMsg = "OpenGL window error :: ";
			break;

		case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
			strMsg = "OpenGL compiler error :: ";
			break;

		case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:
			strMsg = "OpenGL 3rd party error :: ";
			break;

		case GL_DEBUG_SOURCE_APPLICATION_ARB:
			strMsg = "OpenGL application error :: ";
			break;

		case GL_DEBUG_SOURCE_OTHER_ARB:
			strMsg = "OpenGL generic error :: ";
			break;

		default:
			strMsg = "OpenGL error (unknown source) :: ";
			break;
		}

		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR_ARB:
			strMsg += "error :: ";
			break;

		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
			strMsg += "deprecated behaviour :: ";
			break;

		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
			strMsg += "undefined behaviour :: ";
			break;
			
		case GL_DEBUG_TYPE_PORTABILITY_ARB:
			strMsg += "portability issue :: ";
			break;

		case GL_DEBUG_TYPE_PERFORMANCE_ARB:
			strMsg += "performance issue :: ";
			break;

		case GL_DEBUG_TYPE_OTHER_ARB:
		default:
			strMsg += "unknown type :: ";
			break;
		}

		strMsg += msg;
		renderSystem->errorMessage(strMsg);
	}

	/*
	 * Get window width.
	 *
	 */
	size_t RenderSystem::getWindowWidth() const
	{
		return mWindowWidth;
	}

	/*
	 * Get window height.
	 *
	 */
	size_t RenderSystem::getWindowHeight() const
	{
		return mWindowHeight;
	}

	/*
	 * Get window aspect ratio.
	 *
	 */
	float RenderSystem::getAspectRatio() const
	{
		return (float)mViewportWidth / (float)mViewportHeight;
	}
	
	/*
	 * Get caps.
	 *
	 */
	Caps const& RenderSystem::getCaps() const
	{
		return mCaps;
	}

	/*
	 * Initialise OpenGL.
	 *
	 */
	void RenderSystem::initialise()
	{
		glewExperimental = GL_TRUE; // for GL 3.2 and up.
		GLenum err = glewInit();
		if (err != GLEW_OK)
		{
			string msg = "GLEW initialisation failed.  " + string((char*)glewGetErrorString(err));
			THROW_MPP(msg, __LINE__, __FILE__, __func__);
		}

		checkExtensions();
		checkCaps();

		auto requestedMsaa = antiAliasingSampleCount(mOptions.antiAliasing.msaa);
		if (!mCaps.supportsMsaa(requestedMsaa))
		{
			THROW_MPP("Configured [mpp] MSAA setting '" + antiAliasingSamplesName(mOptions.antiAliasing.msaa) + "' is not supported by this GPU.", __LINE__, __FILE__, __func__);
		}
		auto ssaaWidth = static_cast<uint64_t>(ssaaDimension((uint32_t)mWindowWidth,mOptions.antiAliasing.ssaa));
		auto ssaaHeight = static_cast<uint64_t>(ssaaDimension((uint32_t)mWindowHeight,mOptions.antiAliasing.ssaa));
		if (ssaaWidth > static_cast<uint64_t>(mCaps.maxTextureSize) || ssaaHeight > static_cast<uint64_t>(mCaps.maxTextureSize))
		{
			THROW_MPP("Configured [mpp] SSAA setting '" + antiAliasingSamplesName(mOptions.antiAliasing.ssaa) + "' requires " + std::to_string(ssaaWidth) + "x" + std::to_string(ssaaHeight) + ", exceeding the GPU maximum texture size of " + std::to_string(mCaps.maxTextureSize) + ".", __LINE__, __FILE__, __func__);
		}
		infoMessage("Configured anti-aliasing defaults: MSAA=" + antiAliasingSamplesName(mOptions.antiAliasing.msaa) +
			", SSAA=" + antiAliasingSamplesName(mOptions.antiAliasing.ssaa) +
			", TAA=" + std::string(mOptions.antiAliasing.taa ? "true" : "false") +
			", FXAA=" + std::string(mOptions.antiAliasing.fxaa ? "true" : "false"));

#ifdef MPP_PROFILE_BUILD
		mProfiler = new Profiler();
#endif

		// Pools for rendering objects
		delete mModelInstances;
		mModelInstances = new Pool<ModelInstance>(32);

		delete mMeshInstances;
		mMeshInstances = new Pool<MeshInstance>(64);

		// Set state and display
		setDefaultState();
		setDisplay((int)mWindowWidth, (int)mWindowHeight);
		createLightsData();
		createPbrLightsData();
		createCameraFrameData();

		// Text settings
		mTextUniforms = make_shared<UniformCollection>();
		mTextUniforms->setUniform("COLOUR", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

		mTextParams = make_shared<ModelRenderParams>();
		mTextParams->setModelUniforms(mTextUniforms);
		mTextParams->setModelPointSize(16.0f);

		// Internal buffer objects
		GL_CHECK(glGenBuffers(1, &mInternalVBO));
		GL_CHECK(glGenBuffers(1, &mInternalIBO));
	}

	/*
	 * Toggle debug panel.
	 *
	 */
	void RenderSystem::showDebugPanel(bool show, TimeUnit timeUnit, SizeUnit sizeUnit)
	{
		mShowDebugPanel = show;
		mTimeUnit = timeUnit;
		mSizeUnit = sizeUnit;

#ifdef MPP_PROFILE_BUILD
		mProfileTimeSamples.clear();
#endif
	}

	/*
	 * Is debug panel being rendered?
	 *
	 */	
	bool RenderSystem::isDebugPanelShown() const
	{
		return mShowDebugPanel;
	}

	/*
	 * Check that we have all the extensions that we require.
	 *
	 */
	void RenderSystem::checkExtensions()
	{
		// Check we have the extensions we need.
		if (!GLEW_VERSION_3_2)
		{
			THROW_MPP("OpenGL 3.2 was not found.", __LINE__, __FILE__, __func__);
		}


		int NumberOfExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &NumberOfExtensions);
		for (int i = 0; i < NumberOfExtensions; ++i)
		{
			GLubyte const* ccc = glGetStringi(GL_EXTENSIONS, i);
			string extensionName = string((char const*)ccc);
			if (extensionName == "GL_ARB_debug_output")
			{
#ifdef MPP_DEBUG_BUILD
				GL_CHECK(glDebugMessageCallbackARB(&debugOutputCallback, this));
#endif
				infoMessage("GL_ARB_debug_output initialised.");
			}
			else if (extensionName == "GL_EXT_texture_rectangle")
			{
				infoMessage("GL_EXT_texture_rectangle initialised.");
			}
			else if (extensionName == "GLEW_ARB_buffer_storage")
			{
				infoMessage("GLEW_ARB_buffer_storage initialised.");
			}
			else if (extensionName == "GLEW_ARB_map_buffer_range")
			{
				infoMessage("GLEW_ARB_map_buffer_range initialised.");
			}
		}

		/*
		if (GLEW_NVX_gpu_memory_info)
		{
			logMessage("Found 'NVX_gpu_memory_info' extension.");
		}
		*/
	}

	/*
	 *  Check capabilities, to determine how to render things.
	 *
	 */
	void RenderSystem::checkCaps()
	{
		// Get versions
		string glVersion((char*)glGetString(GL_VERSION));
		string glslVersion((char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
		string glRenderer((char*)glGetString(GL_RENDERER));
		string vendorInfo((char*)glGetString(GL_VENDOR));

		// GL_VERSION
		regex glVersionRe(R"(^(\d+)\.(\d+)\.?(\d+)?\s?(.*)?)");
		smatch matches;

		if (regex_search(glVersion, matches, glVersionRe))
		{
			string vMajor = matches[1];
			string vMinor = matches[2];
			string vRelease = matches[3];
			string vendorSpecific = matches[4];

			mCaps.glVersionMajor = utils::StringUtils::parseInt(vMajor);
			mCaps.glVersionMinor = utils::StringUtils::parseInt(vMinor);
		}
		else 
		{
			THROW_MPP("Could not parse GL_VERSION: " + glVersion, __LINE__, __FILE__, __func__);
		}

		// GL_SHADING_LANGUAGE_VERSION
		if (regex_search(glslVersion, matches, glVersionRe))
		{
			string vMajor = matches[1];
			string vMinor = matches[2];
			string vRelease = matches[3];
			string vendorSpecific = matches[4];

			mCaps.glslVersionMajor = utils::StringUtils::parseInt(vMajor);
			mCaps.glslVersionMinor = utils::StringUtils::parseInt(vMinor);
		}
		else
		{
			THROW_MPP("Could not parse GL_SHADING_LANGUAGE_VERSION: " + glslVersion, __LINE__, __FILE__, __func__);
		}

		infoMessage("GL version: " + glVersion);
		infoMessage("GLSL version: " + glslVersion);
		infoMessage("Renderer: " + glRenderer);
		infoMessage("Vendor: " + vendorInfo);

		// GL_CONTEXT_PROFILE_MASK is core since 3.2; anything older is compatibility
		// by definition.
		{
			GLint profileMask = 0;
			if (mCaps.glVersionMajor > 3 || (mCaps.glVersionMajor == 3 && mCaps.glVersionMinor >= 2))
			{
				GL_CHECK(glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask));
				mCaps.compatibilityProfile = (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) == 0;
			}
			else
			{
				mCaps.compatibilityProfile = true;
			}
		}

		// Get point size render range
		GLfloat sizeRange[2] = { 0.0f, 0.0f };
		GL_CHECK(glGetFloatv(GL_SMOOTH_POINT_SIZE_RANGE, sizeRange));

		mCaps.pointSizeRange[0] = sizeRange[0];
		mCaps.pointSizeRange[1] = sizeRange[1];

		// Get aliased line width range
		GLfloat lineRange[2] = { 0.0f, 0.0f };
		GL_CHECK(glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineRange));

		mCaps.aliasedLineWidthRange[0] = lineRange[0];
		mCaps.aliasedLineWidthRange[1] = lineRange[1];

		// Get texture info
		GLint maxTextureSize;
		GLint maxRectTextureSize = 0;
		GLint maxCubeMapTextureSize = 0;

		GL_CHECK(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize));
		GL_CHECK(glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapTextureSize));

		if (glewIsSupported("GL_EXT_texture_rectangle"))
		{
			GL_CHECK(glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT, &maxRectTextureSize));
		}

		mCaps.maxTextureSize = maxTextureSize;
		mCaps.maxRectTextureSize = maxRectTextureSize;
		mCaps.maxCubeMapTextureSize = maxCubeMapTextureSize;

		// Framebuffer/MRT limits.  These are required before a render graph may
		// turn a declared list of colour outputs into glDrawBuffers calls.
		GLint maxColourAttachments = 1;
		GLint maxDrawBuffers = 1;
		GLint maxSamples = 1;
		GL_CHECK(glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColourAttachments));
		GL_CHECK(glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers));
		GL_CHECK(glGetIntegerv(GL_MAX_SAMPLES, &maxSamples));
		mCaps.maxColourAttachments = (uint32_t)maxColourAttachments;
		mCaps.maxDrawBuffers = (uint32_t)maxDrawBuffers;
		mCaps.maxSamples = (uint32_t)maxSamples;
		mCaps.supportedMsaaSampleMask = 0;
		if (GLEW_VERSION_4_2 || GLEW_ARB_internalformat_query)
		{
			auto sampleMask = [](GLenum format)
			{
				GLint count = 0;
				GL_CHECK(glGetInternalformativ(GL_RENDERBUFFER, format, GL_NUM_SAMPLE_COUNTS, 1, &count));
				std::vector<GLint> samples(static_cast<size_t>(std::max(0, count)));
				if (count > 0) GL_CHECK(glGetInternalformativ(GL_RENDERBUFFER, format, GL_SAMPLES, count, samples.data()));
				uint32_t mask = 0;
				for (auto sample : samples) if (sample == 2 || sample == 4 || sample == 8) mask |= 1u << sample;
				return mask;
			};
			mCaps.supportedMsaaSampleMask = sampleMask(GL_RGBA8) & sampleMask(GL_DEPTH_COMPONENT24);
		}
		else
		{
			for (uint32_t sample : { 2u, 4u, 8u }) if (sample <= mCaps.maxSamples) mCaps.supportedMsaaSampleMask |= 1u << sample;
		}
		
		// Get depth range
		GLfloat depthRange[2] = { 0.0f, 0.0f };
		glGetFloatv(GL_DEPTH_RANGE, depthRange);

		mCaps.depthRange[0] = depthRange[0];
		mCaps.depthRange[1] = depthRange[1];

		// Primitive counts
		GLint maxElements, maxVertices;
		GL_CHECK(glGetIntegerv(GL_MAX_ELEMENTS_INDICES, &maxElements));
		GL_CHECK(glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &maxVertices));

		mCaps.maxRecommendedElements = maxElements;
		mCaps.maxRecommendedVertices = maxVertices;
		mCaps.maxElements = maxElements;

		// Streaming geometry
		// ARB_buffer_storage && ARB_map_buffer_range
		mCaps.streamingGeometry = (GLEW_VERSION_4_4 || GLEW_ARB_buffer_storage) && (GLEW_VERSION_3_0 || GLEW_ARB_map_buffer_range);

		// Filtering. GL_TEXTURE_MAX_ANISOTROPY is not legal to query or set
		// unless one of the anisotropic-filtering extensions is present.
		mCaps.maxAnisotropy = 1.0f;
		if (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic)
		{
			GLfloat maxAnisotropy = 1.0f;
			GL_CHECK(glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy));
			mCaps.maxAnisotropy = max(1.0f, maxAnisotropy);
		}

		// Uniform limits
		int maxUniforms;
		GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxUniforms));
		mCaps.maxVertexShaderUniforms = (uint32_t)maxUniforms;

		GL_CHECK(glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, &maxUniforms));
		mCaps.maxGeometryShaderUniforms = (uint32_t)maxUniforms;

		GL_CHECK(glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxUniforms));
		mCaps.maxFragmentShaderUniforms = (uint32_t)maxUniforms;

		// Texture limits
		GLint maxTextureUnits = 0;
		GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxTextureUnits));
		mCaps.maxVertexTextureUnits = (uint32_t)max(0, maxTextureUnits);

		GL_CHECK(glGetIntegerv(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, &maxTextureUnits));
		mCaps.maxGeometryTextureUnits = (uint32_t)max(0, maxTextureUnits);

		GL_CHECK(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits));
		mCaps.maxFragmentTextureUnits = (uint32_t)max(0, maxTextureUnits);

		// Vertex input limits used to reject layouts before glVertexAttrib* emits
		// a context-dependent error. The stride query was introduced with the
		// separate vertex-attrib binding API; 2048 is its required minimum and the
		// legacy glVertexAttribPointer limit on contexts without that query.
		GLint maxVertexAttributes = 0;
		GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttributes));
		mCaps.maxVertexAttributes = (uint32_t)max(0, maxVertexAttributes);
		mCaps.maxVertexAttributeStride = 2048;
		if (GLEW_VERSION_4_4 || GLEW_ARB_vertex_attrib_binding)
		{
			GLint maxVertexAttributeStride = 0;
			GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &maxVertexAttributeStride));
			mCaps.maxVertexAttributeStride = (uint32_t)max(0, maxVertexAttributeStride);
		}

		// Compute and shader-storage limits. Compute is queried rather than assumed
		// -- see Caps. MPP_FORCE_NO_COMPUTE_SUPPORT builds the unsupported path so
		// the graceful-degradation contract can be exercised on hardware that does
		// support it: the particle system then warns once and draws nothing.
#if defined(MPP_FORCE_NO_COMPUTE_SUPPORT)
		mCaps.supportsCompute = false;
#else
		mCaps.supportsCompute = GLEW_VERSION_4_3 ||
			(GLEW_ARB_compute_shader && GLEW_ARB_shader_storage_buffer_object && GLEW_ARB_draw_indirect);
#endif
		mCaps.supportsMultiDrawIndirect = GLEW_VERSION_4_3 || GLEW_ARB_multi_draw_indirect;
		mCaps.supportsBindlessTextures = GLEW_ARB_bindless_texture != 0;
		if (mCaps.supportsCompute)
		{
			GLint limit = 0;
			for (GLuint axis = 0; axis < 3; ++axis)
			{
				GL_CHECK(glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &limit));
				mCaps.maxComputeWorkGroupCount[axis] = (uint32_t)max(0, limit);
				GL_CHECK(glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, axis, &limit));
				mCaps.maxComputeWorkGroupSize[axis] = (uint32_t)max(0, limit);
			}
			GL_CHECK(glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &limit));
			mCaps.maxComputeWorkGroupInvocations = (uint32_t)max(0, limit);
			// The block-size query is a 64-bit limit in the specification; the 32-bit
			// entry point saturates at INT_MAX, which is far beyond any pool this
			// engine allocates and is what the limit is compared against.
			GL_CHECK(glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limit));
			mCaps.maxShaderStorageBlockSize = (uint32_t)max(0, limit);
			GL_CHECK(glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &limit));
			mCaps.maxShaderStorageBufferBindings = (uint32_t)max(0, limit);
		}

		// Print caps
		infoMessage(std::format("Supported point size range: {} to {}", mCaps.pointSizeRange[0], mCaps.pointSizeRange[1]));
		infoMessage(std::format("Supported aliased line width range: {} to {}", mCaps.aliasedLineWidthRange[0], mCaps.aliasedLineWidthRange[1]));
		infoMessage(std::format("Supported square texture size: {}x{}", mCaps.maxTextureSize, mCaps.maxTextureSize));
		infoMessage(std::format("Supported non-square texture size: {}x{}", mCaps.maxRectTextureSize, mCaps.maxRectTextureSize));
		infoMessage(std::format("Max colour attachments: {}", mCaps.maxColourAttachments));
		infoMessage(std::format("Max draw buffers: {}", mCaps.maxDrawBuffers));
		infoMessage(std::format("Max framebuffer samples: {}", mCaps.maxSamples));
		infoMessage(std::format("Supported anti-aliasing MSAA mask: 0x{:x}", mCaps.supportedMsaaSampleMask));
		infoMessage(std::format("Depth range: {} to {}", mCaps.depthRange[0], mCaps.depthRange[1]));
		infoMessage(std::format("Max anisotropy: {}", mCaps.maxAnisotropy));
		infoMessage(std::format("Max recommended elements: {}", mCaps.maxRecommendedElements));
		infoMessage(std::format("Max recommended vertices: {}", mCaps.maxRecommendedVertices));
		infoMessage(std::format("Streaming geometry: {}", mCaps.streamingGeometry ? "yes" : "no"));

		infoMessage(std::format("Max vertex shader uniforms: {}", mCaps.maxVertexShaderUniforms));
		infoMessage(std::format("Max geometry shader uniforms: {}", mCaps.maxGeometryShaderUniforms));
		infoMessage(std::format("Max fragment shader uniforms: {}", mCaps.maxFragmentShaderUniforms));

		infoMessage(std::format("Max vertex texture units: {}", mCaps.maxVertexTextureUnits));
		infoMessage(std::format("Max geometry texture units: {}", mCaps.maxGeometryTextureUnits));
		infoMessage(std::format("Max fragment texture units: {}", mCaps.maxFragmentTextureUnits));
		infoMessage(std::format("Max vertex attributes: {}", mCaps.maxVertexAttributes));
		infoMessage(std::format("Max vertex attribute stride: {} bytes", mCaps.maxVertexAttributeStride));

		infoMessage(std::format("Compute shaders: {}", mCaps.supportsCompute ? "yes" : "no"));
		infoMessage(std::format("Multi-draw indirect: {}", mCaps.supportsMultiDrawIndirect ? "yes" : "no"));
		infoMessage(std::format("Bindless textures: {}", mCaps.supportsBindlessTextures ? "yes" : "no"));
		if (mCaps.supportsCompute)
		{
			infoMessage(std::format("Max compute work group count: {}x{}x{}", mCaps.maxComputeWorkGroupCount[0], mCaps.maxComputeWorkGroupCount[1], mCaps.maxComputeWorkGroupCount[2]));
			infoMessage(std::format("Max compute work group size: {}x{}x{}", mCaps.maxComputeWorkGroupSize[0], mCaps.maxComputeWorkGroupSize[1], mCaps.maxComputeWorkGroupSize[2]));
			infoMessage(std::format("Max compute work group invocations: {}", mCaps.maxComputeWorkGroupInvocations));
			infoMessage(std::format("Max shader storage block size: {} bytes", mCaps.maxShaderStorageBlockSize));
			infoMessage(std::format("Max shader storage buffer bindings: {}", mCaps.maxShaderStorageBufferBindings));
		}
	}

	void RenderSystem::addCoreResource(ResourcePtr resource, bool load)
	{
		resource->acquire(this);
		if (load)
		{
			resource->load();
		}

		mCoreResources.push_back(resource);
	}

	/*
	 * Create core resources which the RenderSystem needs to operate on,
	 * outside of the client-specific resources.
	 *
	 */
	void RenderSystem::createCoreResources(ResourceManager* resourceMgr)
	{
		mResourceMgr = resourceMgr;

		// Cheap to construct: neither primitive allocates GPU state until a graph
		// draws it.
		mParticleSystem = make_unique<ParticleSystem>(this, resourceMgr);
		mTrailSystem = make_unique<TrailSystem>(this, resourceMgr);

		// Default 3d program
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::Normal3, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShader3dTemplate);
			parser->setFragmentSource(FragmentShader3dTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			mDefaultProgram3d = resourceMgr->declareResource("__mpp_p3d_tris_p3n3t2c4__", ResourceStreamPtr(ps)).first;
			addCoreResource(mDefaultProgram3d, true);
		}

		// Generic shadow caster program: position is the only material-independent
		// vertex requirement, so PBR and legacy meshes use the same depth pass.
		{
			mesh::MeshSpecification meshSpec;
			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();
			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderShadowDepthTemplate);
			parser->setFragmentSource(FragmentShaderShadowDepthTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			mShadowDepthProgram = resourceMgr->declareResource("__mpp_p3d_shadow_depth__", ResourceStreamPtr(ps)).first;
			addCoreResource(mShadowDepthProgram, true);

			mesh::MeshSpecification alphaMeshSpec;
			auto alphaLayout = alphaMeshSpec.createVertexBufferAttributeLayout(false);
			alphaLayout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
			alphaLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			auto alphaParser = make_shared<program::Parser>();
			alphaParser->setMeshSpecification(alphaMeshSpec);
			alphaParser->setVertexSource(VertexShaderAlphaShadowDepthTemplate);
			alphaParser->setFragmentSource(FragmentShaderAlphaShadowDepthTemplate);
			auto alphaStream = new ProgrammaticProgramStream(resourceMgr);
			alphaStream->setParser(alphaParser);
			mAlphaShadowDepthProgram = resourceMgr->declareResource("__mpp_p3d_alpha_shadow_depth__", ResourceStreamPtr(alphaStream)).first;
			addCoreResource(mAlphaShadowDepthProgram, true);

			auto pointParser = make_shared<program::Parser>();
			pointParser->setMeshSpecification(meshSpec);
			pointParser->setVertexSource(VertexShaderPointShadowDepthTemplate);
			pointParser->setFragmentSource(FragmentShaderPointShadowDepthTemplate);
			auto pointStream = new ProgrammaticProgramStream(resourceMgr);
			pointStream->setParser(pointParser);
			mPointShadowDepthProgram = resourceMgr->declareResource("__mpp_p3d_point_shadow_depth__", ResourceStreamPtr(pointStream)).first;
			addCoreResource(mPointShadowDepthProgram, true);

			auto pointAlphaParser = make_shared<program::Parser>();
			pointAlphaParser->setMeshSpecification(alphaMeshSpec);
			pointAlphaParser->setVertexSource(VertexShaderPointAlphaShadowDepthTemplate);
			pointAlphaParser->setFragmentSource(FragmentShaderPointAlphaShadowDepthTemplate);
			auto pointAlphaStream = new ProgrammaticProgramStream(resourceMgr);
			pointAlphaStream->setParser(pointAlphaParser);
			mPointAlphaShadowDepthProgram = resourceMgr->declareResource("__mpp_p3d_point_alpha_shadow_depth__", ResourceStreamPtr(pointAlphaStream)).first;
			addCoreResource(mPointAlphaShadowDepthProgram, true);
		}
		createShadowDisabledFrameBuffer();

		// 2d fullscreen program
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderFullscreenTemplate);
			parser->setFragmentSource(FragmentShaderFullscreenTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			mFullscreenProgram = resourceMgr->declareResource("__mpp_p2d_fullscreen__", ResourceStreamPtr(ps)).first;
			addCoreResource(mFullscreenProgram, true);
		}

		// HDR tone-map program used by the opt-in PBR preview pipeline.
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();
			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderFullscreenTemplate);
			parser->setFragmentSource(FragmentShaderToneMapTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			mToneMapProgram = resourceMgr->declareResource("__mpp_p2d_tonemap__", ResourceStreamPtr(ps)).first;
			addCoreResource(mToneMapProgram, true);
		}

		// Graph image diagnostic program for channel, depth, and HDR inspection.
		{
			mesh::MeshSpecification meshSpec;
			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			auto parser = make_shared<program::Parser>();
			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderFullscreenTemplate);
			parser->setFragmentSource(FragmentShaderTextureDiagnosticTemplate);
			auto stream = new ProgrammaticProgramStream(resourceMgr);
			stream->setParser(parser);
			mTextureDiagnosticProgram = resourceMgr->declareResource("__mpp_p2d_texture_diagnostic__", ResourceStreamPtr(stream)).first;
			addCoreResource(mTextureDiagnosticProgram, true);
		}

		// Fullscreen bloom programs. Targets are owned by RenderPipeline, while
		// these reusable programs perform extraction, separable blur, and combine.
		auto createBloomProgram = [&](string const& name, string const& fragmentSource)
		{
			mesh::MeshSpecification meshSpec;
			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			auto parser = make_shared<program::Parser>();
			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderFullscreenTemplate);
			parser->setFragmentSource(fragmentSource);
			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			return resourceMgr->declareResource(name, ResourceStreamPtr(ps)).first;
		};
		mBloomExtractProgram = createBloomProgram("__mpp_p2d_bloom_extract__", FragmentShaderBloomExtractTemplate);
		addCoreResource(mBloomExtractProgram, true);
		mBloomBlurProgram = createBloomProgram("__mpp_p2d_bloom_blur__", FragmentShaderBloomBlurTemplate);
		addCoreResource(mBloomBlurProgram, true);
		mBloomCombineProgram = createBloomProgram("__mpp_p2d_bloom_combine__", FragmentShaderBloomCombineTemplate);
		addCoreResource(mBloomCombineProgram, true);
		mParticleWeightedOitResolveProgram = createBloomProgram("__mpp_particle_weighted_oit_resolve__", ParticleWeightedOitResolveFragmentShader);
		// Keep particle GPU work lazy: graphs without a particle pass never compile
		// these composite programs.
		addCoreResource(mParticleWeightedOitResolveProgram, false);
		mParticleDistortionCompositeProgram = createBloomProgram("__mpp_particle_distortion_composite__", ParticleDistortionCompositeFragmentShader);
		addCoreResource(mParticleDistortionCompositeProgram, false);
		mSsaoRawProgram = createBloomProgram("__mpp_p2d_ssao_raw__", FragmentShaderSsaoRawTemplate);
		addCoreResource(mSsaoRawProgram, true);
		mGtaoRawProgram = createBloomProgram("__mpp_p2d_gtao_raw__", FragmentShaderGtaoRawTemplate);
		addCoreResource(mGtaoRawProgram, true);
		mSsaoBlurProgram = createBloomProgram("__mpp_p2d_ssao_blur__", FragmentShaderSsaoBlurTemplate);
		addCoreResource(mSsaoBlurProgram, true);
		mSsaoCombineProgram = createBloomProgram("__mpp_p2d_ssao_combine__", FragmentShaderSsaoCombineTemplate);
		addCoreResource(mSsaoCombineProgram, true);
		mSsaoCombineModulatedProgram = createBloomProgram("__mpp_p2d_ssao_combine_modulated__", FragmentShaderSsaoCombineModulatedTemplate);
		addCoreResource(mSsaoCombineModulatedProgram, true);
		mEnvironmentDebugCubeProgram = createBloomProgram("__mpp_p2d_environment_debug_cube__", FragmentShaderEnvironmentDebugCubeTemplate);
		addCoreResource(mEnvironmentDebugCubeProgram, true);
		mSsaaLanczosProgram = createBloomProgram("__mpp_p2d_ssaa_lanczos__", FragmentShaderSsaaLanczosTemplate);
		addCoreResource(mSsaaLanczosProgram, true);
		mTaaProgram = createBloomProgram("__mpp_p2d_taa__", FragmentShaderTaaTemplate);
		addCoreResource(mTaaProgram, true);
		mFxaaProgram = createBloomProgram("__mpp_p2d_fxaa__", FragmentShaderFxaaTemplate);
		addCoreResource(mFxaaProgram, true);
		mEquirectangularToCubemapProgram = createBloomProgram("__mpp_ibl_equirectangular_to_cubemap__", FragmentShaderEquirectangularToCubemapTemplate);
		addCoreResource(mEquirectangularToCubemapProgram, true);
		mDiffuseIrradianceProgram = createBloomProgram("__mpp_ibl_diffuse_irradiance__", FragmentShaderDiffuseIrradianceTemplate);
		addCoreResource(mDiffuseIrradianceProgram, true);
		mPrefilteredSpecularProgram = createBloomProgram("__mpp_ibl_prefiltered_specular__", FragmentShaderPrefilteredSpecularTemplate);
		addCoreResource(mPrefilteredSpecularProgram, true);
		mPbrBrdfIntegrationProgram = createBloomProgram("__mpp_ibl_brdf_integration__", FragmentShaderPbrBrdfIntegrationTemplate);
		addCoreResource(mPbrBrdfIntegrationProgram, true);

		// Internal text programs
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord4, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderTextTemplate);
			parser->setFragmentSource(FragmentShaderTextTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			ps->setAttribs({ "Points" });

			auto res = resourceMgr->declareResource("__mpp_p2d_points_text__", ResourceStreamPtr(ps)).first;
			addCoreResource(res, false);
		}
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderTextTemplate);
			parser->setFragmentSource(FragmentShaderTextTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);

			auto res = resourceMgr->declareResource("__mpp_p2d_tris_text__", ResourceStreamPtr(ps)).first;
			addCoreResource(res, true);
		}
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord4, mesh::Vertex::DataType::Float, false);
			//layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);
			layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderTextTemplate);
			parser->setFragmentSource(FragmentShaderTextTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			ps->setAttribs({ "Points", "Colours" });

			auto res = resourceMgr->declareResource("__mpp_p2d_points_text_coloured__", ResourceStreamPtr(ps)).first;
			addCoreResource(res, false);
		}
		{
			mesh::MeshSpecification meshSpec;

			auto layout = meshSpec.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			//layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);
			layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);

			auto parser = make_shared<program::Parser>();

			parser->setMeshSpecification(meshSpec);
			parser->setVertexSource(VertexShaderTextTemplate);
			parser->setFragmentSource(FragmentShaderTextTemplate);

			auto ps = new ProgrammaticProgramStream(resourceMgr);
			ps->setParser(parser);
			ps->setAttribs({ "Colours" });

			auto res = resourceMgr->declareResource("__mpp_p2d_tris_text_coloured__", ResourceStreamPtr(ps)).first;
			addCoreResource(res, true);
		}

		// Default texture
		auto blankStream = new ProgrammaticTextureStream(resourceMgr);
		blankStream->setTarget(TextureTarget::Texture2D);
		blankStream->setData([](string const& id)
		{
			TextureData data;

			data.width = 1;
			data.height = 1;
			data.bitsPerPixel = 24;
			data.dataType = GL_UNSIGNED_BYTE;
			data.pixelFormat = GL_RGB;

			size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

			data.data = new uint8_t[dataSize];
			memset(data.data, 255, dataSize);

			return data;
		});

		blankStream->setFiltering(TextureParams::MinFilter::Nearest, TextureParams::MagFilter::Nearest);
		mNoTexture = resourceMgr->declareResource("__mpp_tex_none__", ResourceStreamPtr(blankStream)).first;
		addCoreResource(mNoTexture, true);

		// Standard PBR fallback maps. PBR materials can omit any texture map;
		// Material resolves the corresponding canonical sampler to these values.
		auto addPbrFallbackTexture = [this, resourceMgr](string const& name, uint8_t red, uint8_t green, uint8_t blue, TextureColourSpace colourSpace)
		{
			auto stream = new ProgrammaticTextureStream(resourceMgr);
			stream->setTarget(TextureTarget::Texture2D);
			stream->setColourSpace(colourSpace);
			stream->setData([red, green, blue](string const&)
			{
				TextureData data;
				data.width = 1;
				data.height = 1;
				data.bitsPerPixel = 24;
				data.dataType = GL_UNSIGNED_BYTE;
				data.pixelFormat = GL_RGB;
				data.data = new uint8_t[3]{ red, green, blue };
				return data;
			});
			auto texture = resourceMgr->declareResource(name, ResourceStreamPtr(stream)).first;
			addCoreResource(texture, true);
		};
		addPbrFallbackTexture("__mpp_tex_pbr_white__", 255, 255, 255, TextureColourSpace::Srgb);
		addPbrFallbackTexture("__mpp_tex_pbr_black__", 0, 0, 0, TextureColourSpace::Srgb);
		addPbrFallbackTexture("__mpp_tex_pbr_normal__", 128, 128, 255, TextureColourSpace::Linear);
		addPbrFallbackTexture("__mpp_tex_pbr_metallic_roughness__", 0, 255, 255, TextureColourSpace::Linear);
		auto addPbrIblFallback = [this, resourceMgr](string const& name, TextureTarget target, uint8_t red, uint8_t green, uint8_t blue)
		{
			auto stream = new ProgrammaticTextureStream(resourceMgr);
			stream->setTarget(target);
			stream->setColourSpace(TextureColourSpace::Linear);
			stream->setData([red, green, blue](string const&) { TextureData data; data.width = data.height = 1; data.bitsPerPixel = 24; data.dataType = GL_UNSIGNED_BYTE; data.pixelFormat = GL_RGB; data.data = new uint8_t[3]{ red, green, blue }; return data; });
			auto texture = resourceMgr->declareResource(name, ResourceStreamPtr(stream)).first;
			addCoreResource(texture, true);
		};
		addPbrIblFallback("__mpp_tex_pbr_ibl_cube__", TextureTarget::CubeMap, 0, 0, 0);
		// The shader reads this as (scale, bias) in .rg:
		//   specular = prefiltered * (fresnel * brdf.x + brdf.y)
		// so a white texel means bias = 1 and adds a full unit of specular energy out
		// of nowhere. (1, 0) is the neutral pair: it passes the Fresnel term through
		// untouched. Black would be equally wrong in the other direction, killing the
		// term entirely. This is currently masked by the prefiltered fallback being
		// black, but anything that supplies a real prefiltered cubemap while falling
		// back on the LUT would bloom out.
		addPbrIblFallback("__mpp_tex_pbr_brdf_lut__", TextureTarget::Texture2D, 255, 0, 0);

		// Internal font texture
		auto ts = new ProgrammaticTextureStream(resourceMgr);
		ts->setTarget(TextureTarget::Texture2D);
		ts->setData([](string const& id)
		{
			InternalFont internalFont;
			TextureData data;

			data.width = internalFont.getWidth();
			data.height = internalFont.getHeight();
			data.bitsPerPixel = 32;
			data.dataType = GL_UNSIGNED_BYTE;
			data.pixelFormat = GL_RGBA;

			size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

			data.data = new uint8_t[dataSize];
			memcpy(data.data, (uint8_t const*)internalFont.getData(), dataSize);

			return data;
		});

		ts->setFiltering(TextureParams::MinFilter::Nearest, TextureParams::MagFilter::Nearest);

		mInternalFontTexture = resourceMgr->declareResource("__mpp_tex_internalfont__", ResourceStreamPtr(ts)).first;
		addCoreResource(mInternalFontTexture, true);

		mInternalFont = new Font(mInternalFontTexture);

		// Default 2d program
		mesh::MeshSpecification spec2d(mesh::Primitive::Type::Triangles);
		auto layout = spec2d.createVertexBufferAttributeLayout(false);
		layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
		layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
		layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);

		mDefaultProgram2d = resourceMgr->getDefault2dProgram(spec2d, 0, true);
		addCoreResource(mDefaultProgram2d, false);

		// Default material
		auto defaultMatStream = new ProgrammaticBasicMaterialStream(mResourceMgr);
		
		defaultMatStream->setProgram(mDefaultProgram2d->getName());
		defaultMatStream->setTexture("TEX1", "__mpp_tex_none__");
		mDefaultMaterial = resourceMgr->declareResource("__mpp_mat_default__", mpp::ResourceStreamPtr(defaultMatStream)).first;
		addCoreResource(mDefaultMaterial, true);

		// Internal 2d program
		{
			mesh::MeshSpecification spec2dinternal(mesh::Primitive::Type::Triangles);
			spec2dinternal.setIndexedVertices(true);

			auto layout = spec2dinternal.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			layout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);

			mInternalProgram2d = resourceMgr->getDefault2dProgram(spec2dinternal, MPP_PROGRAM_TAGS_TEXTURE, true);
			addCoreResource(mInternalProgram2d, true);
		}

		// Internal 2d material
		{
			auto internalMatStream = new ProgrammaticBasicMaterialStream(mResourceMgr);

			internalMatStream->setProgram(mInternalProgram2d->getName());
			internalMatStream->setTexture("TEX1", "__mpp_tex_none__");
			mInternalMaterial = resourceMgr->declareResource("__mpp_mat_internal__", mpp::ResourceStreamPtr(internalMatStream)).first;
			addCoreResource(mInternalMaterial, true);
		}

		// Internal font
		mTextAsPoints = mCaps.pointSizeRange[1] >= 16.0f;
		bool textAsPoints = mTextAsPoints;
		ProgrammaticBasicMaterialStream* textMatStream = new ProgrammaticBasicMaterialStream(mResourceMgr);

		textMatStream->setProgram(textAsPoints ? "__mpp_p2d_points_text__" : "__mpp_p2d_tris_text__");
		textMatStream->setUniform("COLOUR", glm::vec4(1, 1, 1, 1));
		textMatStream->setTexture("TEX1", "__mpp_tex_internalfont__");
		auto res = resourceMgr->declareResource("__mpp_mat_text_pt__", mpp::ResourceStreamPtr(textMatStream)).first;
		addCoreResource(res, true);

		ProgrammaticBasicMaterialStream* textMatStreamColoured = new ProgrammaticBasicMaterialStream(mResourceMgr);
		textMatStreamColoured->setProgram(textAsPoints ? "__mpp_p2d_points_text_coloured__" : "__mpp_p2d_tris_text_coloured__");
		textMatStreamColoured->setUniform("COLOUR", glm::vec4(1, 1, 1, 1));
		textMatStreamColoured->setTexture("TEX1", "__mpp_tex_internalfont__");
		res = resourceMgr->declareResource("__mpp_mat_text_ptc__", mpp::ResourceStreamPtr(textMatStreamColoured)).first;
		addCoreResource(res, true);

		int fontTextureWidth = (int)((Texture&)*mInternalFontTexture).getWidth();
		int fontTextureHeight = (int)((Texture&)*mInternalFontTexture).getHeight();
		int glyphWidth = fontTextureWidth / 16;
		int glyphHeight = fontTextureHeight / 16;
		for (int i = 0; i < 256; ++i)
		{
			int kern = -8;
			if (i == 'f') kern = -9;
			else if (i == 'i') kern = -10;
			else if (i == 'j') kern = -10;
			else if (i == 'r') kern = -10;

			mInternalFont->setGlyph(
				(uint8_t)i,
				(i % 16) * 16,
				fontTextureHeight - 16 - (i / 16) * 16,
				glyphWidth,
				glyphHeight,
				kern,
				0);
		}

		// Create font mesh
		auto textStream = new ProgrammaticModelStream(mResourceMgr);

		if (textAsPoints)
		{
			mesh::MeshSpecification textSpec(mesh::Primitive::Type::Points);
			textSpec.setStorageType(mesh::VertexBufferStorageType::Dynamic);
			textSpec.setIndexedVertices(false);

			mesh::VertexBufferAttributeLayout* attribLayout = textSpec.createVertexBufferAttributeLayout(false);

			attribLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			attribLayout->createAttribute(mesh::Vertex::Component::TexCoord4, mesh::Vertex::DataType::Float, false);

			int textMesh = (int)textStream->createMesh("text-mesh", textSpec, "__mpp_mat_text_pt__", 32, 16.0f);

			for (uint32_t i = 0; i < MaxTextGlyphs; ++i)
			{
				textStream->addVertexData(textMesh, mesh::VertexData(textSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f).f32(1.0f).f32(1.0f));
			}
		}
		else
		{
			mesh::MeshSpecification textSpec(mesh::Primitive::Type::Triangles);
			textSpec.setStorageType(mesh::VertexBufferStorageType::Dynamic);
			textSpec.setIndexedVertices(false);

			mesh::VertexBufferAttributeLayout* attribLayout = textSpec.createVertexBufferAttributeLayout(false);

			attribLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			attribLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);

			int textMesh = (int)textStream->createMesh("text-mesh", textSpec, "__mpp_mat_text_pt__", 32, -1.0f);

			for (uint32_t i = 0; i < MaxTextGlyphs * 6; ++i)
			{
				textStream->addVertexData(textMesh, mesh::VertexData(textSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f));
			}
		}

		auto textStreamPtr = ResourceStreamPtr(textStream);
		mTextMesh = resourceMgr->declareResource("__mpp_internal_text_mesh__", textStreamPtr).first;
		addCoreResource(mTextMesh, true);

		// Coloured font mesh
		textStream = new ProgrammaticModelStream(mResourceMgr);

		if (textAsPoints)
		{
			mesh::MeshSpecification textSpec(mesh::Primitive::Type::Points);
			textSpec.setStorageType(mesh::VertexBufferStorageType::Dynamic);
			textSpec.setIndexedVertices(false);

			mesh::VertexBufferAttributeLayout* attribLayout = textSpec.createVertexBufferAttributeLayout(false);

			attribLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			attribLayout->createAttribute(mesh::Vertex::Component::TexCoord4, mesh::Vertex::DataType::Float, false);
			//attribLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);
			attribLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);

			int textMesh = (int)textStream->createMesh("text-mesh", textSpec, "__mpp_mat_text_ptc__", 32, 16.0f);

			for (uint32_t i = 0; i < MaxTextGlyphs; ++i)
			{
				textStream->addVertexData(textMesh, mesh::VertexData(textSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f)
					.f32(1.0f).f32(1.0f).u8(255).u8(255).u8(255).u8(255));
			}
		}
		else
		{
			mesh::MeshSpecification textSpec(mesh::Primitive::Type::Triangles);
			textSpec.setStorageType(mesh::VertexBufferStorageType::Dynamic);
			textSpec.setIndexedVertices(false);

			mesh::VertexBufferAttributeLayout* attribLayout = textSpec.createVertexBufferAttributeLayout(false);

			attribLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
			attribLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			//attribLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::UnsignedByte, true);
			attribLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);

			int textMesh = (int)textStream->createMesh("text-mesh", textSpec, "__mpp_mat_text_ptc__", 32, -1.0f);

			for (uint32_t i = 0; i < MaxTextGlyphs * 6; ++i)
			{
				textStream->addVertexData(textMesh, mesh::VertexData(textSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f)
					.f32(1.0f).f32(1.0f).u8(255).u8(255).u8(255).u8(255));
			}
		}

		textStreamPtr = ResourceStreamPtr(textStream);
		mColouredTextMesh = resourceMgr->declareResource("__mpp_internal_coloured_text_mesh__", textStreamPtr).first;
		addCoreResource(mColouredTextMesh, true);

		// Fullscreen mesh
		auto quadStream = new ProgrammaticModelStream(mResourceMgr);
		mesh::MeshSpecification quadSpec = mesh::MeshSpecification(mesh::Primitive::Type::Triangles);

		auto attribLayout = quadSpec.createVertexBufferAttributeLayout(false);
		attribLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
		attribLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
		quadSpec.setStorageType(mesh::VertexBufferStorageType::Static);
		quadSpec.setIndexedVertices(false);

		auto fullscreenProgram = resourceMgr->getDefault2dProgram(quadSpec, 0, true);
		auto quadMesh = quadStream->createMesh("fullscreen-quad", quadSpec, fullscreenProgram->getName(), 32, -1.0f);

		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f));
		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32((float)mWindowWidth).f32(0.0f).f32(1.0f).f32(0.0f));
		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32((float)mWindowWidth).f32((float)mWindowHeight).f32(1.0f).f32(1.0f));
		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32((float)mWindowWidth).f32((float)mWindowHeight).f32(1.0f).f32(1.0f));
		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32(0.0f).f32((float)mWindowHeight).f32(0.0f).f32(1.0f));
		quadStream->addVertexData(quadMesh, mesh::VertexData(quadSpec, 1).f32(0.0f).f32(0.0f).f32(0.0f).f32(0.0f));

		mFullscreenQuad = resourceMgr->declareResource("__mpp_mesh_fullscreen_quad__", ResourceStreamPtr(quadStream)).first;
		addCoreResource(mFullscreenQuad, true);

		// Render targets are owned by render passes/pipelines. Do not create a
		// duplicate global scene target here.

		// Set none as active
		mActiveProgram.reset();

		// Profile graph
#ifdef MPP_PROFILE_BUILD
		mProfileLines = new LineBatch(
			"ProfileLines",
			Batch::ColourOptions::FloatRGBA,
			false,
			mSamplesToRecord * 5 + 2, // 5 seconds + 2 extra for graph
			this,
			mResourceMgr);

		mProfileLines->load();
#endif
	}

	void RenderSystem::destroyCoreResources()
	{
		// Font is a non-resource wrangler and must release its texture before the
		// core resource references are dropped. The destructor tolerates null.
		delete mInternalFont;
		mInternalFont = nullptr;

		// Release
		for (auto res : mCoreResources)
		{
			res->release(this);

		}

		// Destroy what we can
		for (auto res : mCoreResources)
		{
			if (!res->isReferenced())
			{
				res->destroy();
			}
		}
	}


	/*
	 * Use the default program.
	 *
	 */
	void RenderSystem::useDefaultProgram()
	{
		switch (mProjectionType)
		{
		case ProjectionType::Perspective3D:
			setUsedProgram(mDefaultProgram3d);
			break;

		case ProjectionType::Ortho2D:
			setUsedProgram(mDefaultProgram2d);
			break;

		default:
			THROW_MPP("Unsupported projection.", __LINE__, __FILE__, __func__);
		}
	}

	RenderSystem::PrimaryColourOutputDraw::PrimaryColourOutputDraw(RenderSystem* renderSystem)
	{
		if (!renderSystem || renderSystem->mExpectedGraphColourOutputs < 2) return;
		mRenderSystem = renderSystem;
		mColourOutputs = renderSystem->mExpectedGraphColourOutputs;
		mSavedState = renderSystem->captureRasterState(mColourOutputs);
		auto state = mSavedState;
		for (size_t output = 1; output < state.colourWriteMasks.size(); ++output)
			state.colourWriteMasks[output] = GraphColourWriteMask{ false, false, false, false };
		renderSystem->applyRasterState(state, mColourOutputs, renderSystem->mViewportWidth, renderSystem->mViewportHeight);
		renderSystem->setExpectedGraphColourOutputs(0);
	}

	RenderSystem::PrimaryColourOutputDraw::~PrimaryColourOutputDraw()
	{
		if (!mRenderSystem) return;
		mRenderSystem->setExpectedGraphColourOutputs(mColourOutputs);
		mRenderSystem->applyRasterState(mSavedState, mColourOutputs, mRenderSystem->mViewportWidth, mRenderSystem->mViewportHeight);
	}
	/*
	 * Set the used program.  This should only be called
	 * after GL has accepted the program as active.
	 *
	 */
	void RenderSystem::setUsedProgram(ResourcePtr program)
	{
		if (mExpectedGraphColourOutputs > 0)
		{
			string diagnostic;
			if (!static_cast<Program*>(program.get())->validateFragmentOutputLocations(mExpectedGraphColourOutputs, diagnostic))
			{
				THROW_MPP(diagnostic, __LINE__, __FILE__, __func__);
			}
		}
		if (program == mActiveProgram)
		{
			return;
		}

		static_cast<Program*>(program.get())->bind();
		mActiveProgram = program;
	}

	/*
	 * Get used program.
	 *
	 */
	ResourcePtr RenderSystem::getUsedProgram()
	{
		return mActiveProgram;
	}

	/*
	 * Use the default texture (no texture).
	 *
	 */
	void RenderSystem::setDefaultTexture()
	{
		GL_CHECK(glActiveTexture(GL_TEXTURE0));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	}

	void RenderSystem::setDepthTestState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.depthTest == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST));
		mRasterStateCache.depthTest = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setDepthWriteState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.depthWrite == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(glDepthMask(enabled ? GL_TRUE : GL_FALSE));
		mRasterStateCache.depthWrite = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setDepthCompareState(GraphCompareOp compare, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.depthCompare == compare) { ++mStateChangesSkipped; return; }
		GL_CHECK(glDepthFunc(graphCompareOp(compare)));
		mRasterStateCache.depthCompare = compare; ++mStateChangesApplied;
	}

	void RenderSystem::setCullState(GraphCullMode mode, bool force)
	{
		bool const enabled = mode != GraphCullMode::None;
		bool const wasEnabled = mRasterStateCache.cullMode != GraphCullMode::None;
		if (force || !mRasterStateCacheKnown || wasEnabled != enabled)
		{
			GL_CHECK(enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE)); ++mStateChangesApplied;
		}
		else ++mStateChangesSkipped;
		if (enabled)
		{
			if (force || !mRasterStateCacheKnown || mRasterStateCache.cullMode != mode) { GL_CHECK(glCullFace(mode == GraphCullMode::Front ? GL_FRONT : GL_BACK)); ++mStateChangesApplied; }
			else ++mStateChangesSkipped;
		}
		mRasterStateCache.cullMode = mode;
	}

	void RenderSystem::setFrontFaceState(GraphFrontFace face, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.frontFace == face) { ++mStateChangesSkipped; return; }
		GL_CHECK(glFrontFace(face == GraphFrontFace::Clockwise ? GL_CW : GL_CCW));
		mRasterStateCache.frontFace = face; ++mStateChangesApplied;
	}

	void RenderSystem::setFillModeState(GraphFillMode mode, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.fillMode == mode) { ++mStateChangesSkipped; return; }
		GL_CHECK(glPolygonMode(GL_FRONT_AND_BACK, mode == GraphFillMode::Line ? GL_LINE : GL_FILL));
		mRasterStateCache.fillMode = mode; ++mStateChangesApplied;
	}

	void RenderSystem::setBlendState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.blend == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND));
		mRasterStateCache.blend = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setBlendEquationState(GraphBlendOp colour, GraphBlendOp alpha, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.colourBlendOp == colour && mRasterStateCache.alphaBlendOp == alpha) { ++mStateChangesSkipped; return; }
		GL_CHECK(glBlendEquationSeparate(graphBlendOp(colour), graphBlendOp(alpha)));
		mRasterStateCache.colourBlendOp = colour; mRasterStateCache.alphaBlendOp = alpha; ++mStateChangesApplied;
	}

	void RenderSystem::setBlendFunctionState(GraphBlendFactor sourceColour, GraphBlendFactor destinationColour, GraphBlendFactor sourceAlpha, GraphBlendFactor destinationAlpha, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.sourceColourBlend == sourceColour && mRasterStateCache.destinationColourBlend == destinationColour && mRasterStateCache.sourceAlphaBlend == sourceAlpha && mRasterStateCache.destinationAlphaBlend == destinationAlpha) { ++mStateChangesSkipped; return; }
		GL_CHECK(glBlendFuncSeparate(graphBlendFactor(sourceColour), graphBlendFactor(destinationColour), graphBlendFactor(sourceAlpha), graphBlendFactor(destinationAlpha)));
		mRasterStateCache.sourceColourBlend = sourceColour; mRasterStateCache.destinationColourBlend = destinationColour; mRasterStateCache.sourceAlphaBlend = sourceAlpha; mRasterStateCache.destinationAlphaBlend = destinationAlpha; ++mStateChangesApplied;
	}

	void RenderSystem::setMultisampleState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.multisample == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_MULTISAMPLE) : glDisable(GL_MULTISAMPLE));
		mRasterStateCache.multisample = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setAlphaToCoverageState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.alphaToCoverage == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE) : glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE));
		mRasterStateCache.alphaToCoverage = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setScissorState(bool enabled, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.scissor == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST));
		mRasterStateCache.scissor = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setScissorRectangleState(glm::uvec4 const& rectangle, bool force)
	{
		if (!force && mRasterStateCacheKnown && mRasterStateCache.scissorRectangle == rectangle) { ++mStateChangesSkipped; return; }
		GL_CHECK(glScissor(static_cast<GLint>(rectangle.x), static_cast<GLint>(rectangle.y), static_cast<GLsizei>(rectangle.z), static_cast<GLsizei>(rectangle.w)));
		mRasterStateCache.scissorRectangle = rectangle; ++mStateChangesApplied;
	}

	void RenderSystem::setColourMaskState(size_t output, GraphColourWriteMask const& mask, bool force)
	{
		if (mRasterStateCache.colourWriteMasks.size() <= output) mRasterStateCache.colourWriteMasks.resize(output + 1);
		if (!force && mRasterStateCacheKnown && mRasterStateCache.colourWriteMasks[output] == mask) { ++mStateChangesSkipped; return; }
		GL_CHECK(glColorMaski(static_cast<GLuint>(output), mask.red, mask.green, mask.blue, mask.alpha));
		mRasterStateCache.colourWriteMasks[output] = mask; ++mStateChangesApplied;
	}

	void RenderSystem::setAllColourMasksState(GraphColourWriteMask const& mask, bool force)
	{
		size_t const count = max<size_t>(1, max<size_t>(mCaps.maxDrawBuffers, mRasterStateCache.colourWriteMasks.size()));
		bool unchanged = mRasterStateCacheKnown;
		for (size_t output = 0; output < count && unchanged; ++output)
			unchanged = output < mRasterStateCache.colourWriteMasks.size() && mRasterStateCache.colourWriteMasks[output] == mask;
		if (!force && unchanged) { ++mStateChangesSkipped; return; }
		GL_CHECK(glColorMask(mask.red, mask.green, mask.blue, mask.alpha));
		mRasterStateCache.colourWriteMasks.assign(count, mask); ++mStateChangesApplied;
	}

	void RenderSystem::setPolygonOffsetFillState(bool enabled)
	{
		if (mRasterStateCacheKnown && mPolygonOffsetFill == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_POLYGON_OFFSET_FILL) : glDisable(GL_POLYGON_OFFSET_FILL)); mPolygonOffsetFill = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setPolygonOffsetState(float factor, float units)
	{
		if (mRasterStateCacheKnown && mPolygonOffsetFactor == factor && mPolygonOffsetUnits == units) { ++mStateChangesSkipped; return; }
		GL_CHECK(glPolygonOffset(factor, units)); mPolygonOffsetFactor = factor; mPolygonOffsetUnits = units; ++mStateChangesApplied;
	}

	void RenderSystem::setProgramPointSizeState(bool enabled)
	{
		if (mRasterStateCacheKnown && mProgramPointSize == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_PROGRAM_POINT_SIZE) : glDisable(GL_PROGRAM_POINT_SIZE)); mProgramPointSize = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setPointSpriteState(bool enabled)
	{
		if (mRasterStateCacheKnown && mPointSprite == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_POINT_SPRITE) : glDisable(GL_POINT_SPRITE)); mPointSprite = enabled; ++mStateChangesApplied;
	}

	void RenderSystem::setCubeMapSeamlessState(bool enabled)
	{
		if (mRasterStateCacheKnown && mCubeMapSeamless == enabled) { ++mStateChangesSkipped; return; }
		GL_CHECK(enabled ? glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS) : glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS)); mCubeMapSeamless = enabled; ++mStateChangesApplied;
	}

	GraphRasterState RenderSystem::captureRasterState(size_t colourOutputs) const
	{
		if (!mRasterStateCacheKnown) THROW_MPP("Render-state cache is not initialized.", __LINE__, __FILE__, __func__);
		auto result = mRasterStateCache;
		result.explicitState = true;
		if (result.colourWriteMasks.size() < max<size_t>(1, colourOutputs))
			result.colourWriteMasks.resize(max<size_t>(1, colourOutputs));
		return result;
	}

	void RenderSystem::applyRasterState(GraphRasterState const& state, size_t colourOutputs, size_t width, size_t height, bool force)
	{
		setDepthTestState(state.depthTest, force); setDepthWriteState(state.depthWrite, force); setDepthCompareState(state.depthCompare, force);
		setCullState(state.cullMode, force); setFrontFaceState(state.frontFace, force); setFillModeState(state.fillMode, force);
		setBlendState(state.blend, force); setBlendEquationState(state.colourBlendOp, state.alphaBlendOp, force);
		setBlendFunctionState(state.sourceColourBlend, state.destinationColourBlend, state.sourceAlphaBlend, state.destinationAlphaBlend, force);
		setMultisampleState(state.multisample, force); setAlphaToCoverageState(state.alphaToCoverage, force); setScissorState(state.scissor, force);
		glm::uvec4 rectangle = state.scissorRectangle;
		if (state.scissor) { if (!rectangle.z) rectangle.z = static_cast<uint32_t>(width); if (!rectangle.w) rectangle.w = static_cast<uint32_t>(height); }
		setScissorRectangleState(rectangle, force);
		for (size_t output = 0; output < max<size_t>(1, colourOutputs); ++output)
			setColourMaskState(output, output < state.colourWriteMasks.size() ? state.colourWriteMasks[output] : GraphColourWriteMask{}, force);
		mRasterStateCacheKnown = true;
	}

	void RenderSystem::forceRenderWriteMasks(bool depth, GraphColourWriteMask const& colour)
	{
		setDepthWriteState(depth);
		setAllColourMasksState(colour);
		mRasterStateCacheKnown = true;
	}

	void RenderSystem::debugVerifyRasterStateCache()
	{
#if defined(_DEBUG)
		if (!mRasterStateCacheKnown || (++mStateVerificationCounter & 255u) != 0) return;
		bool mismatch = (glIsEnabled(GL_DEPTH_TEST) == GL_TRUE) != mRasterStateCache.depthTest ||
			(glIsEnabled(GL_CULL_FACE) == GL_TRUE) != (mRasterStateCache.cullMode != GraphCullMode::None) ||
			(glIsEnabled(GL_BLEND) == GL_TRUE) != mRasterStateCache.blend ||
			(glIsEnabled(GL_MULTISAMPLE) == GL_TRUE) != mRasterStateCache.multisample ||
			(glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) == GL_TRUE) != mRasterStateCache.alphaToCoverage ||
			(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE) != mRasterStateCache.scissor;
		GLboolean depthWrite = GL_TRUE;
		GLint depthCompare = 0, cullMode = 0, frontFace = 0, polygonMode[2]{}, blendEquationColour = 0, blendEquationAlpha = 0;
		GLint sourceColour = 0, destinationColour = 0, sourceAlpha = 0, destinationAlpha = 0, scissor[4]{};
		GL_CHECK(glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite)); GL_CHECK(glGetIntegerv(GL_DEPTH_FUNC, &depthCompare));
		GL_CHECK(glGetIntegerv(GL_CULL_FACE_MODE, &cullMode)); GL_CHECK(glGetIntegerv(GL_FRONT_FACE, &frontFace)); GL_CHECK(glGetIntegerv(GL_POLYGON_MODE, polygonMode));
		GL_CHECK(glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationColour)); GL_CHECK(glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha));
		GL_CHECK(glGetIntegerv(GL_BLEND_SRC_RGB, &sourceColour)); GL_CHECK(glGetIntegerv(GL_BLEND_DST_RGB, &destinationColour));
		GL_CHECK(glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha)); GL_CHECK(glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha)); GL_CHECK(glGetIntegerv(GL_SCISSOR_BOX, scissor));
		mismatch = mismatch || (depthWrite == GL_TRUE) != mRasterStateCache.depthWrite || depthCompare != static_cast<GLint>(graphCompareOp(mRasterStateCache.depthCompare)) ||
			(mRasterStateCache.cullMode != GraphCullMode::None && cullMode != (mRasterStateCache.cullMode == GraphCullMode::Front ? GL_FRONT : GL_BACK)) ||
			frontFace != (mRasterStateCache.frontFace == GraphFrontFace::Clockwise ? GL_CW : GL_CCW) || polygonMode[0] != (mRasterStateCache.fillMode == GraphFillMode::Line ? GL_LINE : GL_FILL) ||
			blendEquationColour != static_cast<GLint>(graphBlendOp(mRasterStateCache.colourBlendOp)) || blendEquationAlpha != static_cast<GLint>(graphBlendOp(mRasterStateCache.alphaBlendOp)) ||
			sourceColour != static_cast<GLint>(graphBlendFactor(mRasterStateCache.sourceColourBlend)) || destinationColour != static_cast<GLint>(graphBlendFactor(mRasterStateCache.destinationColourBlend)) ||
			sourceAlpha != static_cast<GLint>(graphBlendFactor(mRasterStateCache.sourceAlphaBlend)) || destinationAlpha != static_cast<GLint>(graphBlendFactor(mRasterStateCache.destinationAlphaBlend)) ||
			scissor[0] != static_cast<GLint>(mRasterStateCache.scissorRectangle.x) || scissor[1] != static_cast<GLint>(mRasterStateCache.scissorRectangle.y) ||
			scissor[2] != static_cast<GLint>(mRasterStateCache.scissorRectangle.z) || scissor[3] != static_cast<GLint>(mRasterStateCache.scissorRectangle.w);
		for (GLuint output = 0; output < mRasterStateCache.colourWriteMasks.size() && !mismatch; ++output)
		{
			GLboolean mask[4]{}; GL_CHECK(glGetBooleani_v(GL_COLOR_WRITEMASK, output, mask)); auto const& cached = mRasterStateCache.colourWriteMasks[output];
			mismatch = (mask[0] == GL_TRUE) != cached.red || (mask[1] == GL_TRUE) != cached.green || (mask[2] == GL_TRUE) != cached.blue || (mask[3] == GL_TRUE) != cached.alpha;
		}
		if (mismatch)
		{
			warnMessage("Render-state cache diverged from OpenGL state; restoring the authoritative cached snapshot.");
			auto snapshot = mRasterStateCache;
			applyRasterState(snapshot, snapshot.colourWriteMasks.size(), mViewportWidth, mViewportHeight, true);
		}
#endif
	}

	RenderSystem::RasterStateCacheStats RenderSystem::getRasterStateCacheStats() const
	{
		return { mStateChangesApplied, mStateChangesSkipped };
	}

	/*
	* Set up basic OpenGL state.
	*
	*/
	void RenderSystem::setDefaultState()
	{
		mRasterStateCacheKnown = false;
		GraphRasterState defaults;
		defaults.explicitState = true;
		defaults.cullMode = GraphCullMode::None;
		defaults.scissor = false;
		defaults.scissorRectangle = { 0, 0, static_cast<uint32_t>(mWindowWidth), static_cast<uint32_t>(mWindowHeight) };
		applyRasterState(defaults, max<size_t>(1, mCaps.maxDrawBuffers), mWindowWidth, mWindowHeight, true);

		setProgramPointSizeState(true);

		// A core profile always generates gl_PointCoord and rejects this enum, but a
		// compatibility profile leaves point-sprite coordinate generation off, and
		// then gl_PointCoord reads (0, 0) in every fragment. Text is drawn as point
		// sprites whose glyph is a gl_PointCoord lookup into the font atlas, so
		// without this every glyph samples one transparent texel and no overlay ever
		// appears. SDL requests no profile mask, so which one we get is the driver's
		// choice: set the state whenever the context still honours it.
		if (mCaps.compatibilityProfile)
			setPointSpriteState(true);

		// Without this, a bilinear tap near a cube edge clamps inside its own face
		// instead of reaching across, so every edge shows a seam. The IBL cubemaps
		// are small enough -- 32x32 irradiance, a prefiltered chain ending at 1x1 --
		// that a texel spans several degrees and the discontinuity is plainly
		// visible. Every cubemap this engine samples wants seamless filtering, so
		// enable it globally rather than per texture; core since GL 3.2.
		if (mCaps.glVersionMajor > 3 || (mCaps.glVersionMajor == 3 && mCaps.glVersionMinor >= 2))
			setCubeMapSeamlessState(true);
		else
			warnMessage("OpenGL " + std::to_string(mCaps.glVersionMajor) + "." + std::to_string(mCaps.glVersionMinor) +
				" predates seamless cubemap filtering; IBL cube edges will show seams.");

		// Set matrices to identity
		m3dCameraMatrix = glm::mat4();
		m3dProjectionMatrix = glm::mat4();
		m3dModelMatrix = glm::mat4();
		m3dModelCameraProjectionMatrix = glm::mat4();
	}

	/*
	 * Set up the display/viewport.
	 *
	 */
	void RenderSystem::setDisplay(int width, int height)
	{
		if (mScreen)
		{
			mScreen.reset();
		}

		mWindowWidth = (size_t)width;
		mWindowHeight = (size_t)height;
		mScreen = RenderTargetPtr(new Screen(width, height));
		setRenderTarget(mScreen);

		for (auto const& [name, pipeline] : mPipelines)
		{
			pipeline->resize(mWindowWidth, mWindowHeight);
		}

		if (mFullscreenQuad)
		{
			auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
			auto buffer = mesh->getVertexBuffer(0);
			auto& data = buffer->getBufferData();
			const float vertices[] =
			{
				0.0f, 0.0f, 0.0f, 0.0f,
				(float)mWindowWidth, 0.0f, 1.0f, 0.0f,
				(float)mWindowWidth, (float)mWindowHeight, 1.0f, 1.0f,
				(float)mWindowWidth, (float)mWindowHeight, 1.0f, 1.0f,
				0.0f, (float)mWindowHeight, 0.0f, 1.0f,
				0.0f, 0.0f, 0.0f, 0.0f
			};
			memcpy(data.data(), vertices, sizeof(vertices));
			buffer->mapBufferData(6);
		}
	}

	/*
	 * Write a message to logfile.
	 *
	 */
	void RenderSystem::debugMessage(string const& message)
	{
		mLogger->debug(message);
	}

	void RenderSystem::infoMessage(string const& message)
	{
		mLogger->info(message);
	}

	void RenderSystem::warnMessage(string const& message)
	{
		mLogger->warn(message);
	}

	void RenderSystem::errorMessage(string const& message)
	{
		mLogger->error(message);
	}

	/*
	 * Set the current render target.
	 *
	 */
	void RenderSystem::setRenderTarget(RenderTargetPtr renderTarget)
	{
		flushVertexBuffers();

		if (mRenderTarget.get())
		{
			mRenderTarget->deactivate();
		}

		mRenderTarget = renderTarget;
		mRenderTarget->activate();
	}

	void RenderSystem::pushRenderTarget(RenderTargetPtr renderTarget)
	{
		mRenderTargetStack.push(mRenderTarget);
		setRenderTarget(renderTarget);
	}

	void RenderSystem::popRenderTarget()
	{
		auto renderTarget = mRenderTargetStack.top();
		mRenderTargetStack.pop();

		setRenderTarget(renderTarget);
	}

	RenderSystem::CubemapFaceRenderScope::CubemapFaceRenderScope(RenderSystem& system, RenderTargetPtr const& target, uint32_t face, uint32_t mipLevel)
		: mSystem(&system), mTarget(dynamic_cast<RenderTexture*>(target.get()))
	{
		if (!mTarget || mTarget->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP)
			THROW_MPP("Cubemap face rendering requires a cubemap RenderTexture.", __LINE__, __FILE__, __func__);
		if (mSystem->mCubemapFaceRenderActive) THROW_MPP("Nested cubemap face render scopes are not supported.", __LINE__, __FILE__, __func__);
		mSystem->mCubemapFaceRenderActive = true;
		mGpuScope = std::make_unique<GpuDebugScope>("Cubemap: " + mTarget->getName() + " face " + std::to_string(face) + " mip " + std::to_string(mipLevel));
		mViewport[0] = mSystem->mViewportX; mViewport[1] = mSystem->mViewportY;
		mViewport[2] = static_cast<int>(mSystem->mViewportWidth); mViewport[3] = static_cast<int>(mSystem->mViewportHeight);
		auto const rasterState = mSystem->captureRasterState(1);
		mScissor[0] = static_cast<int>(rasterState.scissorRectangle.x); mScissor[1] = static_cast<int>(rasterState.scissorRectangle.y);
		mScissor[2] = static_cast<int>(rasterState.scissorRectangle.z); mScissor[3] = static_cast<int>(rasterState.scissorRectangle.w);
		GL_CHECK(glGetIntegerv(GL_DRAW_BUFFER, &mDrawBuffer));
		GL_CHECK(glGetIntegerv(GL_READ_BUFFER, &mReadBuffer));
		mScissorEnabled = rasterState.scissor;
		mSystem->pushRenderTarget(target);
		try
		{
			mTarget->attachColourFace(0, face, mipLevel);
			auto dimension = std::max<size_t>(1, mTarget->getWidth() >> mipLevel);
			mSystem->setViewport(0, 0, dimension, dimension);
			mSystem->setScissorRectangleState({ 0, 0, dimension, dimension });
		}
		catch (...) { mSystem->popRenderTarget(); mSystem->mCubemapFaceRenderActive = false; throw; }
	}

	void RenderSystem::CubemapFaceRenderScope::finish()
	{
		if (mFinished) return;
		mTarget->restoreColourFaces();
		mSystem->popRenderTarget();
		mSystem->setViewport(mViewport[0], mViewport[1], mViewport[2], mViewport[3]);
		mSystem->setScissorRectangleState({ static_cast<uint32_t>(mScissor[0]), static_cast<uint32_t>(mScissor[1]), static_cast<uint32_t>(mScissor[2]), static_cast<uint32_t>(mScissor[3]) });
		GL_CHECK(glDrawBuffer((GLenum)mDrawBuffer));
		GL_CHECK(glReadBuffer((GLenum)mReadBuffer));
		if (mScissorEnabled) mSystem->setScissorState(true); else mSystem->setScissorState(false);
		mGpuScope.reset();
		mSystem->mCubemapFaceRenderActive = false;
		mFinished = true;
	}

	RenderSystem::CubemapFaceRenderScope::~CubemapFaceRenderScope()
	{
		try { finish(); } catch (...) {}
	}

	/*
	 * Set screen as target.
	 *
	 */
	void RenderSystem::renderToScreen()
	{
		flushVertexBuffers();

		if (mRenderTarget.get())
		{
			mRenderTarget->deactivate();
		}

		mRenderTarget = mScreen;
		mScreen->activate();
	}

	void RenderSystem::setExpectedGraphColourOutputs(size_t count)
	{
		mExpectedGraphColourOutputs = count;
	}

	void RenderSystem::beginRenderFlowCapture(RenderPipelineFlowSnapshot* snapshot) noexcept
	{
		mFlowCapture = snapshot;
		mCurrentFlowPass = {};
		mFlowSequence = 0;
		mFlowCaptureFailed = false;
		if(snapshot)try{snapshot->batches.reserve(mFlowBatchHighWater);snapshot->physicalEvents.reserve(mFlowEventHighWater);}catch(...){mFlowCaptureFailed=true;}
	}

	bool RenderSystem::endRenderFlowCapture() noexcept
	{
		bool const complete = mFlowCapture && !mFlowCaptureFailed;
		if(complete){mFlowBatchHighWater=std::max(mFlowBatchHighWater,mFlowCapture->batches.size());mFlowEventHighWater=std::max(mFlowEventHighWater,mFlowCapture->physicalEvents.size());}
		mFlowCapture = nullptr;
		mCurrentFlowPass = {};
		mFlowSequence = 0;
		mFlowCaptureFailed = false;
		return complete;
	}

	bool RenderSystem::isRenderFlowCaptureActive() const noexcept
	{
		return mFlowCapture && !mFlowCaptureFailed;
	}

	void RenderSystem::beginRenderFlowPass(GraphPassHandle pass, string const& name) noexcept
	{
		mCurrentFlowPass = pass;
		recordRenderFlowEvent(RenderFlowEventKind::PassBegin, name);
	}

	void RenderSystem::endRenderFlowPass(GraphPassHandle pass, string const& name) noexcept
	{
		recordRenderFlowEvent(RenderFlowEventKind::PassEnd, name);
		if (mCurrentFlowPass.id == pass.id) mCurrentFlowPass = {};
	}

	void RenderSystem::abortRenderFlowPass() noexcept
	{
		mCurrentFlowPass = {};
	}

	void RenderSystem::failRenderFlowCapture() noexcept
	{
		if(mFlowCapture)mFlowCaptureFailed=true;
	}

	void RenderSystem::recordRenderFlowBatch(RenderBatchSubmission submission) noexcept
	{
		if (!mFlowCapture || mFlowCaptureFailed || !mCurrentFlowPass.isValid()) return;
		try
		{
			submission.sequence = mFlowSequence++;
			submission.parentPass = mCurrentFlowPass;
			RenderFlowEvent event;event.kind=RenderFlowEventKind::BatchSubmission;event.sequence=submission.sequence;event.pass=mCurrentFlowPass;event.name=submission.meshName;
			mFlowCapture->batches.push_back(std::move(submission));
			mFlowCapture->physicalEvents.push_back(std::move(event));
		}
		catch (...)
		{
			mFlowCaptureFailed = true;
		}
	}

	void RenderSystem::recordRenderFlowStateChanges(vector<string> changes) noexcept
	{
		if (!mFlowCapture || mFlowCaptureFailed || !mCurrentFlowPass.isValid() || changes.empty()) return;
		try
		{
			RenderFlowEvent event; event.kind = RenderFlowEventKind::GlState; event.sequence = mFlowSequence++;
			event.pass = mCurrentFlowPass; event.name = "GL State"; event.stateChanges = std::move(changes);
			mFlowCapture->physicalEvents.push_back(std::move(event));
		}
		catch (...) { mFlowCaptureFailed = true; }
	}

	void RenderSystem::recordRenderFlowEvent(RenderFlowEventKind kind, string const& name, GraphImageHandle image,
		bool enabled, string const& bypassReason, string const& outputName, bool depth,
		vector<RenderFlowResourceDesc> inputs, vector<RenderFlowResourceDesc> outputs) noexcept
	{
		if (!mFlowCapture || mFlowCaptureFailed) return;
		try
		{
			RenderFlowEvent event;event.kind=kind;event.sequence=mFlowSequence++;event.pass=mCurrentFlowPass;event.image=image;event.name=name;event.outputName=outputName;event.bypassReason=bypassReason;event.enabled=enabled;event.depth=depth;event.inputs=std::move(inputs);event.outputs=std::move(outputs);mFlowCapture->physicalEvents.push_back(std::move(event));
		}
		catch (...)
		{
			mFlowCaptureFailed = true;
		}
	}

	RenderTargetPtr RenderSystem::getScreenRenderTarget() const
	{
		return mScreen;
	}

	/*
	 * Create a new render texture.
	 *
	 */
	RenderTargetPtr RenderSystem::createRenderTexture(string const& name, size_t width, size_t height, size_t numAttachments, bool depthBuffer)
	{
		RenderTextureOptions options;
		options.numAttachments = numAttachments;
		options.depthAttachment = depthBuffer ? RenderTextureDepthAttachment::DepthRenderbuffer : RenderTextureDepthAttachment::None;
		return createRenderTexture(name, width, height, options);
	}

	RenderTargetPtr RenderSystem::createRenderTexture(string const& name, size_t width, size_t height, RenderTextureOptions const& options)
	{
		return createPhysicalRenderTexture(name,width,height,options,1);
	}

	void RenderSystem::validateEquirectangularConversionSource(Texture const* source, string const& generatedName, uint32_t faceSize, uint32_t mipLevels) const
	{
		if (!source || !source->isLoaded()) THROW_MPP("Equirectangular IBL source must be a loaded texture.", __LINE__, __FILE__, __func__);
		if (generatedName.empty() || !faceSize || !mipLevels) THROW_MPP("Equirectangular IBL output name, face size, and mip count must be non-zero.", __LINE__, __FILE__, __func__);
		if (source->getTextureTarget() != GL_TEXTURE_2D) THROW_MPP("Equirectangular IBL source must be a Texture2D.", __LINE__, __FILE__, __func__);
		auto format = source->getInternalFormat();
		if (format != GL_RGB16F && format != GL_RGBA16F && format != GL_RGB32F && format != GL_RGBA32F)
			THROW_MPP("Equirectangular IBL source must use a linear floating-point RGB/RGBA format.", __LINE__, __FILE__, __func__);
	}

	void RenderSystem::validatePrefilteredSpecularSource(Texture const* source, string const& generatedName, uint32_t faceSize, uint32_t mipLevels, uint32_t sampleCount)
	{
		if (!source || !source->isLoaded()) THROW_MPP("Specular prefilter source must be a loaded cubemap texture.", __LINE__, __FILE__, __func__);
		// Not fatal: the prefilter still runs, but every GGX sample resolves at
		// full resolution and high-dynamic-range sources alias into fireflies.
		if (source->getMipLevels() <= 1)
			warnMessage("Specular prefilter source '" + source->getName() + "' has no mip chain; '" + generatedName +
				"' will alias at high roughness. Generate the environment cubemap with a full chain.");
		if (generatedName.empty() || !faceSize || mipLevels < 2 || !sampleCount) THROW_MPP("Specular prefilter output name, face size, mip count (at least two), and sample count must be valid.", __LINE__, __FILE__, __func__);
		if (source->getTextureTarget() != GL_TEXTURE_CUBE_MAP) THROW_MPP("Specular prefilter source must be a cubemap texture.", __LINE__, __FILE__, __func__);
		auto format = source->getInternalFormat();
		if (format != GL_RGB16F && format != GL_RGBA16F && format != GL_RGB32F && format != GL_RGBA32F)
			THROW_MPP("Specular prefilter source must use a linear floating-point RGB/RGBA format.", __LINE__, __FILE__, __func__);
	}

	void RenderSystem::validateDiffuseIrradianceSource(Texture const* source, string const& generatedName, uint32_t faceSize, uint32_t sampleCount) const
	{
		if (!source || !source->isLoaded()) THROW_MPP("Diffuse irradiance source must be a loaded cubemap texture.", __LINE__, __FILE__, __func__);
		if (generatedName.empty() || !faceSize || !sampleCount) THROW_MPP("Diffuse irradiance output name, face size, and sample count must be non-zero.", __LINE__, __FILE__, __func__);
		if (source->getTextureTarget() != GL_TEXTURE_CUBE_MAP) THROW_MPP("Diffuse irradiance source must be a cubemap texture.", __LINE__, __FILE__, __func__);
		auto format = source->getInternalFormat();
		if (format != GL_RGB16F && format != GL_RGBA16F && format != GL_RGB32F && format != GL_RGBA32F)
			THROW_MPP("Diffuse irradiance source must use a linear floating-point RGB/RGBA format.", __LINE__, __FILE__, __func__);
	}

	void RenderSystem::renderPrefilteredSpecularFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t mipLevel, float roughness, uint32_t sampleCount)
	{
		auto target = dynamic_cast<RenderTexture*>(destination.get());
		if (!source || !target || source == target || target->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP || mipLevel >= target->getMipLevels())
			THROW_MPP("Specular prefilter requires distinct cubemap source, destination, and valid mip.", __LINE__, __FILE__, __func__);
		CubemapFaceRenderScope scope(*this, destination, face, mipLevel);
		pushModelMatrix(); pushCameraMatrix(); pushProjectionMatrix();
		try
		{
			setProjection2dOrthographic(); resetTransform();
			auto dimension = (float)std::max<size_t>(1, target->getWidth() >> mipLevel);
			scaleTransform2d(glm::vec2(dimension / (float)getWindowWidth(), dimension / (float)getWindowHeight()));
			flushVertexBuffers();
			auto program = static_cast<Program*>(mPrefilteredSpecularProgram.get()); setUsedProgram(mPrefilteredSpecularProgram);
			GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
			GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), dimension * 0.5f, dimension * 0.5f));
			GL_CHECK(glUniform1i(program->getUniformId("ENVIRONMENT"), 0)); GL_CHECK(glUniform1i(program->getUniformId("FACE"), (GLint)face)); GL_CHECK(glUniform1i(program->getUniformId("SAMPLE_COUNT"), (GLint)sampleCount));
			GL_CHECK(glUniform1f(program->getUniformId("ROUGHNESS"), glm::clamp(roughness, 0.0f, 1.0f))); GL_CHECK(glUniform1f(program->getUniformId("SOURCE_RESOLUTION"), (float)source->getWidth()));
			GL_CHECK(glUniform2f(program->getUniformId("OUTPUT_SIZE"), dimension, dimension));
			source->bind(0); auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0); mesh->bind(true); mesh->render(1); mesh->bind(false);
			mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++; popModelMatrix(); popCameraMatrix(); popProjectionMatrix();
		}
		catch (...) { popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); throw; }
	}

	void RenderSystem::renderDiffuseIrradianceFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t sampleCount)
	{
		auto target = dynamic_cast<RenderTexture*>(destination.get());
		if (!source || !target || source == target || target->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP)
			THROW_MPP("Diffuse irradiance requires distinct cubemap source and destination.", __LINE__, __FILE__, __func__);
		CubemapFaceRenderScope scope(*this, destination, face, 0);
		pushModelMatrix(); pushCameraMatrix(); pushProjectionMatrix();
		try
		{
			setProjection2dOrthographic(); resetTransform();
			auto dimension = (float)target->getWidth();
			scaleTransform2d(glm::vec2(dimension / (float)getWindowWidth(), dimension / (float)getWindowHeight()));
			flushVertexBuffers();
			auto program = static_cast<Program*>(mDiffuseIrradianceProgram.get());
			setUsedProgram(mDiffuseIrradianceProgram);
			GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
			GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), dimension * 0.5f, dimension * 0.5f));
			GL_CHECK(glUniform1i(program->getUniformId("ENVIRONMENT"), 0));
			GL_CHECK(glUniform1i(program->getUniformId("FACE"), (GLint)face));
			GL_CHECK(glUniform1i(program->getUniformId("SAMPLE_COUNT"), (GLint)sampleCount));
			GL_CHECK(glUniform2f(program->getUniformId("OUTPUT_SIZE"), dimension, dimension));
			source->bind(0);
			auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0); mesh->bind(true); mesh->render(1); mesh->bind(false);
			mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
			popModelMatrix(); popCameraMatrix(); popProjectionMatrix();
		}
		catch (...) { popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); throw; }
	}

	void RenderSystem::renderEquirectangularCubemapFace(Texture* source, RenderTargetPtr const& destination, uint32_t face, uint32_t mipLevel)
	{
		auto target = dynamic_cast<RenderTexture*>(destination.get());
		if (!source || !target || target->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP)
			THROW_MPP("Equirectangular conversion requires a source texture and cubemap destination.", __LINE__, __FILE__, __func__);
		CubemapFaceRenderScope scope(*this, destination, face, mipLevel);
		pushModelMatrix(); pushCameraMatrix(); pushProjectionMatrix();
		try
		{
			setProjection2dOrthographic(); resetTransform();
			auto dimension = (float)std::max<size_t>(1, target->getWidth() >> mipLevel);
			scaleTransform2d(glm::vec2(dimension / (float)getWindowWidth(), dimension / (float)getWindowHeight()));
			flushVertexBuffers();
			auto program = static_cast<Program*>(mEquirectangularToCubemapProgram.get());
			setUsedProgram(mEquirectangularToCubemapProgram);
			GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
			GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), dimension * 0.5f, dimension * 0.5f));
			GL_CHECK(glUniform1i(program->getUniformId("EQUIRECTANGULAR"), 0));
			GL_CHECK(glUniform1i(program->getUniformId("FACE"), (GLint)face));
			GL_CHECK(glUniform2f(program->getUniformId("OUTPUT_SIZE"), dimension, dimension));
			source->bind(0);
			auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
			mesh->bind(true); mesh->render(1); mesh->bind(false);
			mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
			popModelMatrix(); popCameraMatrix(); popProjectionMatrix();
		}
		catch (...) { popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); throw; }
	}

	RenderTargetPtr RenderSystem::createIblCubemap(string const& name, size_t faceSize, uint32_t mipLevels, uint32_t internalFormat)
	{
		if (name.empty() || !faceSize || !mipLevels) THROW_MPP("IBL cubemap name, face size, and mip level count must be non-zero.", __LINE__, __FILE__, __func__);
		if (internalFormat != GL_RGB16F && internalFormat != GL_RGBA16F && internalFormat != GL_RGB32F && internalFormat != GL_RGBA32F)
			THROW_MPP("IBL cubemap format must be RGB/RGBA 16F or 32F.", __LINE__, __FILE__, __func__);
		RenderTextureOptions options;
		options.target = TextureTarget::CubeMap;
		options.mipLevels = mipLevels;
		options.colourInternalFormat = internalFormat;
		options.colourNormalised = false;
		options.params.wrap = GL_CLAMP_TO_EDGE;
		options.params.minFilter = mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
		options.params.magFilter = GL_LINEAR;
		options.params.useMipmaps = false;
		options.params.lodBaseLevel = 0;
		options.params.lodMaxLevel = (int32_t)mipLevels - 1;
		return createRenderTexture(name, faceSize, faceSize, options);
	}

	RenderTargetPtr RenderSystem::generatePrefilteredSpecular(Texture* environmentCubemap, string const& generatedName, uint32_t faceSize, uint32_t mipLevels, uint32_t sampleCount)
	{
		validatePrefilteredSpecularSource(environmentCubemap, generatedName, faceSize, mipLevels, sampleCount);
		auto candidate = createIblCubemap(generatedName, faceSize, mipLevels);
		if (dynamic_cast<Texture*>(candidate.get()) == environmentCubemap) THROW_MPP("Specular prefilter source and output cannot alias.", __LINE__, __FILE__, __func__);
		for (uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			float roughness = (float)mip / (float)(mipLevels - 1);
			for (uint32_t face = 0; face < 6; ++face)
				renderPrefilteredSpecularFace(environmentCubemap, candidate, face, mip, roughness, sampleCount);
		}
		return candidate;
	}

	RenderTargetPtr RenderSystem::generateDiffuseIrradiance(Texture* environmentCubemap, string const& generatedName, uint32_t faceSize, uint32_t sampleCount)
	{
		validateDiffuseIrradianceSource(environmentCubemap, generatedName, faceSize, sampleCount);
		auto candidate = createIblCubemap(generatedName, faceSize, 1);
		if (dynamic_cast<Texture*>(candidate.get()) == environmentCubemap) THROW_MPP("Diffuse irradiance source and output cannot alias.", __LINE__, __FILE__, __func__);
		for (uint32_t face = 0; face < 6; ++face)
			renderDiffuseIrradianceFace(environmentCubemap, candidate, face, sampleCount);
		return candidate;
	}

	RenderTargetPtr RenderSystem::convertEquirectangularToCubemap(Texture* hdrEquirectangular, string const& generatedName, uint32_t faceSize, uint32_t mipLevels)
	{
		validateEquirectangularConversionSource(hdrEquirectangular, generatedName, faceSize, mipLevels);
		auto candidate = createIblCubemap(generatedName, faceSize, mipLevels);
		for (uint32_t face = 0; face < 6; ++face)
			renderEquirectangularCubemapFace(hdrEquirectangular, candidate, face, 0);
		// The specular prefilter selects a source mip from each GGX sample's solid
		// angle to avoid resolving a bright pixel per sample. That LOD silently
		// clamps to zero unless the environment carries a real chain, so build it
		// here, once every face exists rather than on each face's target pop.
		if (mipLevels > 1) static_cast<RenderTexture*>(candidate.get())->generateMipMaps(true);
		return candidate;
	}

	ResourcePtr RenderSystem::getOrCreatePbrBrdfIntegrationLut()
	{
		if (mPbrBrdfIntegrationLut) return mPbrBrdfIntegrationLut;
		// The shader looks this up as (nDotV, roughness). Both axes reach 1.0 -- a
		// surface facing the camera is the commonest case of all -- so the default
		// GL_REPEAT would make the bilinear tap at u=1 blend the head-on texel with
		// the grazing-incidence texel at u=0, whose scale/bias are nothing alike.
		RenderTextureOptions options; options.colourInternalFormat = GL_RG16F; options.params.minFilter = GL_LINEAR; options.params.magFilter = GL_LINEAR; options.params.wrap = GL_CLAMP_TO_EDGE; options.params.useMipmaps = false;
		auto candidate = createRenderTexture("__mpp_ibl_brdf_integration_lut__", 512, 512, options);
		GLint viewport[4]{ mViewportX, mViewportY, static_cast<GLint>(mViewportWidth), static_cast<GLint>(mViewportHeight) }, drawBuffer = 0, readBuffer = 0;
		auto const rasterState = captureRasterState(1);
		auto const scissor = rasterState.scissorRectangle;
		GL_CHECK(glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer)); GL_CHECK(glGetIntegerv(GL_READ_BUFFER, &readBuffer));
		pushRenderTarget(candidate); pushModelMatrix(); pushCameraMatrix(); pushProjectionMatrix();
		try
		{
			// The shared fullscreen quad is authored in window pixels and the fullscreen
			// vertex shader divides by HALF_WINDOW_SIZE to reach NDC, so both the model
			// scale and that uniform have to be retargeted at 512. Leaving the uniform
			// unset divides by zero and the quad never rasterizes, which is what left
			// this LUT entirely black. Every other offscreen pass does the same pair.
			setViewport(0, 0, 512, 512); setProjection2dOrthographic(); resetTransform();
			scaleTransform2d(glm::vec2(512.0f / (float)getWindowWidth(), 512.0f / (float)getWindowHeight())); flushVertexBuffers();
			auto program = static_cast<Program*>(mPbrBrdfIntegrationProgram.get()); setUsedProgram(mPbrBrdfIntegrationProgram);
			GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix))); GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), 256.0f, 256.0f)); GL_CHECK(glUniform1i(program->getUniformId("SAMPLE_COUNT"), 1024)); GL_CHECK(glUniform2f(program->getUniformId("OUTPUT_SIZE"), 512.0f, 512.0f));
			auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0); mesh->bind(true); mesh->render(1); mesh->bind(false); mRenderInfo.programSwitches++; mRenderInfo.fullscreenQuads++;
			popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); popRenderTarget();
		}
		catch (...) { popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); popRenderTarget(); setViewport(viewport[0], viewport[1], viewport[2], viewport[3]); setScissorRectangleState(scissor); GL_CHECK(glDrawBuffer((GLenum)drawBuffer)); GL_CHECK(glReadBuffer((GLenum)readBuffer)); setScissorState(rasterState.scissor); throw; }
		setViewport(viewport[0], viewport[1], viewport[2], viewport[3]); setScissorRectangleState(scissor); GL_CHECK(glDrawBuffer((GLenum)drawBuffer)); GL_CHECK(glReadBuffer((GLenum)readBuffer)); setScissorState(rasterState.scissor);
		mPbrBrdfIntegrationLut = std::static_pointer_cast<Resource>(std::dynamic_pointer_cast<RenderTexture>(candidate));
		return mPbrBrdfIntegrationLut;
	}

	RenderTargetPtr RenderSystem::createPhysicalRenderTexture(string const& name,size_t width,size_t height,RenderTextureOptions const& options,uint32_t samples)
	{
		if(samples==0||!mCaps.supportsMsaa(samples))THROW_MPP("Unsupported physical render-texture sample count "+to_string(samples)+".",__LINE__,__FILE__,__func__);
		if(!options.mipLevels)THROW_MPP("Render texture mip level count must be non-zero.",__LINE__,__FILE__,__func__);
		if(options.target==TextureTarget::CubeMap){if(samples!=1)THROW_MPP("Cubemap render textures cannot be multisampled.",__LINE__,__FILE__,__func__);if(width!=height)THROW_MPP("Cubemap render textures require square faces.",__LINE__,__FILE__,__func__);bool depth=options.depthAttachment!=RenderTextureDepthAttachment::None;if(depth&&(options.numAttachments!=0||options.depthAttachment!=RenderTextureDepthAttachment::DepthTexture||options.depthFormat!=RenderTextureDepthFormat::Depth24))THROW_MPP("Cubemap depth targets require one depth-only Depth24 texture.",__LINE__,__FILE__,__func__);if(!depth&&options.numAttachments&&options.colourType!=TextureInternalType::Float&&options.colourInternalFormat==0)THROW_MPP("Cubemap render textures require a floating-point colour format.",__LINE__,__FILE__,__func__);}
		else if(options.target!=TextureTarget::Texture2D)THROW_MPP("Render textures support only Texture2D and CubeMap targets.",__LINE__,__FILE__,__func__);
		auto rtStream = new ProgrammaticRenderTextureStream(mResourceMgr);
		rtStream->mPhysicalSamples=samples;

		rtStream->setTarget(options.target);
		rtStream->setMipLevels(options.mipLevels);
		if (options.colourInternalFormat != 0) rtStream->setInternalFormat(options.colourInternalFormat);
		else rtStream->setInternalFormat(options.colourType, options.colourNormalised, options.colourBitSize, options.colourChannels);
		rtStream->setParams(options.params);
		rtStream->setWidth(width);
		rtStream->setHeight(height);
		rtStream->setDepthAttachment(options.depthAttachment);
		rtStream->setDepthParams(options.depthParams);
		rtStream->setDepthFormat(options.depthFormat);
		rtStream->setNumAttachments(options.numAttachments);

		auto rt = new RenderTexture(name, this, mResourceMgr, ResourceStreamPtr(rtStream));
		rt->load();

		return RenderTargetPtr(rt);
	}

	/*
	 * Clip the screen.
	 *
	 */
	void RenderSystem::pushClipRectangle(ClipRectangle const& clipRect)
	{
		ClipRectangle cr = clipRect;

		if (!mClipStack.empty())
		{
			cr = clipRect.intersect(mClipStack.top());
		}

		mClipStack.push(cr);
		setScissorRectangleState({ static_cast<uint32_t>(cr.x), static_cast<uint32_t>(cr.y), static_cast<uint32_t>(cr.width), static_cast<uint32_t>(cr.height) });
	}

	/*
	 * Un-clip the screen.
	 *
	 */
	void RenderSystem::popClipRectangle()
	{
		assert(!mClipStack.empty() && "RenderSystem::popClipRectangle() 'mClipStack' is empty.");
		assert(mRenderTarget && "RenderSystem::popClipRectangle() No active RenderTarget.");
		
		mClipStack.pop();
		if (mClipStack.empty())
		{
			// Set to target size.
			setScissorRectangleState({ 0, 0, static_cast<uint32_t>(mRenderTarget->getWidth()), static_cast<uint32_t>(mRenderTarget->getHeight()) });
		}
		else
		{
			ClipRectangle const& cr = mClipStack.top();
			setScissorRectangleState({ static_cast<uint32_t>(cr.x), static_cast<uint32_t>(cr.y), static_cast<uint32_t>(cr.width), static_cast<uint32_t>(cr.height) });
		}
	}

	void RenderSystem::setViewport(int x, int y, size_t width, size_t height)
	{
		mViewportX = x;
		mViewportY = y;
		mViewportWidth = width;
		mViewportHeight = height;
		GL_CHECK(glViewport(x, y, (GLsizei)mViewportWidth, (GLsizei)mViewportHeight));
		setScissorRectangleState({ static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(mViewportWidth), static_cast<uint32_t>(mViewportHeight) });
	}

	void RenderSystem::resetViewport()
	{
		setViewport(0, 0, mRenderTarget->getWidth(), mRenderTarget->getHeight());
	}

	/*
	 * Reset the model matrix to identity.
	 *
	 */
	void RenderSystem::resetTransform()
	{
		m3dModelMatrix = glm::mat4();

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Translate the model matrix in 3d.
	 *
	 */
	void RenderSystem::translateTransform3d(glm::vec3 const& vec)
	{
		m3dModelMatrix = glm::translate(m3dModelMatrix, vec);

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Rotate the model matrix in 3d.
	 *
	 */
	void RenderSystem::rotateTransform3d(float angle, glm::vec3 const& axis)
	{
		m3dModelMatrix = glm::rotate(m3dModelMatrix, glm::radians(angle), axis);

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Scale the model matrix in 3d.
	 *
	 */
	void RenderSystem::scaleTransform3d(glm::vec3 const& vec)
	{
		m3dModelMatrix = glm::scale(m3dModelMatrix, vec);

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Push the current camera matrix.
	 *
	 */
	void RenderSystem::pushCameraMatrix()
	{
		m3dCameraMatrixStack.push(m3dCameraMatrix);
	}

	/*
	 * Pop the 3d camera matrix.
	 *
	 */
	void RenderSystem::popCameraMatrix()
	{
		assert(!m3dCameraMatrixStack.empty() && "RenderSystem::popCameraMatrix3d() 'm3dCameraMatrixStack' is empty.");

		m3dCameraMatrix = m3dCameraMatrixStack.top();
		m3dCameraMatrixStack.pop();

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Get the current 3d camera matrix.
	 *
	 */
	glm::mat4 const& RenderSystem::getCameraMatrix() const
	{
		return m3dCameraMatrix;
	}

	/*
	 * Set 3d camera.
	 *
	 */
	void RenderSystem::setCamera3d(glm::vec3 const& position, glm::vec3 const& target, glm::vec3 const& up)
	{
		m3dCameraMatrix = glm::lookAt(position, target, up);

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Push the current 3d projection matrix.
	 *
	 */
	void RenderSystem::pushProjectionMatrix()
	{
		m3dProjectionMatrixStack.push(m3dProjectionMatrix);
	}

	/*
	 * Pop the 3d projection matrix.
	 *
	 */
	void RenderSystem::popProjectionMatrix()
	{
		assert(!m3dProjectionMatrixStack.empty() && "RenderSystem::popProjectionMatrix3d() 'm3dProjectionMatrixStack' is empty.");

		m3dProjectionMatrix = m3dProjectionMatrixStack.top();
		m3dProjectionMatrixStack.pop();

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}
	
	/*
	 * Get the current 3d projection matrix.
	 *
	 */
	glm::mat4 const& RenderSystem::getProjectionMatrix() const
	{
		return m3dProjectionMatrix;
	}

	/*
	 * Set 3d projection.
	 *
	 */
	void RenderSystem::setProjection3dPerspective(float fov, float nearDist, float farDist)
	{
		flushVertexBuffers();

		setDepthTestState(true);

		mProjectionType = ProjectionType::Perspective3D;
		mFarPlaneDistance = farDist;

		// Reset camera and model matrices
		m3dCameraMatrix = glm::mat4();
		m3dModelMatrix = glm::mat4();

		// Recalculate combined matrix
		m3dProjectionMatrix = glm::perspective(glm::radians(fov), getAspectRatio(), nearDist, farDist);
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Push the current model matrix.
	 *
	 */
	void RenderSystem::pushModelMatrix()
	{
		m3dModelMatrixStack.push(m3dModelMatrix);
	}

	/*
	 * Pop the 3d model matrix.
	 *
	 */
	void RenderSystem::popModelMatrix()
	{
		assert(!m3dModelMatrixStack.empty() && "RenderSystem::popModelMatrix3d() 'm3dModelMatrixStack' is empty.");

		m3dModelMatrix = m3dModelMatrixStack.top();
		m3dModelMatrixStack.pop();

		// Recalculate combined matrix
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Get the current 3d model matrix.
	 *
	 */
	glm::mat4 const& RenderSystem::getModelMatrix() const
	{
		return m3dModelMatrix;
	}

	/*
	 * Get the current normal matrix.
	 *
	 */
	glm::mat3 RenderSystem::getNormalMatrix() const
	{
		return glm::transpose(glm::inverse(glm::mat3(m3dModelMatrix)));
	}

	/*
	 * Get the combined model-camera-projection matrix.
	 *
	 */
	glm::mat4 const& RenderSystem::getModelCameraProjectionMatrix() const
	{
		return m3dModelCameraProjectionMatrix;
	}

	/*
	 * Set 2d projection.
	 *
	 */
	void RenderSystem::setProjection2dOrthographic()
	{
		flushVertexBuffers();

		setDepthTestState(false);
		setCullState(GraphCullMode::None);
		setScissorState(false);
		setFillModeState(GraphFillMode::Fill);
		setAllColourMasksState({});
		if (mTextAsPoints)
		{
			setProgramPointSizeState(true);
			// GL_POINT_SPRITE is removed from core OpenGL profiles. Point sprites
			// use gl_PointCoord automatically there, so enabling that legacy state
			// produces GL_INVALID_ENUM without affecting rendering.
		}

		mProjectionType = ProjectionType::Ortho2D;

		// Reset camera and model matrices
		m3dCameraMatrix = glm::mat4();
		m3dModelMatrix = glm::mat4();

		// Recalculate combined matrix
		m3dProjectionMatrix = glm::mat4();
		m3dModelCameraProjectionMatrix = m3dProjectionMatrix * m3dCameraMatrix * m3dModelMatrix;
	}

	/*
	 * Translate the model matrix in 2d.
	 *
	 */
	void RenderSystem::translateTransform2d(glm::vec2 const& vec)
	{
		translateTransform3d(glm::vec3(vec.x, vec.y, 0.0f));
	}

	/*
	 * Rotate the model matrix in 2d.
	 *
	 */
	void RenderSystem::rotateTransform2d(float angle)
	{
		rotateTransform3d(angle, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	/*
	 * Scale the model matrix in 2d.
	 * 
	 */
	void RenderSystem::scaleTransform2d(glm::vec2 const& vec)
	{
		scaleTransform3d(glm::vec3(vec.x, vec.y, 1.0f));
	}

	void RenderSystem::setAmbientColour(Colour const& colour)
	{
		const uint32_t offset{ 0 };
		auto fp = (float*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Ambient
		*fp++ = colour.red;
		*fp++ = colour.green;
		*fp++ = colour.blue;

		mLightsBuffer->updateData(offset * sizeof(float), 12);
	}

	void RenderSystem::setLight1Position(glm::vec3 const& pos)
	{
		const uint32_t offset{ 4 };
		auto fp = (float*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Light1 position
		*fp++ = pos.x;
		*fp++ = pos.y;
		*fp++ = pos.z;

		mLightsBuffer->updateData(offset * sizeof(float), 12);
	}

	void RenderSystem::setLight1Colour(Colour const& colour)
	{
		const uint32_t offset{ 8 };
		auto fp = (float*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Light1 position
		*fp++ = colour.red;
		*fp++ = colour.green;
		*fp++ = colour.blue;

		mLightsBuffer->updateData(offset * sizeof(float), 12);
	}

	void RenderSystem::setLight2Position(glm::vec3 const& pos)
	{
		const uint32_t offset{ 12 };
		auto fp = (float*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Light1 position
		*fp++ = pos.x;
		*fp++ = pos.y;
		*fp++ = pos.z;

		mLightsBuffer->updateData(offset * sizeof(float), 12);
	}

	void RenderSystem::setLight2Colour(Colour const& colour)
	{
		const uint32_t offset{ 16 };
		auto fp = (float*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Light2 position
		*fp++ = colour.red;
		*fp++ = colour.green;
		*fp++ = colour.blue;

		mLightsBuffer->updateData(offset * sizeof(float), 12);
	}

	void RenderSystem::setLightCount(size_t count)
	{
		const uint32_t offset{ 20 };

		auto fp = (int*)(&mLightsBuffer->getBufferData()[0]);
		fp += offset;

		// Count
		*fp = (int)count;

		mLightsBuffer->updateData(offset * sizeof(float), 4);
	}

	/*
	 * Clear the screen to the specified colour.
	 *
	 */
	void RenderSystem::clearScreen(Colour const& colour)
	{
		GL_CHECK(glClearColor(colour.red, colour.green, colour.blue, colour.alpha));

		setScissorState(true);
		GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
		setScissorState(false);
	}

	void RenderSystem::setGamma(float gamma)
	{
		mGamma = gamma;
	}

	float RenderSystem::getGamma() const
	{
		return mGamma;
	}

	/*
	 * Render a model.
	 *
	 */
	ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend)
	{
		return renderModelBatched(model, alphaBlend, glm::vec3(0.0f, 0.0f, 0.0f), m3dModelMatrix, m3dModelCameraProjectionMatrix);
	}

	ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, CameraPtr camera)
	{
		return renderModelBatched(model, alphaBlend, glm::vec3(0.0f, 0.0f, 0.0f), m3dModelMatrix, camera->getProjectionTransform() * camera->getViewTransform());
	}

	ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos)
	{
		return renderModelBatched(model, alphaBlend, viewPos, m3dModelMatrix, m3dModelCameraProjectionMatrix);
	}

	ModelInstance* RenderSystem::renderModelBatched(Model const& model, glm::mat4 const& transform, CameraPtr camera)
	{
		return renderModelBatched(model, true, camera->getPosition(), transform, camera->getProjectionTransform() * camera->getViewTransform() * transform);
	}

	ModelInstance* RenderSystem::renderModelBatched(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, glm::mat4 const& transform, glm::mat4 const& mcp)
	{
		auto modelInstance = mModelInstances->acquireObject();
		
		modelInstance->setup(
			model,
			viewPos,
			transform,
			mcp,
			glm::transpose(glm::inverse(glm::mat3(transform))),
			glm::vec2(mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f),
			getGamma(),
			mMeshInstances);

		auto& instances = modelInstance->getMeshInstances();
		for (auto& instance : instances)
		{
			instance->blend(alphaBlend);
		}

		return modelInstance;
	}

	/*
	 * Render a model.
	 *
	 */
	void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, glm::vec3 const& viewPos, shared_ptr<ModelRenderParams> params)
	{
		renderModelBatched(model, alphaBlend, viewPos)->setParams(params);
		flushVertexBuffers();
	}

	void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, shared_ptr<ModelRenderParams> params)
	{
		renderModelBatched(model, alphaBlend)->setParams(params);
		flushVertexBuffers();
	}

	void RenderSystem::renderModelImmediate(Model const& model, bool alphaBlend, shared_ptr<ModelRenderParams> params, CameraPtr camera)
	{
		renderModelBatched(model, alphaBlend, camera)->setParams(params);
		flushVertexBuffers();
	}

	/*
	 * Set up the model used for rendering text
	 *
	 */
	int RenderSystem::buildTextVertexBuffer(VertexBuffer* buffer, string const& text, int& offset, int x, int y)
	{
		char const* textPtr = text.c_str();
		int numChars = (int)strlen(textPtr);

		int vertexStride = (int)buffer->getVertexStride() / sizeof(float);

		vector<int8_t>& bufferData = buffer->getBufferData();

		float xpos = (float)x + 8; // 8 to offset default kerning
		float ypos = (float)y;

		if (mTextAsPoints)
		{
			auto glyphOffset = (uint32_t)(offset + numChars);
			if (glyphOffset > MaxTextGlyphs)
			{
				return 0;
			}

			float* bufferPtr = (float*)&(bufferData[offset * vertexStride * sizeof(float)]);

			for (int i = 0; i < numChars; ++i)
			{
				Font::Glyph const& glyph = mInternalFont->getGlyph(textPtr[i]);
				xpos += glyph.kern / 2;

				// Set data
				bufferPtr[0] = xpos;
				bufferPtr[1] = ypos + glyph.raise;
				bufferPtr[2] = glyph.u0_;
				bufferPtr[3] = glyph.v0_;
				bufferPtr[4] = glyph.u1_;
				bufferPtr[5] = glyph.v1_;

				bufferPtr += vertexStride;

				xpos += (glyph.width + glyph.kern / 2);
			}

			offset += numChars;
			return numChars;
		}
		else
		{
			auto glyphOffset = (uint32_t)(offset + numChars * 6);
			if (glyphOffset > MaxTextGlyphs * 6)
			{
				return 0;
			}

			float* bufferPtr = (float*)&(bufferData[offset * vertexStride * sizeof(float)]);

			for (int i = 0; i < numChars; ++i)
			{
				Font::Glyph const& glyph = mInternalFont->getGlyph(textPtr[i]);
				xpos += glyph.kern / 2;

				// Set data
				bufferPtr[0] = xpos;
				bufferPtr[1] = ypos + glyph.raise;
				bufferPtr[2] = glyph.u0_;
				bufferPtr[3] = glyph.v0_;

				bufferPtr[4] = xpos;
				bufferPtr[5] = ypos + glyph.height + glyph.raise;
				bufferPtr[6] = glyph.u0_;
				bufferPtr[7] = glyph.v1_;

				bufferPtr[8] = xpos + glyph.width;
				bufferPtr[9] = ypos + glyph.height + glyph.raise;
				bufferPtr[10] = glyph.u1_;
				bufferPtr[11] = glyph.v1_;

				bufferPtr[12] = xpos + glyph.width;
				bufferPtr[13] = ypos + glyph.height + glyph.raise;
				bufferPtr[14] = glyph.u1_;
				bufferPtr[15] = glyph.v1_;

				bufferPtr[16] = xpos + glyph.width;
				bufferPtr[17] = ypos + glyph.raise;
				bufferPtr[18] = glyph.u1_;
				bufferPtr[19] = glyph.v0_;

				bufferPtr[20] = xpos;
				bufferPtr[21] = ypos + glyph.raise;
				bufferPtr[22] = glyph.u0_;
				bufferPtr[23] = glyph.v0_;

				bufferPtr += vertexStride * 6;

				xpos += (glyph.width + glyph.kern / 2);
			}

			offset += numChars * 6;
			return numChars * 2;
		}
	}

	/*
	 * Set up the model used for rendering text
	 *
	 */
	int RenderSystem::buildColouredTextVertexBuffer(VertexBuffer* buffer, string const& text, int& offset, int x, int y)
	{
		int vertexStride = (int)buffer->getVertexStride() / sizeof(float);
		vector<int8_t>& bufferData = buffer->getBufferData();
		float xpos = (float)x + 8; // 8 to offset default kerning
		float ypos = (float)y;
		Colour colour = Colour::White;

		if (mTextAsPoints)
		{
			auto glyphs = parseColouredGlyphs(text);
			auto glyphOffset = (uint32_t)(offset + glyphs.size());
			if (glyphOffset > MaxTextGlyphs)
			{
				return 0;
			}

			float* bufferPtr = (float*)&(bufferData[offset * vertexStride * sizeof(float)]);
			for (auto const& colouredGlyph : glyphs)
			{
				Font::Glyph const& glyph = mInternalFont->getGlyph(colouredGlyph.character);
				auto const& glyphColour = colouredGlyph.colour;
				xpos += glyph.kern / 2;

				bufferPtr[0] = xpos;
				bufferPtr[1] = ypos + glyph.raise;
				bufferPtr[2] = glyph.u0_;
				bufferPtr[3] = glyph.v0_;
				bufferPtr[4] = glyph.u1_;
				bufferPtr[5] = glyph.v1_;
				bufferPtr[6] = glyphColour.red;
				bufferPtr[7] = glyphColour.green;
				bufferPtr[8] = glyphColour.blue;
				bufferPtr[9] = glyphColour.alpha;
				bufferPtr += vertexStride;
				xpos += glyph.width + glyph.kern / 2;
			}

			offset += static_cast<int>(glyphs.size());
			return static_cast<int>(glyphs.size());
		}
		else
		{
			auto glyphs = parseColouredGlyphs(text);
			auto glyphOffset = (uint32_t)(offset + glyphs.size() * 6);
			if (glyphOffset > MaxTextGlyphs * 6)
			{
				return 0;
			}

			float* bufferPtr = (float*)&(bufferData[offset * vertexStride * sizeof(float)]);

			for (auto const& colouredGlyph : glyphs)
			{
				Font::Glyph const& glyph = mInternalFont->getGlyph(colouredGlyph.character);
				colour = colouredGlyph.colour;
				xpos += glyph.kern / 2;

				// Set data
				bufferPtr[0] = xpos;
				bufferPtr[1] = ypos + glyph.raise;
				bufferPtr[2] = glyph.u0_;
				bufferPtr[3] = glyph.v0_;
				bufferPtr[4] = colour.red;
				bufferPtr[5] = colour.green;
				bufferPtr[6] = colour.blue;
				bufferPtr[7] = colour.alpha;

				bufferPtr[8] = xpos;
				bufferPtr[9] = ypos + glyph.height + glyph.raise;
				bufferPtr[10] = glyph.u0_;
				bufferPtr[11] = glyph.v1_;
				bufferPtr[12] = colour.red;
				bufferPtr[13] = colour.green;
				bufferPtr[14] = colour.blue;
				bufferPtr[15] = colour.alpha;

				bufferPtr[16] = xpos + glyph.width;
				bufferPtr[17] = ypos + glyph.height + glyph.raise;
				bufferPtr[18] = glyph.u1_;
				bufferPtr[19] = glyph.v1_;
				bufferPtr[20] = colour.red;
				bufferPtr[21] = colour.green;
				bufferPtr[22] = colour.blue;
				bufferPtr[23] = colour.alpha;

				bufferPtr[24] = xpos + glyph.width;
				bufferPtr[25] = ypos + glyph.height + glyph.raise;
				bufferPtr[26] = glyph.u1_;
				bufferPtr[27] = glyph.v1_;
				bufferPtr[28] = colour.red;
				bufferPtr[29] = colour.green;
				bufferPtr[30] = colour.blue;
				bufferPtr[31] = colour.alpha;

				bufferPtr[32] = xpos + glyph.width;
				bufferPtr[33] = ypos + glyph.raise;
				bufferPtr[34] = glyph.u1_;
				bufferPtr[35] = glyph.v0_;
				bufferPtr[36] = colour.red;
				bufferPtr[37] = colour.green;
				bufferPtr[38] = colour.blue;
				bufferPtr[39] = colour.alpha;

				bufferPtr[40] = xpos;
				bufferPtr[41] = ypos + glyph.raise;
				bufferPtr[42] = glyph.u0_;
				bufferPtr[43] = glyph.v0_;
				bufferPtr[44] = colour.red;
				bufferPtr[45] = colour.green;
				bufferPtr[46] = colour.blue;
				bufferPtr[47] = colour.alpha;

				bufferPtr += vertexStride * 6;

				xpos += (glyph.width + glyph.kern / 2);
			}

			offset += static_cast<int>(glyphs.size() * 6);
			return static_cast<int>(glyphs.size() * 2);
		}
	}

	void RenderSystem::createLightsData()
	{
		if (mLightsBuffer)
		{
			delete mLightsBuffer;
		}

		// Set up lights uniform buffer
		const size_t uniformSize{ 96 };
		shared_ptr<const int8_t> uniformData(new int8_t[uniformSize], [](int8_t *p) { delete[] p; });

		auto fp = (float*)uniformData.get();

		// Ambient
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		fp++;

		// Light1 position
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		fp++;

		// Light1 colour
		*fp++ = 1.0f;
		*fp++ = 1.0f;
		*fp++ = 1.0f;
		fp++;

		// Light2 position
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		*fp++ = 0.0f;
		fp++;

		// Light2 colour
		*fp++ = 1.0f;
		*fp++ = 1.0f;
		*fp++ = 1.0f;
		fp++;

		// Count
		*(int32_t*)(fp) = 0;

		mLightsBuffer = new UniformBuffer(this, uniformData, uniformSize, 0);
		mLightsBuffer->load();
	}

	void RenderSystem::destroyLightsData()
	{
		if (mLightsBuffer)
		{
			mLightsBuffer->unload();
			delete mLightsBuffer;
			mLightsBuffer = nullptr;
		}
	}

	void RenderSystem::createPbrLightsData()
	{
		destroyPbrLightsData();

		// std140 layout: vec4 ambientAndCount, followed by MaxPbrLights entries
		// of vec4 colourIntensity, vec4 positionRange, vec4 directionType.
		const size_t uniformSize = 16 + MaxPbrLights * 48;
		shared_ptr<const int8_t> uniformData(new int8_t[uniformSize](), [](int8_t* p) { delete[] p; });
		auto fp = reinterpret_cast<float*>(const_cast<int8_t*>(uniformData.get()));
		fp[0] = 0.03f;
		fp[1] = 0.03f;
		fp[2] = 0.03f;
		fp[3] = 0.0f;

		mPbrLightsBuffer = new UniformBuffer(this, uniformData, uniformSize, 1);
		mPbrLightsBuffer->load();
	}

	void RenderSystem::destroyPbrLightsData()
	{
		if (mPbrLightsBuffer)
		{
			mPbrLightsBuffer->unload();
			delete mPbrLightsBuffer;
			mPbrLightsBuffer = nullptr;
		}
	}

	void RenderSystem::createCameraFrameData()
	{
		destroyCameraFrameData();

		// std140: three mat4 followed by two vec4. Every member is already a
		// multiple of 16 bytes, so the C++ side is a straight memcpy.
		const size_t uniformSize = 3 * 64 + 32;
		shared_ptr<const int8_t> uniformData(new int8_t[uniformSize](), [](int8_t* p) { delete[] p; });
		auto fp = reinterpret_cast<float*>(const_cast<int8_t*>(uniformData.get()));
		// Identity matrices rather than zeroes: an unpopulated frame must not make
		// a shader divide by a singular projection.
		for (int matrix = 0; matrix < 3; ++matrix)
			for (int diagonal = 0; diagonal < 4; ++diagonal) fp[matrix * 16 + diagonal * 5] = 1.0f;

		mCameraFrameBuffer = new UniformBuffer(this, uniformData, uniformSize, 3);
		mCameraFrameBuffer->load();
	}

	void RenderSystem::destroyCameraFrameData()
	{
		if (mCameraFrameBuffer)
		{
			mCameraFrameBuffer->unload();
			delete mCameraFrameBuffer;
			mCameraFrameBuffer = nullptr;
		}
	}

	void RenderSystem::setCameraFrame(glm::mat4 const& view, glm::mat4 const& projection,
		glm::vec2 const& viewportSize, float nearDistance, float farDistance, float seconds)
	{
		mCameraFrameView = view;
		mCameraFrameProjection = projection;
		if (!mCameraFrameBuffer) return;
		auto& data = mCameraFrameBuffer->getBufferData();
		auto fp = reinterpret_cast<float*>(data.data());
		auto const inverseProjection = glm::inverse(projection);
		memcpy(fp, glm::value_ptr(view), 64);
		memcpy(fp + 16, glm::value_ptr(projection), 64);
		memcpy(fp + 32, glm::value_ptr(inverseProjection), 64);
		fp[48] = viewportSize.x;
		fp[49] = viewportSize.y;
		fp[50] = viewportSize.x > 0.0f ? 1.0f / viewportSize.x : 0.0f;
		fp[51] = viewportSize.y > 0.0f ? 1.0f / viewportSize.y : 0.0f;
		fp[52] = nearDistance;
		fp[53] = farDistance;
		fp[54] = seconds;
		fp[55] = 0.0f;
		mCameraFrameBuffer->mapBufferData();
		// Particle proxy-light injection is view-local when a particle effect has
		// aggregate bounds, just like its draw-pass contributions.
		uploadPbrLights();
	}

	void RenderSystem::setPbrAmbientColour(Colour const& colour)
	{
		auto fp = reinterpret_cast<float*>(mPbrLightsBuffer->getBufferData().data());
		fp[0] = colour.red;
		fp[1] = colour.green;
		fp[2] = colour.blue;
		mPbrLightsBuffer->updateData(0, 12);
	}

	void RenderSystem::setPbrLights(vector<PbrLight> const& lights)
	{
		if (lights.size() > MaxPbrLights)
		{
			THROW_MPP("Too many PBR lights for the PBR light uniform buffer.", __LINE__, __FILE__, __func__);
		}
		mPbrLights = lights;
		uploadPbrLights();
	}

	void RenderSystem::uploadPbrLights()
	{
		if (!mPbrLightsBuffer) return;
		auto lights = mPbrLights;
		if (mParticleSystem && lights.size() < MaxPbrLights)
		{
			auto proxies = mParticleSystem->getProxyLights(MaxPbrLights - lights.size(), true);
			for (auto const& proxy : proxies) lights.push_back(proxy.light);
		}

		auto& data = mPbrLightsBuffer->getBufferData();
		fill(data.begin() + 16, data.end(), 0);
		auto fp = reinterpret_cast<float*>(data.data());
		fp[3] = (float)lights.size();
		for (size_t i = 0; i < lights.size(); ++i)
		{
			auto const& light = lights[i];
			fp += 4 + i * 12;
			fp[0] = light.colour.r;
			fp[1] = light.colour.g;
			fp[2] = light.colour.b;
			fp[3] = light.intensity;
			fp[4] = light.position.x;
			fp[5] = light.position.y;
			fp[6] = light.position.z;
			fp[7] = light.range;
			fp[8] = light.direction.x;
			fp[9] = light.direction.y;
			fp[10] = light.direction.z;
			fp[11] = light.type == PbrLightType::Point ? 1.0f : 0.0f;
			fp -= 4 + i * 12;
		}
		mPbrLightsBuffer->mapBufferData();
	}

	void RenderSystem::destroyShadowDomains()
	{
		mActiveShadowDepthTarget.reset();
		mShadowDisabledFrameBuffer.reset();
		mShadowDomains.clear();
	}

	void RenderSystem::createShadowDisabledFrameBuffer()
	{
		if (mShadowDisabledFrameBuffer)
		{
			return;
		}
		ShadowFrameData frame;
		shared_ptr<const int8_t> frameBytes(new int8_t[sizeof(frame)](), [](int8_t* p) { delete[] p; });
		memcpy(const_cast<int8_t*>(frameBytes.get()), &frame, sizeof(frame));
		mShadowDisabledFrameBuffer = make_shared<UniformBuffer>(this, frameBytes, sizeof(frame), 2);
		mShadowDisabledFrameBuffer->load();
	}

	void RenderSystem::createShadowDomainResources(string const& name, ShadowDomainState& domain)
	{
		if (!domain.options.enabled || domain.depthTarget)
		{
			return;
		}
		if (!mResourceMgr)
		{
			THROW_MPP("Cannot create a shadow domain before core resources are available.", __LINE__, __FILE__, __func__);
		}

		bool const point = domain.options.light.type == ShadowLightType::Point;
		if (point && (mCaps.maxCubeMapTextureSize <= 0 || domain.options.resolution > (size_t)mCaps.maxCubeMapTextureSize))
		{
			domain.options.enabled = false;
			if (!domain.fallbackWarningIssued)
			{
				warnMessage("Shadow domain '" + name + "' disabled: requested Depth24 cubemap is unsupported; direct lighting remains enabled.");
				domain.fallbackWarningIssued = true;
			}
			return;
		}

		RenderTextureOptions targetOptions;
		targetOptions.target = point ? TextureTarget::CubeMap : TextureTarget::Texture2D;
		targetOptions.numAttachments = 0;
		targetOptions.depthAttachment = RenderTextureDepthAttachment::DepthTexture;
		targetOptions.depthParams.params.minFilter = GL_LINEAR;
		targetOptions.depthParams.params.magFilter = GL_LINEAR;
		targetOptions.depthParams.params.wrap = domain.options.light.type == ShadowLightType::Point ? GL_CLAMP_TO_EDGE : GL_CLAMP_TO_BORDER;
		targetOptions.depthParams.compareRefToTexture = true;
		try
		{
			domain.depthTarget = createRenderTexture(
				"ShadowDomain." + name + (domain.options.light.type == ShadowLightType::Point ? ".PointDepthCube" : ".Depth"),
				domain.options.resolution,
				domain.options.resolution,
				targetOptions);
		}
		catch (...)
		{
			if (domain.options.light.type != ShadowLightType::Point) throw;
			domain.options.enabled = false;
			domain.depthTarget.reset();
			domain.frameBuffer.reset();
			if (!domain.fallbackWarningIssued)
			{
				warnMessage("Shadow domain '" + name + "' disabled: depth cubemap allocation/capability is unavailable; direct lighting remains enabled.");
				domain.fallbackWarningIssued = true;
			}
			return;
		}

		// std140: mat4 (64 bytes), then two vec4 values. Keep this independent
		// of PBR lighting so legacy/custom shaders can use the same frame data.
		ShadowFrameData frame;
		const float texelSize = 1.0f / (float)domain.options.resolution;
		frame.mapTexelSizeAndRadius = glm::vec4(texelSize, texelSize, domain.options.filterRadiusTexels,
			domain.options.filterMode == ShadowFilterMode::Pcf3x3 ? 1.0f : 0.0f);
		frame.biasAndEnabled = glm::vec4(domain.options.constantBias, domain.options.normalBias, 1.0f, domain.options.fadeStartNormalized);
		frame.pointPositionAndRange = glm::vec4(domain.options.light.position, domain.options.light.range);
		frame.shadowTypeAndLightIndex = glm::vec4(domain.options.light.type == ShadowLightType::Point ? 1.0f : 0.0f,
			(float)domain.options.light.lightIndex, 0.0f, 0.0f);

		shared_ptr<const int8_t> frameBytes(new int8_t[sizeof(frame)](), [](int8_t* p) { delete[] p; });
		memcpy(const_cast<int8_t*>(frameBytes.get()), &frame, sizeof(frame));
		domain.frameBuffer = make_shared<UniformBuffer>(this, frameBytes, sizeof(frame), 2);
		domain.frameBuffer->load();
	}

	namespace
	{
		bool shadowLightEqual(ShadowLight const& a, ShadowLight const& b)
		{
			return a.type == b.type && a.direction == b.direction && a.focusPoint == b.focusPoint &&
				a.position == b.position && a.range == b.range && a.lightIndex == b.lightIndex;
		}

		bool pointShadowCasterClipEqual(PointShadowCasterClip const& a,
			PointShadowCasterClip const& b)
		{
			return a.enabled == b.enabled &&
				a.retainedWorldPlane == b.retainedWorldPlane &&
				a.openingCentre == b.openingCentre &&
				a.openingTangent == b.openingTangent &&
				a.openingBitangent == b.openingBitangent &&
				a.openingHalfSize == b.openingHalfSize &&
				a.planeTolerance == b.planeTolerance;
		}

		bool shadowOptionsEqual(ShadowOptions const& a, ShadowOptions const& b)
		{
			return a.enabled == b.enabled && shadowLightEqual(a.light, b.light) && a.resolution == b.resolution &&
				a.orthoHalfWidth == b.orthoHalfWidth && a.nearPlane == b.nearPlane && a.farPlane == b.farPlane &&
				a.constantBias == b.constantBias && a.normalBias == b.normalBias &&
				a.filterRadiusTexels == b.filterRadiusTexels && a.filterMode == b.filterMode &&
				a.fadeStartNormalized == b.fadeStartNormalized &&
				pointShadowCasterClipEqual(a.pointCasterClip, b.pointCasterClip);
		}

		uint64_t shadowPointer(void const* value)
		{
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
		}
	}

	void RenderSystem::markShadowDomainDirty(ShadowDomainState& domain, ShadowInvalidationReason reason)
	{
		domain.cacheDirty = true;
		domain.regenerationStarted = false;
		domain.renderedFaces = 0;
		domain.pendingReason = reason;
		++domain.diagnostics.revision;
		domain.diagnostics.cacheComplete = false;
		domain.diagnostics.reusedLastRequest = false;
	}

	vector<uint64_t> RenderSystem::captureShadowCasterState(vector<SceneModel3dPtr> const& models) const
	{
		vector<uint64_t> state;
		state.reserve(models.size() * 8);
		for (auto const& sceneModel : models)
		{
			if (!sceneModel) continue;
			auto modelResource = sceneModel->getModel();
			auto const* model = dynamic_cast<Model const*>(modelResource.get());
			auto const params = sceneModel->getParams();
			state.push_back(shadowPointer(sceneModel.get()));
			state.push_back(sceneModel->getShadowRevision());
			state.push_back(shadowPointer(model));
			state.push_back(modelResource ? modelResource->getLifecycleRevision() : 0);
			state.push_back(model ? model->getMaterialRevision() : 0);
			state.push_back(params ? params->getShadowRevision() : 0);
			if (!model || !params) continue;
			auto const& meshParams = params->getMeshParams();
			auto const defaultParams = meshParams.find("");
			for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
			{
				auto const* mesh = model->getMesh(meshIndex);
				auto const meshOverride = meshParams.find(mesh->getName());
				auto const* renderParams = meshOverride != meshParams.end() ? &meshOverride->second :
					(defaultParams != meshParams.end() ? &defaultParams->second : nullptr);
				auto materialResource = renderParams && renderParams->material ? renderParams->material : mesh->getMaterial();
				auto const* material = dynamic_cast<Material const*>(materialResource.get());
				state.push_back(shadowPointer(material));
				state.push_back(materialResource ? materialResource->getLifecycleRevision() : 0);
				state.push_back(material && material->isTransparent() ? 1 : 0);
			}
		}
		return state;
	}

	void RenderSystem::configureShadowDomain(string const& name, ShadowOptions const& options)
	{
		if (name.empty())
		{
			THROW_MPP("Shadow domain name cannot be empty.", __LINE__, __FILE__, __func__);
		}
		if (options.enabled)
		{
			auto const& clip = options.pointCasterClip;
			auto finite3 = [](glm::vec3 const& value)
			{
				return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
			};
			bool const invalidClip = clip.enabled &&
				(options.light.type != ShadowLightType::Point ||
				 !finite3(glm::vec3(clip.retainedWorldPlane)) ||
				 !isfinite(clip.retainedWorldPlane.w) ||
				 glm::dot(glm::vec3(clip.retainedWorldPlane), glm::vec3(clip.retainedWorldPlane)) < 0.000001f ||
				 !finite3(clip.openingCentre) || !finite3(clip.openingTangent) ||
				 !finite3(clip.openingBitangent) ||
				 glm::dot(clip.openingTangent, clip.openingTangent) < 0.000001f ||
				 glm::dot(clip.openingBitangent, clip.openingBitangent) < 0.000001f ||
				 !isfinite(clip.openingHalfSize.x) || !isfinite(clip.openingHalfSize.y) ||
				 clip.openingHalfSize.x <= 0.0f || clip.openingHalfSize.y <= 0.0f ||
				 !isfinite(clip.planeTolerance) || clip.planeTolerance < 0.0f);
			if ((options.light.type != ShadowLightType::Directional && options.light.type != ShadowLightType::Point) ||
				options.resolution == 0 || options.orthoHalfWidth <= 0.0f || options.nearPlane < 0.0f ||
				options.farPlane <= options.nearPlane || options.constantBias < 0.0f || options.normalBias < 0.0f ||
				options.filterRadiusTexels < 0.0f || !isfinite(options.orthoHalfWidth) ||
				!isfinite(options.nearPlane) || !isfinite(options.farPlane) ||
				!isfinite(options.constantBias) || !isfinite(options.normalBias) || !isfinite(options.filterRadiusTexels) ||
				!isfinite(options.fadeStartNormalized) || options.fadeStartNormalized < 0.0f || options.fadeStartNormalized > 1.0f ||
				(options.filterMode != ShadowFilterMode::Hard && options.filterMode != ShadowFilterMode::Pcf3x3) || invalidClip ||
				(options.light.type == ShadowLightType::Directional &&
				 (!isfinite(options.light.direction.x) || !isfinite(options.light.direction.y) || !isfinite(options.light.direction.z) ||
				  glm::dot(options.light.direction, options.light.direction) < 0.000001f)) ||
				(options.light.type == ShadowLightType::Point &&
				 (!isfinite(options.light.position.x) || !isfinite(options.light.position.y) || !isfinite(options.light.position.z) ||
				  !isfinite(options.light.range) || options.nearPlane <= 0.0f || options.light.range <= options.nearPlane)))
			{
				THROW_MPP("Invalid shadow domain options.", __LINE__, __FILE__, __func__);
			}
		}

		auto existing = mShadowDomains.find(name);
		bool const newlyConfigured = existing == mShadowDomains.end();
		auto& domain = mShadowDomains[name];
		bool const lightChanged = !newlyConfigured && !shadowLightEqual(domain.options.light, options.light);
		bool const optionsChanged = newlyConfigured || !shadowOptionsEqual(domain.options, options);
		// Direction, projection bounds, bias, and filter values are uploaded on
		// the next shadow pass. Only a resolution or enabled-state change needs
		// to discard GL resources, which keeps interactive light movement cheap.
		const bool recreateResources = domain.depthTarget &&
			(!options.enabled || domain.options.resolution != options.resolution || domain.options.light.type != options.light.type);
		if (recreateResources)
		{
			domain.depthTarget.reset();
			domain.frameBuffer.reset();
		}
		domain.options = options;
		if (optionsChanged)
			markShadowDomainDirty(domain, newlyConfigured ? ShadowInvalidationReason::InitialConfiguration :
				(lightChanged ? ShadowInvalidationReason::LightChanged : ShadowInvalidationReason::OptionsChanged));
	}

	bool RenderSystem::hasShadowDomain(string const& name) const
	{
		return mShadowDomains.find(name) != mShadowDomains.end();
	}

	ShadowOptions const& RenderSystem::getShadowDomainOptions(string const& name) const
	{
		auto it = mShadowDomains.find(name);
		if (it == mShadowDomains.end())
		{
			THROW_MPP("Unknown shadow domain.", __LINE__, __FILE__, __func__);
		}
		return it->second.options;
	}

	RenderTargetPtr RenderSystem::getShadowDomainDepthTarget(string const& name)
	{
		ensureShadowDomainResources(name);
		return mShadowDomains.at(name).depthTarget;
	}

	void RenderSystem::ensureShadowDomainResources(string const& name)
	{
		auto it = mShadowDomains.find(name);
		if (it == mShadowDomains.end())
		{
			THROW_MPP("Pipeline references an unknown shadow domain.", __LINE__, __FILE__, __func__);
		}
		createShadowDomainResources(name, it->second);
	}

	bool RenderSystem::prepareShadowDomain(string const& name, vector<SceneModel3dPtr> const& models)
	{
		ensureShadowDomainResources(name);
		auto& domain = mShadowDomains.at(name);
		if (!domain.options.enabled || !domain.depthTarget) return false;
		if (domain.options.light.type != ShadowLightType::Point) return true;

		auto state = captureShadowCasterState(models);
		domain.diagnostics.selectedModelCount = models.size();
		if (domain.casterState != state)
		{
			domain.casterState = std::move(state);
			if (!domain.cacheDirty) markShadowDomainDirty(domain, ShadowInvalidationReason::SceneChanged);
		}
		if (!domain.cacheDirty && domain.diagnostics.cacheComplete)
		{
			++domain.diagnostics.reuseCount;
			domain.diagnostics.reusedLastRequest = true;
			return false;
		}
		if (!domain.regenerationStarted)
		{
			domain.regenerationStarted = true;
			++domain.diagnostics.regenerationCount;
			domain.diagnostics.reusedLastRequest = false;
		}
		return true;
	}

	void RenderSystem::invalidateShadowDomain(string const& name)
	{
		auto found = mShadowDomains.find(name);
		if (found == mShadowDomains.end()) THROW_MPP("Unknown shadow domain.", __LINE__, __FILE__, __func__);
		markShadowDomainDirty(found->second, ShadowInvalidationReason::Explicit);
	}

	ShadowDomainDiagnostics RenderSystem::getShadowDomainDiagnostics(string const& name) const
	{
		auto found = mShadowDomains.find(name);
		if (found == mShadowDomains.end()) THROW_MPP("Unknown shadow domain.", __LINE__, __FILE__, __func__);
		return found->second.diagnostics;
	}

	void RenderSystem::renderShadowDomain(string const& name, vector<SceneModel3dPtr> const& models, uint32_t face)
	{
		ensureShadowDomainResources(name);
		auto& domain = mShadowDomains.at(name);
		if (!domain.options.enabled || !domain.depthTarget)
		{
			return;
		}
		bool const point = domain.options.light.type == ShadowLightType::Point;
		if (point && !prepareShadowDomain(name, models)) return;
		if (point && face == UINT32_MAX)
		{
			for (uint32_t cubeFace = 0; cubeFace < 6; ++cubeFace) renderShadowDomain(name, models, cubeFace);
			return;
		}
		if (point && face >= 6)
		{
			THROW_MPP("Point shadow cubemap face must be in [0, 5].", __LINE__, __FILE__, __func__);
		}

		static constexpr char const* faceNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
		GpuDebugScope faceScope(point ? "Pass: PointShadow [" + name + "] Face " + faceNames[face] : "Pass: DirectionalShadow [" + name + "]");
		auto depthTarget = domain.depthTarget;
		auto shadowProgramResource = point ? mPointShadowDepthProgram : mShadowDepthProgram;
		glm::mat4 lightView;
		glm::mat4 lightProjection;
		if (point)
		{
			static glm::vec3 const directions[] = { { 1,0,0 }, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
			static glm::vec3 const ups[] = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
			lightView = glm::lookAt(domain.options.light.position, domain.options.light.position + directions[face], ups[face]);
			lightProjection = glm::perspective(glm::radians(90.0f), 1.0f, domain.options.nearPlane, domain.options.light.range);
		}
		else
		{
			glm::vec3 direction = glm::normalize(domain.options.light.direction);
			glm::vec3 up = abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			float lightDistance = (domain.options.farPlane - domain.options.nearPlane) * 0.5f;
			lightView = glm::lookAt(domain.options.light.focusPoint - direction * lightDistance, domain.options.light.focusPoint, up);
			float extent = domain.options.orthoHalfWidth;
			lightProjection = glm::ortho(-extent, extent, -extent, extent, domain.options.nearPlane, domain.options.farPlane);
		}
		glm::mat4 lightViewProjection = lightProjection * lightView;

		ShadowFrameData frame;
		frame.lightViewProjection = lightViewProjection;
		float texelSize = 1.0f / (float)domain.options.resolution;
		frame.mapTexelSizeAndRadius = glm::vec4(texelSize, texelSize, domain.options.filterRadiusTexels,
			domain.options.filterMode == ShadowFilterMode::Pcf3x3 ? 1.0f : 0.0f);
		frame.biasAndEnabled = glm::vec4(domain.options.constantBias, domain.options.normalBias, 1.0f, domain.options.fadeStartNormalized);
		frame.pointPositionAndRange = glm::vec4(domain.options.light.position, domain.options.light.range);
		frame.shadowTypeAndLightIndex = glm::vec4(point ? 1.0f : 0.0f, (float)domain.options.light.lightIndex, 0.0f, 0.0f);
		auto& frameBytes = domain.frameBuffer->getBufferData();
		memcpy(frameBytes.data(), &frame, sizeof(frame));
		domain.frameBuffer->mapBufferData();

		GLint previousViewport[4]{ mViewportX, mViewportY, static_cast<GLint>(mViewportWidth), static_cast<GLint>(mViewportHeight) };
		auto const previousRasterState = captureRasterState(1);
		bool const previousPolygonOffsetEnabled = mPolygonOffsetFill;
		float const previousPolygonOffsetFactor = mPolygonOffsetFactor;
		float const previousPolygonOffsetUnits = mPolygonOffsetUnits;

		pushRenderTarget(depthTarget);
		if (point) static_cast<RenderTexture*>(depthTarget.get())->attachDepthFace(face, 0);
		GL_CHECK(glViewport(0, 0, (GLsizei)depthTarget->getWidth(), (GLsizei)depthTarget->getHeight()));
		setDepthTestState(true);
		setDepthWriteState(true);
		setBlendState(false);
		setCullState(point ? GraphCullMode::None : GraphCullMode::Front);
		setPolygonOffsetFillState(true);
		setPolygonOffsetState(2.0f, 4.0f);
		GL_CHECK(glClearDepth(1.0));
		GL_CHECK(glClear(GL_DEPTH_BUFFER_BIT));

		if (isRenderFlowCaptureActive())
			recordRenderFlowStateChanges({ point ? "Render target: shared point depth cubemap face " + string(faceNames[face]) : "Render target: shadow depth",
			                              "Depth test: enabled", "Depth write: enabled", "Blend: disabled",
			                              point ? "Cull face: disabled (two-sided caster)" : "Cull face: front",
			                              "Polygon offset: enabled" });
		auto bindPointCasterClip = [&](Program* program)
		{
			if (!point) return;
			auto const& clip = domain.options.pointCasterClip;
			GL_CHECK(glUniform1i(program->getUniformId("POINT_SHADOW_CASTER_CLIP_ENABLED"), clip.enabled ? 1 : 0));
			if (!clip.enabled) return;
			float const planeScale = glm::length(glm::vec3(clip.retainedWorldPlane));
			auto plane = glm::vec4(
				glm::vec3(clip.retainedWorldPlane) / planeScale,
				clip.retainedWorldPlane.w / planeScale);
			GL_CHECK(glUniform4fv(program->getUniformId("POINT_SHADOW_RETAINED_PLANE"), 1, glm::value_ptr(plane)));
			auto centreTolerance = glm::vec4(clip.openingCentre, clip.planeTolerance);
			GL_CHECK(glUniform4fv(program->getUniformId("POINT_SHADOW_OPENING_CENTRE_TOLERANCE"), 1, glm::value_ptr(centreTolerance)));
			auto tangentWidth = glm::vec4(glm::normalize(clip.openingTangent), clip.openingHalfSize.x);
			GL_CHECK(glUniform4fv(program->getUniformId("POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH"), 1, glm::value_ptr(tangentWidth)));
			auto bitangentHeight = glm::vec4(glm::normalize(clip.openingBitangent), clip.openingHalfSize.y);
			GL_CHECK(glUniform4fv(program->getUniformId("POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT"), 1, glm::value_ptr(bitangentHeight)));
		};
		for (auto const& sceneModel : models)
		{
			auto params = sceneModel->getParams();
			auto const& meshParams = params->getMeshParams();
			auto defaultParams = meshParams.find("");
			auto model = static_cast<Model*>(sceneModel->getModel().get());
			glm::mat4 modelLightProjection = lightViewProjection * sceneModel->getTransform();

			for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
			{
				auto mesh = model->getMesh(meshIndex);
				auto meshParamsIt = meshParams.find(mesh->getName());
				auto const* renderParams = meshParamsIt != meshParams.end() ? &meshParamsIt->second :
					(defaultParams != meshParams.end() ? &defaultParams->second : nullptr);
				if (renderParams && ((renderParams->flags & ModelRenderParams::Flag_Visible) == 0 ||
					(renderParams->flags & ModelRenderParams::Flag_CastShadows) == 0))
				{
					continue;
				}

				auto materialResource = renderParams && renderParams->material ? renderParams->material : mesh->getMaterial();
				auto material = static_cast<Material*>(materialResource.get());
				auto const shadowContract = material->getShadowCasterContract();
				if (!castsShadow(shadowContract)) continue;
				bool const alphaMasked = shadowContract.behaviour == ShadowCasterContract::Behaviour::AlphaMask;
				auto const activeShadowProgramResource = alphaMasked
					? (point ? mPointAlphaShadowDepthProgram : mAlphaShadowDepthProgram)
					: shadowProgramResource;
				auto shadowProgram = static_cast<Program*>(activeShadowProgramResource.get());
				setUsedProgram(activeShadowProgramResource);
				bindPointCasterClip(shadowProgram);
				if (isRenderFlowCaptureActive())
					recordRenderFlowStateChanges({ "Program: " + shadowProgram->getName() });
				if (alphaMasked)
				{
					auto materialProgram = static_cast<Program*>(material->getProgram().get());
					ResourcePtr alphaTexture = mNoTexture;
					for (int sampler = 0; sampler < materialProgram->getNumSamplers(); ++sampler)
						if (materialProgram->getSamplerName(sampler) == shadowContract.alphaSampler)
						{
							alphaTexture = material->getTexture(sampler);
							break;
						}
					auto* texture = dynamic_cast<Texture*>(alphaTexture.get());
					if (!texture) THROW_MPP("Shadow alpha contract sampler is not a texture.", __LINE__, __FILE__, __func__);
					texture->bind((uint32_t)shadowProgram->getSamplerUnit("SHADOW_ALPHA_MAP"));
					GL_CHECK(glUniform1f(shadowProgram->getUniformId("SHADOW_ALPHA_CUTOFF"), shadowContract.alphaCutoff));
					GL_CHECK(glUniform1f(shadowProgram->getUniformId("SHADOW_ALPHA_FACTOR"), shadowContract.alphaFactor));
				}

				GL_CHECK(glUniformMatrix4fv(shadowProgram->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(modelLightProjection)));
				if (point) GL_CHECK(glUniformMatrix4fv(shadowProgram->getModelMatrixId(), 1, GL_FALSE, glm::value_ptr(sceneModel->getTransform())));
				mesh->bind(true);
				size_t instanceCount = renderParams ? renderParams->instanceCount : 1;
				if (renderParams && !renderParams->renderCommands.empty())
				{
					for (auto const& command : renderParams->renderCommands)
					{
						if(isRenderFlowCaptureActive())try{RenderBatchSubmission submission;submission.sceneObject=sceneModel.get();submission.meshName=mesh->getName();submission.materialName=material->getName();submission.programName=shadowProgram->getName();submission.primitiveType=mesh->mPrimitiveType;submission.offset=command.offset;submission.count=command.count!=~0u?command.count:static_cast<uint32_t>(mesh->getNumPrimitives());submission.instanceCount=instanceCount;recordRenderFlowBatch(std::move(submission));}catch(...){mFlowCaptureFailed=true;}
						mesh->render(instanceCount, command.offset, command.count);
					}
				}
				else
				{
					if(isRenderFlowCaptureActive())try{RenderBatchSubmission submission;submission.sceneObject=sceneModel.get();submission.meshName=mesh->getName();submission.materialName=material->getName();submission.programName=shadowProgram->getName();submission.primitiveType=mesh->mPrimitiveType;submission.count=static_cast<uint32_t>(mesh->getNumPrimitives());submission.instanceCount=instanceCount;recordRenderFlowBatch(std::move(submission));}catch(...){mFlowCaptureFailed=true;}
					mesh->render(instanceCount);
				}
				mesh->bind(false);
			}
		}

		popRenderTarget();
		if (point)
		{
			domain.renderedFaces |= static_cast<uint8_t>(1u << face);
			++domain.diagnostics.facePassCount;
			if (domain.renderedFaces == 0x3f)
			{
				domain.cacheDirty = false;
				domain.regenerationStarted = false;
				domain.diagnostics.renderedRevision = domain.diagnostics.revision;
				domain.diagnostics.renderedFrame = mFrameSerial;
				domain.diagnostics.cacheComplete = true;
				domain.diagnostics.reusedLastRequest = false;
				domain.diagnostics.invalidationReason = domain.pendingReason;
			}
		}
		GL_CHECK(glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]));
		applyRasterState(previousRasterState, 1, depthTarget->getWidth(), depthTarget->getHeight());
		setPolygonOffsetFillState(previousPolygonOffsetEnabled);
		setPolygonOffsetState(previousPolygonOffsetFactor, previousPolygonOffsetUnits);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::setActiveShadowDomain(string const& name)
	{
		mActiveShadowDepthTarget.reset();
		if (name.empty())
		{
			if (mShadowDisabledFrameBuffer)
			{
				mShadowDisabledFrameBuffer->activate();
			}
			return;
		}

		ensureShadowDomainResources(name);
		auto& domain = mShadowDomains.at(name);
		if (domain.options.enabled)
		{
			domain.frameBuffer->activate();
			mActiveShadowDepthTarget = domain.depthTarget;
		}
		else if (mShadowDisabledFrameBuffer)
		{
			mShadowDisabledFrameBuffer->activate();
		}
	}

	void RenderSystem::renderDepthPrepass(
		vector<SceneModel3dPtr> const& models, CameraPtr camera,
		size_t colourOutputCount)
	{
		if (!camera || models.empty())
		{
			setDepthCompareState(GraphCompareOp::LessEqual);
			return;
		}

		GpuDebugScope prepassScope("Depth Prepass");
		auto savedState = captureRasterState(colourOutputCount);
		auto prepassState = savedState;
		prepassState.depthTest = true;
		prepassState.depthWrite = true;
		prepassState.depthCompare = GraphCompareOp::Less;
		prepassState.blend = false;
		prepassState.colourWriteMasks.assign(
			max<size_t>(1, colourOutputCount),
			GraphColourWriteMask{ false, false, false, false });
		auto const expectedOutputs = mExpectedGraphColourOutputs;
		applyRasterState(prepassState, colourOutputCount, mViewportWidth, mViewportHeight);
		setExpectedGraphColourOutputs(0);

		auto restore = [&](bool materialPass)
		{
			setExpectedGraphColourOutputs(expectedOutputs);
			if (materialPass) savedState.depthCompare = GraphCompareOp::LessEqual;
			applyRasterState(savedState, colourOutputCount, mViewportWidth, mViewportHeight);
		};

		try
		{
			if (isRenderFlowCaptureActive())
				recordRenderFlowStateChanges({ "Depth prepass: enabled", "Colour writes: disabled",
					"Depth test: less", "Depth write: enabled" });

			auto const viewProjection =
				camera->getProjectionTransform() * camera->getViewTransform();
			for (auto const& sceneModel : models)
			{
				if (!sceneModel) continue;
				auto params = sceneModel->getParams();
				auto const& meshParams = params->getMeshParams();
				auto defaultParams = meshParams.find("");
				auto model = static_cast<Model*>(sceneModel->getModel().get());
				auto modelViewProjection = viewProjection * sceneModel->getTransform();

				for (int meshIndex = 0; meshIndex < model->getNumMeshes(); ++meshIndex)
				{
					auto mesh = model->getMesh(meshIndex);
					auto meshParamsIt = meshParams.find(mesh->getName());
					auto const* renderParams = meshParamsIt != meshParams.end()
						? &meshParamsIt->second
						: (defaultParams != meshParams.end() ? &defaultParams->second : nullptr);
					if (renderParams && (renderParams->flags & ModelRenderParams::Flag_Visible) == 0)
						continue;

					auto baseMaterial = renderParams && renderParams->material
						? renderParams->material : mesh->getMaterial();
					size_t const instanceCount = renderParams ? renderParams->instanceCount : 1;
					bool const wireframe = renderParams &&
						(renderParams->flags & ModelRenderParams::Flag_Wireframe) != 0;
					bool const requestedCull = renderParams &&
						(renderParams->flags & ModelRenderParams::Flag_CullBackFaces) != 0;

					auto draw = [&](VertexBufferRenderCommand const* command)
					{
						auto materialResource = command && command->material
							? command->material : baseMaterial;
						auto material = static_cast<Material*>(materialResource.get());
						bool const pbr = material->getShadingModel() == Material::ShadingModel::Pbr;
						// PBR derives blend semantics from the material. Legacy scene
						// submission defaults to blending unless params explicitly turn it
						// off, so mirror the proper pass rather than letting translucent
						// geometry become an occluder here.
						bool const blended = pbr ? material->isTransparent()
							: (!renderParams || renderParams->blend.value_or(true));
						bool const participates = renderParams && renderParams->depthPrepass
							? *renderParams->depthPrepass : !blended;
						if (!participates) return;

						auto const contract = material->getShadowCasterContract();
						bool const alphaMasked =
							contract.behaviour == ShadowCasterContract::Behaviour::AlphaMask;
						auto programResource = alphaMasked
							? mAlphaShadowDepthProgram : mShadowDepthProgram;
						auto program = static_cast<Program*>(programResource.get());
						setUsedProgram(programResource);

						bool const cull = requestedCull && !(pbr && material->isDoubleSided());
						setCullState(cull ? GraphCullMode::Back : GraphCullMode::None);
						setFillModeState(wireframe ? GraphFillMode::Line : GraphFillMode::Fill);

						if (alphaMasked)
						{
							auto materialProgram = static_cast<Program*>(material->getProgram().get());
							ResourcePtr alphaTexture = mNoTexture;
							for (int sampler = 0; sampler < materialProgram->getNumSamplers(); ++sampler)
							{
								if (materialProgram->getSamplerName(sampler) != contract.alphaSampler) continue;
								if (command && sampler < (int)command->textures.size() && command->textures[sampler])
									alphaTexture = command->textures[sampler];
								else if (renderParams && sampler < (int)renderParams->textures.size() && renderParams->textures[sampler])
									alphaTexture = renderParams->textures[sampler];
								else alphaTexture = material->getTexture(sampler);
								break;
							}
							auto texture = dynamic_cast<Texture*>(alphaTexture.get());
							if (!texture) THROW_MPP("Depth-prepass alpha contract sampler is not a texture.", __LINE__, __FILE__, __func__);
							texture->bind((uint32_t)program->getSamplerUnit("SHADOW_ALPHA_MAP"));
							GL_CHECK(glUniform1f(program->getUniformId("SHADOW_ALPHA_CUTOFF"), contract.alphaCutoff));
							GL_CHECK(glUniform1f(program->getUniformId("SHADOW_ALPHA_FACTOR"), contract.alphaFactor));
						}

						GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(),
							1, GL_FALSE, glm::value_ptr(modelViewProjection)));
						mesh->bind(true);
						if (command) mesh->render(instanceCount, command->offset, command->count);
						else mesh->render(instanceCount);
						mesh->bind(false);
					};

					if (renderParams && !renderParams->renderCommands.empty())
						for (auto const& command : renderParams->renderCommands) draw(&command);
					else draw(nullptr);
				}
			}
		}
		catch (...)
		{
			restore(false);
			throw;
		}

		restore(true);
		if (isRenderFlowCaptureActive())
			recordRenderFlowStateChanges({ "Depth prepass complete", "Depth test: less-equal", "Colour writes: enabled" });
	}

	void RenderSystem::setActivePbrEnvironment(PbrEnvironmentPtr environment)
	{
		mActivePbrEnvironment = std::move(environment);
		if (!mActivePbrEnvironment)
		{
			return;
		}

		for (auto const& resource : {
			mActivePbrEnvironment->irradianceMap,
			mActivePbrEnvironment->prefilteredSpecularMap,
			mActivePbrEnvironment->brdfIntegrationLut,
			mActivePbrEnvironment->backgroundMap,
			mActivePbrEnvironment->environmentMap })
		{
			if (resource)
			{
				resource->load();
			}
		}
	}

	void RenderSystem::setActivePipelineSamplerOverrides(map<string, ResourcePtr> const& overrides)
	{
		mActivePipelineSamplerOverrides = overrides;
		for (auto const& [sampler, resource] : mActivePipelineSamplerOverrides)
		{
			if (resource)
			{
				resource->load();
			}
		}
	}

	float RenderSystem::getElapsedSeconds() const
	{
		static auto const epoch = chrono::steady_clock::now();
		return chrono::duration<float>(chrono::steady_clock::now() - epoch).count();
	}

	map<string, ResourcePtr> const& RenderSystem::getActivePipelineSamplerOverrides() const
	{
		return mActivePipelineSamplerOverrides;
	}

	ParticleSystem& RenderSystem::getParticleSystem()
	{
		if (!mParticleSystem) THROW_MPP("ParticleSystem is unavailable before core resources are created.", __LINE__, __FILE__, __func__);
		return *mParticleSystem;
	}

	ParticleSystem const& RenderSystem::getParticleSystem() const
	{
		if (!mParticleSystem) THROW_MPP("ParticleSystem is unavailable before core resources are created.", __LINE__, __FILE__, __func__);
		return *mParticleSystem;
	}

	TrailSystem& RenderSystem::getTrailSystem()
	{
		if (!mTrailSystem) THROW_MPP("TrailSystem is unavailable before core resources are created.", __LINE__, __FILE__, __func__);
		return *mTrailSystem;
	}

	TrailSystem const& RenderSystem::getTrailSystem() const
	{
		if (!mTrailSystem) THROW_MPP("TrailSystem is unavailable before core resources are created.", __LINE__, __FILE__, __func__);
		return *mTrailSystem;
	}

	/*
	 * Advance particles for this frame.
	 *
	 */
	void RenderSystem::simulateParticles()
	{
		if (!mParticleSystem) return;
		// A graph can retry after a recoverable pass failure, and an application can
		// render several pipelines/views between frame boundaries. Neither is a new
		// particle frame: startStatsCollection() is the renderer's frame boundary.
		if (mParticleSimulationFrameValid && mParticleSimulationFrameSerial == mFrameSerial) return;
		mParticleSimulationFrameSerial = mFrameSerial;
		mParticleSimulationFrameValid = true;
		mParticleSystem->simulate();
		// Emitter transforms and enabled state may have changed since the scene
		// supplied its base lights. Refresh the bounded proxy-light tail before the
		// opaque PBR pass consumes the UBO.
		uploadPbrLights();

		// The raw compute program was bound outside the Program cache, which would
		// otherwise skip rebinding whatever material program was last used.
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::simulateTrails()
	{
		if (!mTrailSystem) return;
		if (mTrailSimulationFrameValid && mTrailSimulationFrameSerial == mFrameSerial) return;
		mTrailSimulationFrameSerial = mFrameSerial;
		mTrailSimulationFrameValid = true;
		mTrailSystem->simulate();
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	/*
	 * Draw the live particles into the current render target.
	 *
	 */
	void RenderSystem::renderParticles(ParticleBlendClass blendClass, RenderTexture* sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->render(blendClass, sceneDepth);

		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderParticles(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->render(blendClass, sceneDepth);

		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderParticleDistortion(RenderTexture* sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->renderDistortion(sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderParticleDistortion(ResourcePtr const& sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->renderDistortion(sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderParticleVolumetricLighting(RenderTexture* sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->renderVolumetricLighting(sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderParticleVolumetricLighting(ResourcePtr const& sceneDepth)
	{
		if (!mParticleSystem) return;
		mParticleSystem->renderVolumetricLighting(sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderMeshParticles()
	{
		if (!mParticleSystem) return;
		mParticleSystem->renderMeshes();
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderTrails(ParticleBlendClass blendClass, RenderTexture* sceneDepth)
	{
		if (!mTrailSystem) return;
		mTrailSystem->render(blendClass, sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	void RenderSystem::renderTrails(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth)
	{
		if (!mTrailSystem) return;
		mTrailSystem->render(blendClass, sceneDepth);
		GL_CHECK(glUseProgram(0));
		mActiveProgram.reset();
	}

	bool RenderSystem::particlesAvailable()
	{
		if (!mParticleSystem) return false;
		mParticleSystem->initialise();
		return mParticleSystem->isAvailable();
	}

	void RenderSystem::setActivePipelineUniformOverrides(UniformCollection const& overrides)
	{
		mActivePipelineUniformOverrides = overrides;
	}

	UniformCollection const& RenderSystem::getActivePipelineUniformOverrides() const
	{
		return mActivePipelineUniformOverrides;
	}

	/*
	 * Render a texture as a fullscreen quad
	 *
	 */
	void RenderSystem::renderFullscreenQuad(Texture* texture, BlendMode srcBlend, BlendMode dstBlend, shared_ptr<UniformCollection> uniforms)
	{
		flushVertexBuffers();

		// The shared fullscreen program writes colour attachment zero only, and
		// graph passes such as MPP.WaterScene seed their primary output with it
		// while a second, loaded attachment is still bound.
		PrimaryColourOutputDraw primaryOutput(this);

		// Set program
		auto p = static_cast<Program*>(mFullscreenProgram.get());

		setUsedProgram(mFullscreenProgram);
		mRenderInfo.programSwitches++;

		// Set uniforms
		if (uniforms)
		{
			uniforms->bindUniforms(mFullscreenProgram);
		}
		else
		{
			// Set defaults manually
			GL_CHECK(glUniform4f(p->getUniformId("DIFFUSE"), 1, 1, 1, 1));
		}

		GL_CHECK(glUniformMatrix4fv(p->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(p->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));

		int gammaId = p->getUniformId("GAMMA");

		if (gammaId >= 0)
		{
			GL_CHECK(glUniform1f(gammaId, mGamma));
		}

		// Set texture
		texture->bind(0, 0);
		mRenderInfo.textureSwitches++;

		// Set blend
		setBlendState(true);
		auto const sourceFactor = graphBlendFactor(srcBlend);
		auto const destinationFactor = graphBlendFactor(dstBlend);
		setBlendFunctionState(sourceFactor, destinationFactor, sourceFactor, destinationFactor);

		// Bind mesh
		auto quadMesh = ((Model*)mFullscreenQuad.get())->getMesh(0);
		quadMesh->bind(true);
		quadMesh->render(1);

		// Unbind
		quadMesh->bind(false);

		// Disable blend
		setBlendState(false);

		mRenderInfo.batchCount++;
		mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderGraphFullscreen(ResourcePtr program, vector<pair<string, Texture*>> const& samplers, UniformCollection const& parameters)
	{
		flushVertexBuffers();
		if (!program || program->getType() != "Program")
		{
			THROW_MPP("Graph fullscreen pass requires a Program resource.", __LINE__, __FILE__, __func__);
		}
		setUsedProgram(program);
		auto p = static_cast<Program*>(program.get());
		auto uniformCopy = parameters;
		uniformCopy.bindUniforms(program);
		GL_CHECK(glUniformMatrix4fv(p->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(p->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		// GAMMA is an engine-wide renderer setting, not something PostEffectMaterial
		// authors declare (see every other fullscreen render function, which all
		// auto-inject it from mGamma the same way). Never set here, it stays at
		// GLSL's uniform default of 0, so 1.0/GAMMA is +inf and pow(colour, vec3(inf))
		// crushes everything below 1.0 to black -- this generic function replaced the
		// old bespoke tonemap pass, which did get this injection, and never picked it up.
		int gammaId = p->getUniformId("GAMMA");
		if (gammaId >= 0) GL_CHECK(glUniform1f(gammaId, mGamma));
		// Bind each texture to the unit the program itself samples that name from.
		// The previous code bound to the binding's own position and tried to point
		// the sampler at it with getUniformId, which cannot work: samplers are marked
		// up as _mpp_t_NAME_ and live in mTextures, while getUniformId looks up
		// _mpp_u_NAME_, so it returned -1 for every sampler and glUniform1i(-1, ...)
		// did nothing. Units came from Program::bind instead, in declaration order,
		// so a pass whose sampler bindings were authored in a different order from
		// the shader's declarations sent each texture to the wrong sampler in
		// silence. An undeclared name is now an error rather than the same silence.
		for (auto const& [samplerName, texture] : samplers)
		{
			if (!texture) THROW_MPP("Graph fullscreen pass has an unresolved sampler target.", __LINE__, __FILE__, __func__);
			auto const unit = p->getSamplerUnit(samplerName);
			if (unit < 0)
			{
				THROW_MPP("Graph fullscreen pass binds sampler '" + samplerName + "' which program '" + p->getName() +
					"' does not declare.", __LINE__, __FILE__, __func__);
			}
			texture->bind((uint32_t)unit, 0);
		}
		setBlendState(true);
		setBlendFunctionState(GraphBlendFactor::One, GraphBlendFactor::Zero, GraphBlendFactor::One, GraphBlendFactor::Zero);
		auto quadMesh = ((Model*)mFullscreenQuad.get())->getMesh(0);
		quadMesh->bind(true);
		quadMesh->render(1);
		quadMesh->bind(false);
		setBlendState(false);
		mRenderInfo.batchCount++;
		mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderParticleWeightedOitResolve(Texture* scene, Texture* accumulation, Texture* opticalDepth, Texture* bloom)
	{
		if (!scene || !accumulation || !opticalDepth)
			THROW_MPP("Weighted blended OIT resolve requires scene, accumulation, and optical-depth textures.", __LINE__, __FILE__, __func__);
		mParticleWeightedOitResolveProgram->load();
		UniformCollection parameters;
		parameters.setUniform("HAS_BLOOM", int32_t(bloom ? 1 : 0));
		std::vector<std::pair<std::string, Texture*>> samplers{
			{ "SCENE", scene }, { "ACCUMULATION", accumulation }, { "OPTICAL_DEPTH", opticalDepth }
		};
		// Every declared sampler has a fixed program unit. Bind a harmless valid
		// texture when the optional MRT bloom input is absent; HAS_BLOOM prevents it
		// from contributing to the result.
		samplers.push_back({ "BLOOM", bloom ? bloom : scene });
		renderGraphFullscreen(mParticleWeightedOitResolveProgram, samplers, parameters);
	}

	void RenderSystem::renderParticleDistortionComposite(Texture* scene, Texture* distortion)
	{
		if (!scene || !distortion)
			THROW_MPP("Particle distortion composite requires scene and distortion textures.", __LINE__, __FILE__, __func__);
		mParticleDistortionCompositeProgram->load();
		renderGraphFullscreen(mParticleDistortionCompositeProgram,
			{ { "SCENE", scene }, { "DISTORTION", distortion } }, {});
	}

	void RenderSystem::renderTextureDiagnostic(RenderTexture* source, RenderTargetPtr const& destination, TextureDiagnosticOptions const& options)
	{
		if (!source || !destination || source->isMultisampled())
		{
			THROW_MPP("Texture diagnostics require a resolved texture source and destination.", __LINE__, __FILE__, __func__);
		}
		flushVertexBuffers();
		GLint previousViewport[4]{ mViewportX, mViewportY, static_cast<GLint>(mViewportWidth), static_cast<GLint>(mViewportHeight) };
		auto const previousRasterState = captureRasterState(1);
		pushRenderTarget(destination);
		pushProjectionMatrix(); pushCameraMatrix(); pushModelMatrix();
		setProjection2dOrthographic();
		resetTransform();
		// The shared fullscreen quad is authored at window dimensions; diagnostics
		// render into the editor viewport target, which is usually smaller.
		scaleTransform2d(glm::vec2(
			(float)destination->getWidth() / (float)getWindowWidth(),
			(float)destination->getHeight() / (float)getWindowHeight()));
		setViewport(0, 0, destination->getWidth(), destination->getHeight());
		setDepthTestState(false); setDepthWriteState(false); setCullState(GraphCullMode::None); setBlendState(false); setScissorState(false);
		try
		{
			source->applyMipView(options.mipLevel);
			auto program = static_cast<Program*>(mTextureDiagnosticProgram.get()); setUsedProgram(mTextureDiagnosticProgram);
			GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
			GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), destination->getWidth() / 2.0f, destination->getHeight() / 2.0f));
			GL_CHECK(glUniform1i(program->getUniformId("MODE"), (GLint)options.mode)); GL_CHECK(glUniform1f(program->getUniformId("EXPOSURE"), options.exposure)); GL_CHECK(glUniform1f(program->getUniformId("GAMMA"), mGamma));
			GL_CHECK(glUniform1f(program->getUniformId("DEPTH_NEAR"), options.depthNear)); GL_CHECK(glUniform1f(program->getUniformId("DEPTH_FAR"), options.depthFar)); GL_CHECK(glUniform1i(program->getUniformId("SOURCE"), 0));
			if (options.mode == TextureDiagnosticMode::Depth) source->bindDepth(0); else source->bind(0, 0);
			auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0); mesh->bind(true); mesh->render(1); mesh->bind(false);
			source->restoreMipView();
			mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++; mRenderInfo.batchCount++;
		}
		catch (...)
		{
			source->restoreMipView(); popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); popRenderTarget(); setViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]); applyRasterState(previousRasterState, 1, previousViewport[2], previousViewport[3]); throw;
		}
		popModelMatrix(); popCameraMatrix(); popProjectionMatrix(); popRenderTarget(); setViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]); applyRasterState(previousRasterState, 1, previousViewport[2], previousViewport[3]);
	}

	void RenderSystem::renderToneMappedFullscreenQuad(Texture* texture, float exposure, bool useAcesToneMap)
	{
		flushVertexBuffers();

		auto program = static_cast<Program*>(mToneMapProgram.get());
		setUsedProgram(mToneMapProgram);
		mRenderInfo.programSwitches++;

		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniform1f(program->getUniformId("EXPOSURE"), exposure));
		GL_CHECK(glUniform1f(program->getUniformId("GAMMA"), mGamma));
		GL_CHECK(glUniform1i(program->getUniformId("TONE_MAP_OPERATOR"), useAcesToneMap ? 1 : 0));

		texture->bind(0, 0);
		mRenderInfo.textureSwitches++;

		setBlendState(false);
		auto quadMesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		quadMesh->bind(true);
		quadMesh->render(1);
		quadMesh->bind(false);

		mRenderInfo.batchCount++;
		mRenderInfo.fullscreenQuads++;
	}

	/*
	 * Render a simple quad.
	 *
	 */
	void RenderSystem::renderBloomExtract(Texture* source, float threshold)
	{
		flushVertexBuffers();
		auto program = static_cast<Program*>(mBloomExtractProgram.get());
		setUsedProgram(mBloomExtractProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniform1f(program->getUniformId("THRESHOLD"), threshold));
		source->bind(0);
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderBloomBlur(Texture* source, glm::vec2 const& direction)
	{
		flushVertexBuffers();
		auto program = static_cast<Program*>(mBloomBlurProgram.get());
		setUsedProgram(mBloomBlurProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniform2f(program->getUniformId("DIRECTION"), direction.x, direction.y));
		source->bind(0);
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderFxaa(RenderTexture* source,RenderTargetPtr const& destination)
	{
		if(!source||!destination)THROW_MPP("FXAA pass requires source and destination targets.",__LINE__,__FILE__,__func__);setProjection2dOrthographic();resetTransform();scaleTransform2d(glm::vec2((float)destination->getWidth()/getWindowWidth(),(float)destination->getHeight()/getWindowHeight()));setRenderTarget(destination);setViewport(0,0,destination->getWidth(),destination->getHeight());flushVertexBuffers();auto program=static_cast<Program*>(mFxaaProgram.get());setUsedProgram(mFxaaProgram);GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(),1,GL_FALSE,glm::value_ptr(m3dModelCameraProjectionMatrix)));GL_CHECK(glUniform2f(program->getHalfWindowSizeId(),destination->getWidth()/2.0f,destination->getHeight()/2.0f));source->bind(0);setBlendState(false);auto mesh=static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);mesh->bind(true);mesh->render(1);mesh->bind(false);mRenderInfo.programSwitches++;mRenderInfo.textureSwitches++;mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderTaa(RenderTexture* currentColour,RenderTexture* currentDepth,RenderTexture* historyColour,RenderTexture* historyDepth,RenderTargetPtr const& destination,glm::mat4 const& inverseCurrentViewProjection,glm::mat4 const& previousViewProjection)
	{
		if(!currentColour||!currentDepth||!historyColour||!historyDepth||!destination)THROW_MPP("TAA pass requires current colour/depth, history colour/depth, and a destination.",__LINE__,__FILE__,__func__);setProjection2dOrthographic();resetTransform();scaleTransform2d(glm::vec2((float)destination->getWidth()/getWindowWidth(),(float)destination->getHeight()/getWindowHeight()));setRenderTarget(destination);setViewport(0,0,destination->getWidth(),destination->getHeight());flushVertexBuffers();auto program=static_cast<Program*>(mTaaProgram.get());setUsedProgram(mTaaProgram);GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(),1,GL_FALSE,glm::value_ptr(m3dModelCameraProjectionMatrix)));GL_CHECK(glUniform2f(program->getHalfWindowSizeId(),destination->getWidth()/2.0f,destination->getHeight()/2.0f));GL_CHECK(glUniformMatrix4fv(program->getUniformId("INVERSE_CURRENT_VIEW_PROJECTION"),1,GL_FALSE,glm::value_ptr(inverseCurrentViewProjection)));GL_CHECK(glUniformMatrix4fv(program->getUniformId("PREVIOUS_VIEW_PROJECTION"),1,GL_FALSE,glm::value_ptr(previousViewProjection)));GL_CHECK(glUniform1i(program->getUniformId("CURRENT_COLOUR"),0));GL_CHECK(glUniform1i(program->getUniformId("CURRENT_DEPTH"),1));GL_CHECK(glUniform1i(program->getUniformId("HISTORY_COLOUR"),2));GL_CHECK(glUniform1i(program->getUniformId("HISTORY_DEPTH"),3));currentColour->bind(0);currentDepth->bindDepth(1);historyColour->bind(2);historyDepth->bindDepth(3);setBlendState(false);auto mesh=static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);mesh->bind(true);mesh->render(1);mesh->bind(false);mRenderInfo.programSwitches++;mRenderInfo.textureSwitches+=4;mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderSsaaLanczos(RenderTexture* source,RenderTargetPtr const& destination,glm::vec2 const& direction)
	{
		if(!source||!destination)THROW_MPP("SSAA Lanczos pass requires source and destination targets.",__LINE__,__FILE__,__func__);setProjection2dOrthographic();resetTransform();scaleTransform2d(glm::vec2((float)destination->getWidth()/getWindowWidth(),(float)destination->getHeight()/getWindowHeight()));setRenderTarget(destination);setViewport(0,0,destination->getWidth(),destination->getHeight());flushVertexBuffers();auto program=static_cast<Program*>(mSsaaLanczosProgram.get());setUsedProgram(mSsaaLanczosProgram);GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(),1,GL_FALSE,glm::value_ptr(m3dModelCameraProjectionMatrix)));GL_CHECK(glUniform2f(program->getHalfWindowSizeId(),destination->getWidth()/2.0f,destination->getHeight()/2.0f));GL_CHECK(glUniform2f(program->getUniformId("DIRECTION"),direction.x,direction.y));GL_CHECK(glUniform2f(program->getUniformId("OUTPUT_SIZE"),(float)destination->getWidth(),(float)destination->getHeight()));source->bind(0);setBlendState(false);auto mesh=static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);mesh->bind(true);mesh->render(1);mesh->bind(false);mRenderInfo.programSwitches++;mRenderInfo.textureSwitches++;mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderEnvironmentDebugCube(Texture* environment, Camera* camera)
	{
		if (!environment || !camera || environment->getTextureTarget() != GL_TEXTURE_CUBE_MAP)
		{
			return;
		}
		flushVertexBuffers();
		auto inverseViewProjection = glm::inverse(camera->getProjectionTransform() * camera->getViewTransform());
		auto cameraPosition = camera->getPosition();
		auto const previousRasterState = captureRasterState(1);
		pushProjectionMatrix(); pushCameraMatrix(); pushModelMatrix();
		setProjection2dOrthographic(); resetTransform();
		scaleTransform2d(glm::vec2((float)mRenderTarget->getWidth() / (float)getWindowWidth(),
		                             (float)mRenderTarget->getHeight() / (float)getWindowHeight()));
		auto program = static_cast<Program*>(mEnvironmentDebugCubeProgram.get());
		setUsedProgram(mEnvironmentDebugCubeProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniformMatrix4fv(program->getUniformId("INVERSE_VIEW_PROJECTION"), 1, GL_FALSE, glm::value_ptr(inverseViewProjection)));
		GL_CHECK(glUniform3fv(program->getUniformId("CAMERA_POSITION"), 1, glm::value_ptr(cameraPosition)));
		for (int unit = 0; unit < program->getNumSamplers(); ++unit)
			if (program->getSamplerName(unit) == "ENVIRONMENT") environment->bind((uint32_t)unit);
		setDepthTestState(false); setDepthWriteState(false);
		setCullState(GraphCullMode::None); setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		popModelMatrix(); popCameraMatrix(); popProjectionMatrix();
		applyRasterState(previousRasterState, 1, mRenderTarget->getWidth(), mRenderTarget->getHeight());
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++; mRenderInfo.batchCount++;
	}

	void RenderSystem::renderSSAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, SSAOOptions const& options)
	{
		if (!depth || !depth->getDepthTextureId()) THROW_MPP("SSAO requires a sampled depth texture.", __LINE__, __FILE__, __func__);
		flushVertexBuffers();
		auto program = static_cast<Program*>(mSsaoRawProgram.get());
		setUsedProgram(mSsaoRawProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniformMatrix4fv(program->getUniformId("PROJECTION"), 1, GL_FALSE, glm::value_ptr(projection)));
		GL_CHECK(glUniformMatrix4fv(program->getUniformId("INVERSE_PROJECTION"), 1, GL_FALSE, glm::value_ptr(inverseProjection)));
		GL_CHECK(glUniform1f(program->getUniformId("RADIUS"), options.radius));
		GL_CHECK(glUniform1f(program->getUniformId("INTENSITY"), options.intensity));
		GL_CHECK(glUniform1f(program->getUniformId("BIAS"), options.bias));
		GL_CHECK(glUniform1f(program->getUniformId("POWER"), options.power));
		GL_CHECK(glUniform1i(program->getUniformId("SAMPLE_COUNT"), options.sampleCount));
		for (int unit = 0; unit < program->getNumSamplers(); ++unit) if (program->getSamplerName(unit) == "DEPTH") depth->bindDepth((uint32_t)unit);
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderGTAORaw(RenderTexture* depth, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options)
	{
		renderGTAORaw(depth, nullptr, projection, inverseProjection, options);
	}

	void RenderSystem::renderGTAORaw(RenderTexture* depth, Texture* normals, glm::mat4 const& projection, glm::mat4 const& inverseProjection, GTAOOptions const& options)
	{
		if (!depth || !depth->getDepthTextureId()) THROW_MPP("GTAO requires a sampled depth texture.", __LINE__, __FILE__, __func__);
		if (options.normalSource == GTAONormalSource::Mrt && (!normals || normals->getTextureTarget() != GL_TEXTURE_2D || normals->getInternalFormat() != GL_RG16F)) THROW_MPP("GTAO MRT normals require a valid RG16F normals texture.", __LINE__, __FILE__, __func__);
		flushVertexBuffers();
		auto program = static_cast<Program*>(mGtaoRawProgram.get());
		setUsedProgram(mGtaoRawProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniformMatrix4fv(program->getUniformId("PROJECTION"), 1, GL_FALSE, glm::value_ptr(projection)));
		GL_CHECK(glUniformMatrix4fv(program->getUniformId("INVERSE_PROJECTION"), 1, GL_FALSE, glm::value_ptr(inverseProjection)));
		GL_CHECK(glUniform1f(program->getUniformId("RADIUS"), options.radius));
		GL_CHECK(glUniform1f(program->getUniformId("INTENSITY"), options.intensity));
		GL_CHECK(glUniform1f(program->getUniformId("THICKNESS"), options.thickness));
		GL_CHECK(glUniform1f(program->getUniformId("HORIZON_BIAS"), options.horizonBias));
		GL_CHECK(glUniform1f(program->getUniformId("FALLOFF_START"), options.falloffStart));
		GL_CHECK(glUniform1f(program->getUniformId("FALLOFF_END"), options.falloffEnd));
		GL_CHECK(glUniform1i(program->getUniformId("SLICE_COUNT"), options.sliceCount));
		GL_CHECK(glUniform1i(program->getUniformId("STEPS_PER_SLICE"), options.stepsPerSlice));
		GL_CHECK(glUniform1f(program->getUniformId("POWER"), options.power));
		GL_CHECK(glUniform1i(program->getUniformId("NORMAL_SOURCE"), options.normalSource == GTAONormalSource::Mrt ? 1 : 0));
		for (int unit = 0; unit < program->getNumSamplers(); ++unit)
		{
			if (program->getSamplerName(unit) == "DEPTH") depth->bindDepth((uint32_t)unit);
			else if (program->getSamplerName(unit) == "NORMALS" && normals) normals->bind((uint32_t)unit);
		}
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches++; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderSSAOBlur(Texture* ambientOcclusion, RenderTexture* depth, int blurRadius)
	{
		if (!ambientOcclusion || !depth || !depth->getDepthTextureId()) THROW_MPP("SSAO blur requires ambient-occlusion and sampled depth textures.", __LINE__, __FILE__, __func__);
		flushVertexBuffers();
		auto program = static_cast<Program*>(mSsaoBlurProgram.get());
		setUsedProgram(mSsaoBlurProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniform1i(program->getUniformId("BLUR_RADIUS"), blurRadius));
		for (int unit = 0; unit < program->getNumSamplers(); ++unit)
		{
			auto const& sampler = program->getSamplerName(unit);
			if (sampler == "AMBIENT_OCCLUSION") ambientOcclusion->bind((uint32_t)unit);
			else if (sampler == "DEPTH") depth->bindDepth((uint32_t)unit);
		}
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches += 2; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderSSAOCombine(Texture* scene, Texture* ambientOcclusion, Texture* modulation)
	{
		if (!scene || !ambientOcclusion) THROW_MPP("SSAO combine requires scene and ambient-occlusion textures.", __LINE__, __FILE__, __func__);
		flushVertexBuffers();
		auto const& combineProgram = modulation ? mSsaoCombineModulatedProgram : mSsaoCombineProgram;
		auto program = static_cast<Program*>(combineProgram.get());
		setUsedProgram(combineProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		for (int unit = 0; unit < program->getNumSamplers(); ++unit)
		{
			auto const& sampler = program->getSamplerName(unit);
			if (sampler == "SCENE") scene->bind((uint32_t)unit);
			else if (sampler == "AMBIENT_OCCLUSION") ambientOcclusion->bind((uint32_t)unit);
			else if (sampler == "MODULATION" && modulation) modulation->bind((uint32_t)unit);
		}
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches += (modulation ? 3 : 2); mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderBloomCombine(Texture* scene, Texture* bloom, float intensity)
	{
		flushVertexBuffers();
		auto program = static_cast<Program*>(mBloomCombineProgram.get());
		setUsedProgram(mBloomCombineProgram);
		GL_CHECK(glUniformMatrix4fv(program->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(program->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));
		GL_CHECK(glUniform1f(program->getUniformId("INTENSITY"), intensity));
		// Bind against the Program sampler table. Parser texture slots are sorted
		// by sampler name, and getUniformId() intentionally does not address
		// @Texture declarations.
		for (int unit = 0; unit < program->getNumSamplers(); ++unit)
		{
			auto const& sampler = program->getSamplerName(unit);
			if (sampler == "SCENE") scene->bind((uint32_t)unit);
			else if (sampler == "BLOOM") bloom->bind((uint32_t)unit);
		}
		setBlendState(false);
		auto mesh = static_cast<Model*>(mFullscreenQuad.get())->getMesh(0);
		mesh->bind(true); mesh->render(1); mesh->bind(false);
		mRenderInfo.programSwitches++; mRenderInfo.textureSwitches += 2; mRenderInfo.fullscreenQuads++;
	}

	void RenderSystem::renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame)
	{
		renderQuad(x, y, width, height, colour, alphaBlend, wireFrame, mNoTexture);
	}

	/*
	 * Render a simple quad.
	 *
	 */
	void RenderSystem::renderQuad(int x, int y, int width, int height, Colour const& colour, bool alphaBlend, bool wireFrame, ResourcePtr texture)
	{
		flushVertexBuffers();

		// Same single-output contract as renderFullscreenQuad above.
		PrimaryColourOutputDraw primaryOutput(this);

		// Set program
		auto p = static_cast<Program*>(mFullscreenProgram.get());

		setUsedProgram(mFullscreenProgram);
		mRenderInfo.programSwitches++;

		pushModelMatrix();
		translateTransform2d(glm::vec2(x, mRenderTarget->getHeight() - y));
		GL_CHECK(scaleTransform2d(glm::vec2(width / (float)mRenderTarget->getWidth(), height / (float)mRenderTarget->getHeight())));

		GL_CHECK(glUniformMatrix4fv(p->getModelCameraProjectionMatrixId(), 1, GL_FALSE, glm::value_ptr(m3dModelCameraProjectionMatrix)));
		GL_CHECK(glUniform2f(p->getHalfWindowSizeId(), mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f));

		int gammaId = p->getUniformId("GAMMA");

		if (gammaId >= 0)
		{
			GL_CHECK(glUniform1f(gammaId, mGamma));
		}

		GL_CHECK(glUniform4f(p->getUniformId("DIFFUSE"), colour.red, colour.green, colour.blue, colour.alpha));

		// Set texture
		((Texture*)texture.get())->bind(0);
		mRenderInfo.textureSwitches++;

		// Bind mesh
		if (alphaBlend)
		{
			setBlendState(true);
			setBlendFunctionState(GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha, GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha);
		}

		if (wireFrame)
		{
			setFillModeState(GraphFillMode::Line);
		}

		auto quadMesh = ((Model*)mFullscreenQuad.get())->getMesh(0);
		quadMesh->bind(true);
		quadMesh->render(1);

		// Unbind
		quadMesh->bind(false);

		if (alphaBlend)
		{
			setBlendState(false);
		}

		if (wireFrame)
		{
			setFillModeState(GraphFillMode::Fill);
		}

		popModelMatrix();

		mRenderInfo.batchCount++;
	}

	/*
	 * Render a text string as a series of quads.
	 *
	 */
	void RenderSystem::renderText(string const& text, int x, int y, Colour const& colour)
	{
		// Text is a screen-space overlay and must not inherit the projection or
		// depth state left by the preceding 3D or render-graph pass.
		setProjection2dOrthographic();

		// Set text mesh for updating
		Model* textModel = (Model*)mTextMesh.get();
		Mesh* textMesh = textModel->getMesh(0);
		VertexBuffer* vertexBuffer = textMesh->getVertexBuffer(0);

		y = (int)(mWindowHeight - y - 16);
		
		int offset = 0;
		int count = buildTextVertexBuffer(vertexBuffer, text, offset, x, y);
		vertexBuffer->mapBufferData(offset);
		
		mTextUniforms->updateUniform("COLOUR", glm::vec4(colour.red, colour.green, colour.blue, colour.alpha));
		mTextParams->setModelPrimitiveCount(count);

		renderModelImmediate(static_cast<Model const&>(*mTextMesh.get()), true, mTextParams);
	}
	
	/*
	 * Render lines of text.
	 *
	 */
	void RenderSystem::renderText(vector<string> const& text, int x, int y, Colour const& colour)
	{
		setProjection2dOrthographic();

		// Set text mesh for updating
		Model* textModel = (Model*)mTextMesh.get();
		Mesh* textMesh = textModel->getMesh(0);
		VertexBuffer* vertexBuffer = textMesh->getVertexBuffer(0);

		y = (int)(mWindowHeight - y - 16); 
		int count = 0, offset = 0;
		for (uint32_t i = 0; i < text.size(); ++i)
		{
			count += buildTextVertexBuffer(vertexBuffer, text[i], offset, x, y - i * 16);
		}

		vertexBuffer->mapBufferData(offset);

		mTextUniforms->updateUniform("COLOUR", glm::vec4(colour.red, colour.green, colour.blue, colour.alpha));
		mTextParams->setModelPrimitiveCount(count);

		renderModelImmediate(static_cast<Model const&>(*mTextMesh.get()), true, mTextParams);
	}

	/*
	 * Render coloured text using tags.
	 *
	 */
	void RenderSystem::renderTextFormatted(string const& text, int x, int y)
	{
		setProjection2dOrthographic();

		Model* textModel = (Model*)mColouredTextMesh.get();
		Mesh* textMesh = textModel->getMesh(0);
		VertexBuffer* vertexBuffer = textMesh->getVertexBuffer(0);

		y = (int)(mWindowHeight - y - 16);

		int offset = 0;
		int count = buildColouredTextVertexBuffer(vertexBuffer, text, offset, x, y);
		vertexBuffer->mapBufferData(offset);

		mTextUniforms->updateUniform("COLOUR", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		mTextParams->setModelPrimitiveCount(count);

		renderModelImmediate(static_cast<Model const&>(*mColouredTextMesh.get()), true, mTextParams);
	}
	
	void RenderSystem::renderTextFormatted(vector<string> const& text, int x, int y)
	{
		setProjection2dOrthographic();

		Model* textModel = (Model*)mColouredTextMesh.get();
		Mesh* textMesh = textModel->getMesh(0);
		VertexBuffer* vertexBuffer = textMesh->getVertexBuffer(0);

		y = (int)(mWindowHeight - y - 16);
		int count = 0, offset = 0;
		for (uint32_t i = 0; i < text.size(); ++i)
		{
			count += buildColouredTextVertexBuffer(vertexBuffer, text[i], offset, x, y - i * 16);
		}

		vertexBuffer->mapBufferData(offset);

		mTextUniforms->updateUniform("COLOUR", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		mTextParams->setModelPrimitiveCount(count);

		renderModelImmediate(static_cast<Model const&>(*mColouredTextMesh.get()), true, mTextParams);
	}

	void RenderSystem::renderBufferImmediate(int8_t const* vertexData, uint32_t vertexStride, uint32_t numVertices, int8_t* const indexData, uint32_t indexWidth, uint32_t numIndices, vector<VertexBufferRenderCommand> const& commands)
	{
		flushVertexBuffers();

		size_t indexBytes = indexWidth >> 3;
		bool useIndices = indexData != nullptr && (indexBytes == 2 || indexBytes == 4);

		setScissorState(true);
		setBlendState(true);
		setBlendEquationState(GraphBlendOp::Add, GraphBlendOp::Add);
		setBlendFunctionState(GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha, GraphBlendFactor::One, GraphBlendFactor::OneMinusSourceAlpha);

		// Create VAO.  The VAO stores the vertex attributes and the VBO and IBO
		GLuint vao{ 0 };
		GL_CHECK(glGenVertexArrays(1, &vao));

		GL_CHECK(glBindVertexArray(vao));
		
		GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, mInternalVBO));

		if (useIndices)
		{
			GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mInternalIBO));
		}

		// Enable vertex attributes
		size_t posOffset{ 0 }, texOffset{ 8 }, colOffset{ 16 };

		GL_CHECK(glEnableVertexAttribArray(0));
		GL_CHECK(glEnableVertexAttribArray(1));
		GL_CHECK(glEnableVertexAttribArray(2));
		GL_CHECK(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)vertexStride, (GLvoid const*)posOffset));
		GL_CHECK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)vertexStride, (GLvoid const*)texOffset));
		GL_CHECK(glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)vertexStride, (GLvoid const*)colOffset));

		// Upload vertex and index data
		size_t vertexDataSize = vertexStride * numVertices;
		GL_CHECK(glBufferData(GL_ARRAY_BUFFER, (GLsizei)vertexDataSize, (GLvoid const*)vertexData, GL_STREAM_DRAW));
		
		if (useIndices)
		{
			size_t indexDataSize = indexBytes * numIndices;
			GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizei)indexDataSize, (GLvoid const*)indexData, GL_STREAM_DRAW));
		}

		// Render
		for (auto const& cmd : commands)
		{
			// Set program, uniforms, textures
			auto materialRes = cmd.material ? cmd.material : mInternalMaterial;
			auto mat = static_cast<Material*>(materialRes.get());

			setUsedProgram(mat->getProgram());
			mat->setUniforms();

			// Uniforms
			auto p = static_cast<Program*>(mat->getProgram().get());
			
			auto hwsId = p->getHalfWindowSizeId();
			auto mcpId = p->getModelCameraProjectionMatrixId();

			if (hwsId >= 0)
			{
				glm::vec2 halfWindowSize(mRenderTarget->getWidth() / 2.0f, mRenderTarget->getHeight() / 2.0f);
				GL_CHECK(glUniform2fv(hwsId, 1, glm::value_ptr(halfWindowSize)));
			}

			if (mcpId >= 0)
			{
				glm::mat4 mcp(
					cmd.scale[0], 0.0f, 0.0f, 0.0f,
					0.0f, cmd.scale[1], 0.0f, 0.0f,
					0.0f, 0.0f, cmd.scale[2], 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				);
				
				GL_CHECK(glUniformMatrix4fv(mcpId, 1, GL_FALSE, glm::value_ptr(mcp)));
			}

			int gammaId = p->getUniformId("GAMMA");

			if (gammaId >= 0)
			{
				GL_CHECK(glUniform1f(gammaId, mGamma));
			}

			// Textures
			for (int i = 0; i < mat->getNumTextures(); ++i)
			{
				auto textureRes = (size_t)i < cmd.textures.size() && cmd.textures[i]
					? cmd.textures[i]
					: mat->getTexture(i);
				static_cast<Texture*>(textureRes.get())->bind((uint32_t)i);
			}

			if (cmd.clipSize[0] > 0 && cmd.clipSize[1] > 0)
			{
				setScissorRectangleState({ static_cast<uint32_t>(cmd.clipMin[0]), static_cast<uint32_t>(cmd.clipMin[1]), static_cast<uint32_t>(cmd.clipSize[0]), static_cast<uint32_t>(cmd.clipSize[1]) });
			}

			// Draw
			if (useIndices)
			{
				GLenum indexType = indexBytes == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

				auto offset = (void*)(intptr_t)(cmd.offset * indexBytes);
				size_t count = cmd.count != ~0u ? (size_t)cmd.count : (numIndices - cmd.offset);

				GL_CHECK(glDrawElements(GL_TRIANGLES, (GLsizei)count, indexType, offset));
			}
			else
			{
				uint32_t offset = cmd.offset * 3;
				size_t count = cmd.count != ~0u ? (size_t)(cmd.count * 3) : (numVertices - cmd.offset * 3);

				GL_CHECK(glDrawArrays(GL_TRIANGLES, offset, (GLsizei)count));
			}
		}

		// Destroy VAO
		GL_CHECK(glDeleteVertexArrays(1, &vao));

		setScissorState(false);
		setBlendState(false);
	}

	void RenderSystem::setDebugPreMessages(vector<string> const& messages)
	{
		mDebugPreMessages = messages;
	}

	void RenderSystem::setDebugPostMessages(vector<string> const& messages)
	{
		mDebugPostMessages = messages;
	}

	/*
	 * Render debugging panel.
	 *
	 */
	void RenderSystem::renderDebugPanel()
	{
		setProjection2dOrthographic();

		vector<string> lines;

		copy(mDebugPreMessages.begin(), mDebugPreMessages.end(), back_inserter(lines));

#ifdef MPP_PROFILE_BUILD
		map<string, uint64_t> profileResults = mProfiler->getSamples();

		TimeSample timeSample;
		timeSample.frameTime = -1;
		timeSample.driverWaitsGPU = -1;
		timeSample.driverWaitsKernel = -1;
		timeSample.driverWaitsLock = -1;
		timeSample.driverWaitsRender = -1;
		timeSample.driverWaitsSwap = -1;

		for (auto it = profileResults.begin(); it != profileResults.end(); ++it)
		{
			string profile = it->first;
			uint64_t result = it->second;

			string msg, unit;

			if (profile == "OGL frame time")
			{
				timeSample.frameTime = (int)result;
			}
			else if (profile == "OGL driver waits for GPU")
			{
				timeSample.driverWaitsGPU = (int)result;
			}
			else if (profile == "OGL driver waits for GPU")
			{
				timeSample.driverWaitsKernel = (int)result;
			}
			else if (profile == "OGL driver waits for kernel")
			{
				timeSample.driverWaitsKernel = (int)result;
			}
			else if (profile == "OGL driver waits for lock")
			{
				timeSample.driverWaitsLock = (int)result;
			}
			else if (profile == "OGL driver waits for render")
			{
				timeSample.driverWaitsRender = (int)result;
			}
			else if (profile == "OGL driver waits for swap")
			{
				timeSample.driverWaitsSwap = (int)result;
			}

			if (profile == "OGL frame time" ||
				profile == "OGL driver time waiting" ||
				profile == "OGL driver waits for GPU" ||
				profile == "OGL driver waits for kernel" ||
				profile == "OGL driver waits for lock" ||
				profile == "OGL driver waits for render" ||
				profile == "OGL driver waits for swap")
			{
				switch (mTimeUnit)
				{
				case TimeUnit::Nanoseconds:
					unit = "ns";
					result *= 1000000;
					break;
				case TimeUnit::Microseconds:
					unit = "us";
					result *= 1000;
					break;
				case TimeUnit::Milliseconds:
					unit = "ms";
					result *= 1;
					break;
				case TimeUnit::Seconds:
					unit = "s";
					result /= 1000;
					break;
				}

				msg = std::format("{}: {} {}", profile, result, unit);
			}
			else if (profile == "OGL memory allocated" ||
				profile == "OGL memory allocated (textures)" ||
				profile == "OGL memory allocated (vertex)")
			{
				switch (mSizeUnit)
				{
				case SizeUnit::Gigabytes:
					unit = "Gb";
					result /= 1000000000;
					break;
				case SizeUnit::Megabytes:
					unit = "Mb";
					result /= 1000000;
					break;
				case SizeUnit::Kilobytes:
					unit = "Kb";
					result /= 1000;
					break;
				case SizeUnit::Bytes:
					unit = "b";
					result *= 1;
					break;
				}

				msg = std::format("{}: {} {}", profile, result, unit);
			}
			else if (profile == "Total GPU memory" ||
				profile == "Total available GPU memory" ||
				profile == "Current available GPU memory")
			{
				msg = std::format("{}: {} Kb", profile, result);
			}
			else
			{
				msg = std::format("{}: {}", profile, result);
			}

			lines.push_back(msg);
		}
#else
		lines.push_back(std::format("Batches: {}", mRenderInfo.batchCount));
		lines.push_back(std::format("Primitives: {}", mRenderInfo.primitivesRendered));
		lines.push_back(std::format("Triangles: {}", mRenderInfo.trianglesRendered));
#endif

		lines.push_back(std::format("Program switches: {}", mRenderInfo.programSwitches));
		lines.push_back(std::format("Texture switches: {}", mRenderInfo.textureSwitches));
		lines.push_back(std::format("Screen quads: {}", mRenderInfo.fullscreenQuads));

		copy(mDebugPostMessages.begin(), mDebugPostMessages.end(), back_inserter(lines));

		// Get resource info
		uint32_t numTotalResources, numDeclaredResources, numCreatedResources, numLoadedResources;
		mResourceMgr->getResourceCounts(numTotalResources, numDeclaredResources, numCreatedResources, numLoadedResources);

		lines.push_back(std::format("Resources : {} ([#FF0000FF]{}[#FFFFFFFF] / [#FFFF00FF]{}[#FFFFFFFF] / [#00FF00FF]{}[#FFFFFFFF])",
			numTotalResources, 
			numDeclaredResources, 
			numCreatedResources, 
			numLoadedResources));

		// Calculate padding and placement
		size_t width = 0;
		for (auto const& line: lines)
		{
			auto thisWidth = line.length();

			// Factor out colour formatting
			string::size_type pos{ 0 };
			while ((pos = line.find("[#", pos)) != string::npos)
			{
				thisWidth -= 11;
				pos += 2;
			}

			width = max(width, thisWidth);
		}

		// Background, in case the screen in that location is the same colour as the text
		int w = (int)width * 8, h = (int)(lines.size() + 1) * 16 - 8;
		int x = 0, y = (GLsizei)mWindowHeight - h;
		
		renderQuad(x, (GLsizei)mWindowHeight, w, h, Colour(0.5f, 0.625f, 0.87f, 0.85f), true, false);

		// Render batch information
		renderTextFormatted(lines, x, y);

#ifdef MPP_PROFILE_BUILD

		x = x + w + 10;
		w = mSamplesToRecord;
		h = 32 * 4;

		renderQuad(x, mWindowHeight, w, h, Colour(0.5f, 0.625f, 0.87f, 0.85f), true, false);

		// Render graph
		while (mProfileTimeSamples.size() >= mSamplesToRecord)
		{
			mProfileTimeSamples.pop_front();
		}
		
		mProfileTimeSamples.push_back(timeSample);
		
		mProfileLines->startUpdate(mSamplesToRecord * 5 + 2);
		float* posDataDst = (float*)mProfileLines->getPositionData();

		int xp = x, yp;
		for (uint32_t i = 0; i < mProfileTimeSamples.size(); ++i)
		{
			yp = 0;
			auto const& sample = mProfileTimeSamples[i];

			// Driver waiting for GPU
			*posDataDst++ = xp + i;
			*posDataDst++ = yp;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;

			*posDataDst++ = xp + i;
			*posDataDst++ = yp + sample.driverWaitsGPU * 4;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;

			yp += sample.driverWaitsGPU * 4;

			// Driver waiting for kernel
			*posDataDst++ = xp + i;
			*posDataDst++ = yp;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;

			*posDataDst++ = xp + i;
			*posDataDst++ = yp + sample.driverWaitsKernel * 4;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;

			yp += sample.driverWaitsKernel * 4;

			// Driver waiting for lock
			*posDataDst++ = xp + i;
			*posDataDst++ = yp;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;

			*posDataDst++ = xp + i;
			*posDataDst++ = yp + sample.driverWaitsLock * 4;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;

			yp += sample.driverWaitsLock * 4;

			// Driver waiting for render
			*posDataDst++ = xp + i;
			*posDataDst++ = yp;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;

			*posDataDst++ = xp + i;
			*posDataDst++ = yp + sample.driverWaitsRender * 4;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;

			yp += sample.driverWaitsRender * 4;

			// Driver waiting for swap
			*posDataDst++ = xp + i;
			*posDataDst++ = yp;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;

			*posDataDst++ = xp + i;
			*posDataDst++ = yp + sample.driverWaitsSwap * 4;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 1.0f;
			*posDataDst++ = 0.0f;
			*posDataDst++ = 1.0f;
		}

		// Lines at 16 and 32ms
		*posDataDst++ = xp;
		*posDataDst++ = 16 * 4;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 0.0f;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 1.0f;

		*posDataDst++ = xp + mSamplesToRecord;
		*posDataDst++ = 16 * 4;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 0.0f;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 1.0f;

		*posDataDst++ = xp;
		*posDataDst++ = 32 * 4;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 0.0f;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 1.0f;

		*posDataDst++ = xp + mSamplesToRecord;
		*posDataDst++ = 32 * 4;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 0.0f;
		*posDataDst++ = 1.0f;
		*posDataDst++ = 1.0f;

		mProfileLines->finishUpdate(mProfileTimeSamples.size() * 5 + 2);

		renderModelImmediate(*mProfileLines, false);
#endif
	}

	void RenderSystem::setupRenderMeshInstance(MeshInstance* meshInstance, VertexBufferRenderCommand const& renderCmd, uint64_t sortKey, uint64_t* currentProgramKey, vector<uint64_t>* currentTextureKeys, Material** currentMaterial, vector<string>* flowStateChanges)
	{
		// Mask off program and see if it has changed from previous.
		uint64_t thisProgramKey = sortKey;
		thisProgramKey >>= MPP_RENDER_SORT_PROGRAM_BITS_OFFSET;
		thisProgramKey &= ((1 << MPP_RENDER_SORT_PROGRAM_BITS_SIZE) - 1);

		// Set program
		bool programChanged = false;
		if (thisProgramKey != *currentProgramKey)
		{
			auto program = mResourceMgr->getProgramBySortId((uint32_t)thisProgramKey);
			setUsedProgram(program);

			*currentProgramKey = thisProgramKey;
			programChanged = true;
			if (flowStateChanges) flowStateChanges->push_back("Program: " + static_cast<Program*>(program.get())->getName());
			mRenderInfo.programSwitches++;
		}

		// Set uniforms if the program or material have changed.
		auto material = static_cast<Material*>(renderCmd.material.get());
		bool const uniformsChanged = programChanged || material != *currentMaterial;
		if (uniformsChanged)
		{
			material->setUniforms();
			// GAMMA is an engine-wide renderer setting, not an authored material
			// value, so every other render path injects it from mGamma. This one did
			// not, leaving it at GLSL's uniform default of 0: 1.0/GAMMA is then +inf
			// and pow(colour, vec3(inf)) resolves to NaN, which the blend and the
			// unorm target turn into black. Every batched draw whose shader gamma
			// -corrects -- renderText's overlays among them -- came out black.
			auto const gammaId = static_cast<Program*>(material->getProgram().get())->getUniformId("GAMMA");
			if (gammaId >= 0)
			{
				GL_CHECK(glUniform1f(gammaId, mGamma));
			}
		}

		*currentMaterial = material;

		// Bind every material sampler. Texture keys are intentionally kept outside
		// the packed sort key: PBR needs more than the legacy two texture units.
		const size_t textureCount = (size_t)material->getNumTextures();
		if (textureCount > mCaps.maxFragmentTextureUnits)
		{
			THROW_MPP("Material requires more fragment texture units than supported by this renderer.", __LINE__, __FILE__, __func__);
		}
		if (currentTextureKeys->size() < textureCount)
		{
			currentTextureKeys->resize(textureCount, 0);
		}
		auto materialProgram = static_cast<Program*>(material->getProgram().get());
		// The prefiltered specular chain length is a property of whichever cubemap
		// is finally bound below, which may be the material's own map, a pipeline
		// override, or a neutral fallback. Resolve it alongside the binding rather
		// than assuming a fixed mip count in the shader.
		Texture const* prefilteredSpecular = nullptr;
		// Same question for water's SSR blur: the resolved scene colour's mip chain
		// is a property of the graph image the water pass bound, not of the material.
		Texture const* resolvedSceneColour = nullptr;
		for (size_t i = 0; i < textureCount; ++i)
		{
			ResourcePtr textureResource = i < renderCmd.textures.size() && renderCmd.textures[i]
				? renderCmd.textures[i]
				: material->getTexture((int)i);

			// Pipeline-owned samplers are authoritative for matching shader names.
			// This generalizes PBR IBL binding and will also bind generic shadow
			// maps without making them material-owned texture slots.
			auto const& samplerName = materialProgram->getSamplerName((int)i);
			auto* activeShadowTexture = dynamic_cast<RenderTexture*>(mActiveShadowDepthTarget.get());
			bool const activePointShadow = activeShadowTexture && activeShadowTexture->getAttachmentTextureTarget() == GL_TEXTURE_CUBE_MAP;
			if (activeShadowTexture && ((samplerName == "SHADOW_MAP" && !activePointShadow) ||
				(samplerName == "POINT_SHADOW_MAP" && activePointShadow)))
			{
				activeShadowTexture->bindDepth((uint32_t)i);
				(*currentTextureKeys)[i] = (uint64_t)(uintptr_t)mActiveShadowDepthTarget.get();
				if (flowStateChanges) flowStateChanges->push_back("Texture unit " + std::to_string(i) + (activePointShadow ? ": point shadow depth cubemap" : ": shadow depth"));
				mRenderInfo.textureSwitches++;
				continue;
			}
			auto pipelineSampler = mActivePipelineSamplerOverrides.find(samplerName);
			if (pipelineSampler != mActivePipelineSamplerOverrides.end() && pipelineSampler->second)
			{
				// A depth-only render target has no colour attachment to bind; the
				// aspect a sampler wants from it is the depth texture. This is the
				// same case the SHADOW_MAP branch above handles, generalized so any
				// pipeline-bound depth image works -- water's PBR_SCENE_DEPTH being
				// the first one that is not a shadow map.
				auto* depthOnly = dynamic_cast<RenderTexture*>(pipelineSampler->second.get());
				if (depthOnly && depthOnly->getNumColourAttachments() == 0 && depthOnly->getDepthTextureId() != 0)
				{
					uint64_t const depthKey = (uint64_t)(uintptr_t)depthOnly;
					if ((*currentTextureKeys)[i] != depthKey)
					{
						depthOnly->bindDepth((uint32_t)i);
						(*currentTextureKeys)[i] = depthKey;
						if (flowStateChanges) flowStateChanges->push_back("Texture unit " + std::to_string(i) + ": " + depthOnly->getName() + " (depth)");
						mRenderInfo.textureSwitches++;
					}
					continue;
				}
				textureResource = pipelineSampler->second;
			}
			auto texture = static_cast<Texture*>(textureResource.get());
			if (uniformsChanged && samplerName == "PBR_PREFILTERED_SPECULAR_MAP") prefilteredSpecular = texture;
			if (uniformsChanged && samplerName == "PBR_SCENE_COLOUR_RESOLVED") resolvedSceneColour = texture;
			const uint64_t textureKey = (uint64_t)(uintptr_t)texture;
			if ((*currentTextureKeys)[i] != textureKey)
			{
				texture->bind((uint32_t)i);
				(*currentTextureKeys)[i] = textureKey;
				if (flowStateChanges) flowStateChanges->push_back("Texture unit " + std::to_string(i) + ": " + texture->getName());
				mRenderInfo.textureSwitches++;
			}
		}
		if (prefilteredSpecular)
		{
			// Uniform state belongs to the program object, so this only needs
			// re-uploading when the program or material changes, exactly like the
			// material's own uniforms above.
			auto const location = materialProgram->getUniformId("PBR_PREFILTERED_MAX_LOD");
			if (location >= 0)
			{
				auto const levels = std::max(1u, prefilteredSpecular->getMipLevels());
				GL_CHECK(glUniform1f(location, (float)(levels - 1)));
			}
		}
		if (resolvedSceneColour)
		{
			auto const location = materialProgram->getUniformId("PBR_SCENE_COLOUR_MAX_LOD");
			if (location >= 0)
			{
				auto const levels = std::max(1u, resolvedSceneColour->getMipLevels());
				GL_CHECK(glUniform1f(location, (float)(levels - 1)));
			}
		}

		// Bind mesh material (including program), and vertex buffers
		meshInstance->mwMesh->bind(true);

		// Go through all mesh uniforms and set them, then apply the active pass's
		// authoritative values. The latter deliberately happens last so a virtual
		// camera cannot be mistaken for the main camera by host material code.
		meshInstance->bindUniforms();
		mActivePipelineUniformOverrides.bindUniforms(material->getProgram());

		// Wireframe?
		setFillModeState(meshInstance->mWireframe ? GraphFillMode::Line : GraphFillMode::Fill);
		if (flowStateChanges && meshInstance->mWireframe) flowStateChanges->push_back("Polygon mode: line");

		if (meshInstance->mCullBackFaces)
		{
			setCullState(GraphCullMode::Back);
			if (flowStateChanges) flowStateChanges->push_back("Cull face: back");
		}

		// Blend?
		if (meshInstance->mBlend)
		{
			setBlendState(true);
			setBlendFunctionState(GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha, GraphBlendFactor::SourceAlpha, GraphBlendFactor::OneMinusSourceAlpha);
			setDepthWriteState(false);
			if (flowStateChanges) { flowStateChanges->push_back("Blend: src alpha / one minus src alpha"); flowStateChanges->push_back("Depth write: disabled"); }
		}
	}

	void RenderSystem::teardownRenderMeshInstance(MeshInstance* meshInstance)
	{
		// Unbind
		meshInstance->mwMesh->bind(false);

		setFillModeState(GraphFillMode::Fill);

		if (meshInstance->mCullBackFaces)
		{
			setCullState(GraphCullMode::None);
		}

		if (meshInstance->mBlend)
		{
			setBlendState(false);
			setDepthWriteState(true);
		}
	}

	/*
	 * Render any meshes we have.
	 *
	 */
	void RenderSystem::flushVertexBuffers()
	{
		// Extract and sort list of render commands
		vector<SortedRenderCommand> renderCommands;

		for (size_t i = 0; i < mModelInstances->getCount(); ++i)
		{
			auto modelInstance = mModelInstances->getObject((uint32_t)i);
			auto const& modelMeshInstances = modelInstance->getMeshInstances();
		
			for (auto const& mi: modelMeshInstances)
			{
				if (mi->render())
				{
					// Fix up render commands here.  If there are none, then add a default one.
					if (mi->mRenderCommands.empty())
					{
						VertexBufferRenderCommand cmd{};
						mi->addRenderCommand(cmd);
					}

					for (auto const& renderCmd : mi->mRenderCommands)
					{
						// Create sort key
						uint64_t sortKey = 0;

						auto material = static_cast<Material*>(renderCmd.material.get());

						// Program. Textures are bound from their dynamic sampler list in
						// setupRenderMeshInstance rather than being limited to two keys.
						uint64_t programKey = (uint64_t)((Program*)material->getProgram().get())->getSortId();
						programKey <<= MPP_RENDER_SORT_PROGRAM_BITS_OFFSET;
						sortKey |= programKey;

						auto modelMatrix = mi->mModelMatrix * mi->mLocalTransform;
						auto modelPosition = glm::vec3(modelMatrix[3]);
						auto viewDelta = modelPosition - mi->mViewPos;
						auto viewDistanceSquared = glm::dot(viewDelta, viewDelta);

						renderCommands.push_back({ sortKey, viewDistanceSquared, renderCmd, mi });
					}
				}
			}
		}

		// Sort in 3D mode.  In Orthographic/2D assume everything is specified in order.
		if (mProjectionType == ProjectionType::Perspective3D)
		{
			if (mSortGeometryFrontToBack)
			{
				// Diagnostic mode is deliberately strict: even transparent commands
				// render closest-first. This makes the toggle describe one unambiguous
				// ordering for every queued piece of 3D geometry.
				stable_sort(renderCommands.begin(), renderCommands.end(), [](SortedRenderCommand const& a, SortedRenderCommand const& b) -> bool
				{
					if (a.viewDistanceSquared != b.viewDistanceSquared)
						return a.viewDistanceSquared < b.viewDistanceSquared;
					return a.key < b.key;
				});
			}
			else
			{
				sort(renderCommands.begin(), renderCommands.end(), [](SortedRenderCommand const& a, SortedRenderCommand const& b) -> bool
				{
					bool const aTransparent = a.meshInstance->sortTransparent();
					bool const bTransparent = b.meshInstance->sortTransparent();
					if (aTransparent != bTransparent)
					{
						return !aTransparent; // Opaque and masked geometry first.
					}
					if (aTransparent)
						return a.viewDistanceSquared > b.viewDistanceSquared;
					return a.key < b.key;
				});
			}
		}

		// Now render all the meshes.
		uint64_t currentProgramKey = 0; // Sort ids start at 1, so this is guaranteed not to be one.
		vector<uint64_t> currentTextureKeys;

		Material* currentMaterial{ nullptr };
		unique_ptr<GpuDebugScope> geometryScope;
		int currentGeometryClass = -1;
		for (auto const& renderCommand: renderCommands)
		{
			auto key = renderCommand.key;
			auto const& cmd = renderCommand.cmd;
			auto meshInstance = renderCommand.meshInstance;
			int const geometryClass = meshInstance->sortTransparent() ? 1 : 0;
			if (geometryClass != currentGeometryClass)
			{
				geometryScope.reset();
				geometryScope = make_unique<GpuDebugScope>(renderFlowGeometryRenderDocLabel(geometryClass != 0));
				currentGeometryClass = geometryClass;
			}
			auto mesh = meshInstance->mwMesh;
			auto instanceCount = meshInstance->mInstanceCount;
			auto numPrimitives = mesh->getNumPrimitives();

			vector<string> flowStateChanges;
			setupRenderMeshInstance(meshInstance, cmd, key, &currentProgramKey, &currentTextureKeys, &currentMaterial,
			                        isRenderFlowCaptureActive() ? &flowStateChanges : nullptr);
			if (!flowStateChanges.empty()) recordRenderFlowStateChanges(std::move(flowStateChanges));

			auto count = cmd.count != ~0u ? cmd.count : numPrimitives;
			if (mFlowCapture && !mFlowCaptureFailed && mCurrentFlowPass.isValid())
			{
				try
				{
					auto material = static_cast<Material*>(cmd.material.get());
					auto program = static_cast<Program*>(material->getProgram().get());
					RenderBatchSubmission submission;submission.sceneObject=meshInstance->mSourceSceneObject;submission.meshName=mesh->getName();submission.materialName=material->getName();submission.programName=program->getName();submission.primitiveType=mesh->mPrimitiveType;submission.offset=cmd.offset;submission.count=static_cast<uint32_t>(count);submission.instanceCount=instanceCount;submission.transparent=meshInstance->sortTransparent();submission.blend=meshInstance->mBlend;submission.cullBackFaces=meshInstance->mCullBackFaces;submission.wireframe=meshInstance->mWireframe;
					for (int textureIndex=0;textureIndex<material->getNumTextures();++textureIndex)
					{
						auto const& samplerName=program->getSamplerName(textureIndex);if((samplerName=="SHADOW_MAP"||samplerName=="POINT_SHADOW_MAP")&&mActiveShadowDepthTarget){submission.textureNames.push_back(samplerName=="POINT_SHADOW_MAP"?"__pipeline_point_shadow_depth_cube__":"__pipeline_shadow_depth__");continue;}auto pipelineTexture=mActivePipelineSamplerOverrides.find(samplerName);if(pipelineTexture!=mActivePipelineSamplerOverrides.end()&&pipelineTexture->second){submission.textureNames.push_back(pipelineTexture->second->getName());continue;}auto texture=textureIndex<(int)cmd.textures.size()&&cmd.textures[textureIndex]?cmd.textures[textureIndex]:material->getTexture(textureIndex);submission.textureNames.push_back(texture?texture->getName():string());
					}
					recordRenderFlowBatch(std::move(submission));
				}
				catch (...)
				{
					mFlowCaptureFailed = true;
				}
			}
			mesh->render(instanceCount, cmd.offset, count);
			mRenderInfo.primitivesRendered += (int)(count * instanceCount);
			if (mesh->mPrimitiveType == mpp::mesh::Primitive::Type::Triangles)
				mRenderInfo.trianglesRendered += (int)(count * instanceCount);
			mRenderInfo.batchCount++;

			teardownRenderMeshInstance(meshInstance);
		}

		mModelInstances->releaseAllObjects();
		mMeshInstances->releaseAllObjects();
	}
	
	/*
	 * Get rendering information about this frame.
	 *
	 */
	RenderInfo const& RenderSystem::getRenderInfo() const
	{
		return mRenderInfo;
	}

	void RenderSystem::addSceneFactory(string const& type, SceneFactory factory)
	{
		if (mSceneFactories.find(type) != mSceneFactories.end())
		{
			string errMsg = "Scene type: " + type + " already registered";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		mSceneFactories[type] = factory;
	}

	ScenePtr RenderSystem::createScene(string const& type)
	{
		auto it = mSceneFactories.find(type);
		if (it == mSceneFactories.end())
		{
			string errMsg = "Unknown scene type: " + type;
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		return it->second(this);
	}

	void RenderSystem::renderScene(ScenePtr scene, CameraPtr camera,
		glm::vec2 const& offset2d, string const& pipelineName,
		ScenePassOverrides const& overrides)
	{
		auto pipeline = getRenderPipeline(pipelineName);
		pipeline->render(scene, camera, offset2d, overrides);
	}

	AuxiliarySceneOutputs RenderSystem::renderAuxiliaryScene(
		ScenePtr scene, CameraPtr hostCamera, string const& pipelineName,
		AuxiliarySceneView const& view)
	{
		return getRenderPipeline(pipelineName)->renderAuxiliaryScene(
			std::move(scene), std::move(hostCamera), view);
	}

	AuxiliarySceneDiagnostics const& RenderSystem::getAuxiliarySceneDiagnostics(
		string const& pipelineName, string const& slot) const
	{
		auto const found = mPipelines.find(pipelineName);
		if (found == mPipelines.end())
			THROW_MPP("RenderPipeline '" + pipelineName + "' not found.", __LINE__, __FILE__, __func__);
		return found->second->getAuxiliarySceneDiagnostics(slot);
	}

	RenderPipelinePtr RenderSystem::getOrCreateRenderPipeline(string const& name)
	{
		return getOrCreateRenderPipeline(name, {});
	}

	RenderPipelinePtr RenderSystem::getOrCreateRenderPipeline(string const& name, RenderPipelineOptions const& options)
	{
		auto it = mPipelines.find(name);
		if (it != mPipelines.end())
		{
			if (it->second->getOptions().mode != options.mode)
			{
				THROW_MPP("RenderPipeline '" + name + "' already exists with different options.", __LINE__, __FILE__, __func__);
			}
			return it->second;
		}

		if(!options.outputs.empty())
		{
			std::set<std::string> names;std::optional<AntiAliasingDefaults> shared;
			for(auto const& output:options.outputs)
			{
				if(output.name.empty()||output.image.empty()||!names.insert(output.name).second)THROW_MPP("RenderPipeline '"+name+"' output names/images must be non-empty and names must be unique.",__LINE__,__FILE__,__func__);
				auto effective=resolveAntiAliasing(mOptions.antiAliasing,output.antiAliasing);if(effective.taa&&options.mode!=RenderPipelineMode::GraphPbrForward&&options.mode!=RenderPipelineMode::GraphLegacyForward&&options.mode!=RenderPipelineMode::XmlGraphPbrForward)THROW_MPP("RenderPipeline '"+name+"' enables TAA on a non-render-graph pipeline path.",__LINE__,__FILE__,__func__);
				if(shared&&(effective.msaa!=shared->msaa||effective.ssaa!=shared->ssaa||effective.taa!=shared->taa))THROW_MPP("RenderPipeline '"+name+"' outputs must use identical effective MSAA, SSAA, and TAA settings.",__LINE__,__FILE__,__func__);if(!shared)shared=effective;
				if(!mCaps.supportsMsaa(antiAliasingSampleCount(effective.msaa)))THROW_MPP("RenderPipeline '"+name+"' output '"+output.name+"' requests unsupported "+antiAliasingSamplesName(effective.msaa)+" MSAA.",__LINE__,__FILE__,__func__);
				auto width=(uint64_t)ssaaDimension((uint32_t)mWindowWidth,effective.ssaa),height=(uint64_t)ssaaDimension((uint32_t)mWindowHeight,effective.ssaa);if(width>(uint64_t)mCaps.maxTextureSize||height>(uint64_t)mCaps.maxTextureSize)THROW_MPP("RenderPipeline '"+name+"' output '"+output.name+"' SSAA dimensions exceed the GPU maximum texture size.",__LINE__,__FILE__,__func__);
			}
		}
		auto pipeline = make_shared<RenderPipeline>(name, this, options);
		mPipelines[name] = pipeline;
		return pipeline;
	}

	RenderPipelinePtr RenderSystem::getRenderPipeline(string const& name)
	{
		auto it = mPipelines.find(name);

		if (it == mPipelines.end())
		{
			string errMsg = "RenderPipeline '" + name + "' not found.";
			THROW_MPP(errMsg, __LINE__, __FILE__, __func__);
		}

		return it->second;
	}

	bool RenderSystem::removeRenderPipeline(string const& name)
	{
		return mPipelines.erase(name) != 0;
	}

	void RenderSystem::startStatsCollection()
	{
		++mFrameSerial;mRenderInfo.clear();
	}

	uint64_t RenderSystem::getFrameSerial() const{return mFrameSerial;}

	/*
	 * Finish rendering.  Must be called before swapping screen.
	 *
	 */
	RenderInfo const& RenderSystem::getCurrentRenderInfo() const
	{
		return mRenderInfo;
	}

	void RenderSystem::setSortGeometryFrontToBack(bool enabled)
	{
		mSortGeometryFrontToBack = enabled;
	}

	bool RenderSystem::sortGeometryFrontToBack() const
	{
		return mSortGeometryFrontToBack;
	}

	RenderInfo const& RenderSystem::finishStatsCollection()
	{
#ifdef MPP_PROFILE_BUILD
		mProfiler->sample();
#endif		
		// Display debug panel?
		if (mShowDebugPanel)
		{
			// Backup stats so we don't include them for the debug panel
			auto ri = mRenderInfo;

			renderDebugPanel();

			mRenderInfo = ri;
		}

		return mRenderInfo;
	}

} 