#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <filesystem>
#include <format>
#include <fstream>
#include <type_traits>
#include <tuple>
#include <vector>

#include "mpp/RenderGraphGpuTests.h"
#include "mpp/Camera.h"
#include "mpp/BoxModelStream.h"
#include "mpp/ComputeProgram.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/RenderGraph.h"
#include "mpp/RenderGraphBuiltInPasses.h"
#include "mpp/RenderGraphExecutor.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphStream.h"
#include "mpp/RenderGraphTargets.h"
#include "mpp/RenderOutputProcessor.h"
#include "mpp/RenderPipelineFlow.h"
#include "mpp/RenderSystem.h"
#include "mpp/Scene.h"
#include "mpp/SceneRuntime.h"
#include "mpp/Model.h"
#include "mpp/IblEnvironmentCache.h"
#include "mpp/RenderTexture.h"
#include "mpp/ProgrammaticTextureStream.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/DefaultShaders.h"
#include "mpp/Program.h"
#include "mpp/Texture.h"
#include "mpp/UniformBuffer.h"
#include "mpp/VertexBuffer.h"
#include "mpp/ResourceManager.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"
#include "mpp/ParticleData.h"
#include "mpp/ParticleSystem.h"

namespace mpp
{
	namespace
	{
		std::array<uint8_t, 4> readFirstPixel(RenderTargetPtr const& target, uint32_t mipLevel = 0)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			if (!texture) return { 0, 0, 0, 0 };
			size_t width = std::max<size_t>(1, texture->getWidth() >> mipLevel);
			size_t height = std::max<size_t>(1, texture->getHeight() >> mipLevel);
			std::vector<uint8_t> pixels(width * height * 4);
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture->getColourAttachmentId(0)));
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D, (GLint)mipLevel, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
			return { pixels[0], pixels[1], pixels[2], pixels[3] };
		}

		float readFirstDepth(RenderTargetPtr const& target)
		{
			auto texture=dynamic_cast<RenderTexture*>(target.get());if(!texture||!texture->getDepthTextureId())return -1.0f;std::vector<float> values(texture->getWidth()*texture->getHeight());GL_CHECK(glBindTexture(GL_TEXTURE_2D,texture->getDepthTextureId()));GL_CHECK(glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,values.data()));GL_CHECK(glBindTexture(GL_TEXTURE_2D,0));return values.empty()?-1.0f:values.front();
		}

		float minimumDepth(RenderTargetPtr const& target)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			if (!texture || !texture->getDepthTextureId()) return -1.0f;
			std::vector<float> values(texture->getWidth() * texture->getHeight());
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture->getDepthTextureId()));
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
			return values.empty() ? -1.0f : *std::min_element(values.begin(), values.end());
		}

		float readFirstCubeDepth(RenderTargetPtr const& target, uint32_t face)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get()); if (!texture || face >= 6) return -1.0f;
			std::vector<float> values(texture->getWidth() * texture->getHeight()); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, texture->getDepthTextureId()));
			GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
			return values.empty() ? -1.0f : values.front();
		}

		float readCubeDepth(RenderTargetPtr const& target, uint32_t face, size_t x, size_t y)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			if (!texture || face >= 6 || x >= texture->getWidth() || y >= texture->getHeight()) return -1.0f;
			std::vector<float> values(texture->getWidth() * texture->getHeight());
			GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, texture->getDepthTextureId()));
			GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT, GL_FLOAT, values.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
			return values[y * texture->getWidth() + x];
		}

		bool nearColour(std::array<uint8_t, 4> const& pixel, std::array<uint8_t, 4> const& expected)
		{
			for (size_t i = 0; i < 4; ++i)
				if (std::abs((int)pixel[i] - (int)expected[i]) > 1) return false;
			return true;
		}

		std::vector<uint8_t> readPixels(RenderTargetPtr const& target)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			if (!texture) return {};
			std::vector<uint8_t> pixels(texture->getWidth() * texture->getHeight() * 4);
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture->getColourAttachmentId(0)));
			GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
			return pixels;
		}

		std::array<uint8_t, 4> readPixel(RenderTargetPtr const& target, size_t x, size_t y)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			auto pixels = readPixels(target);
			if (!texture || x >= texture->getWidth() || y >= texture->getHeight() || pixels.empty()) return { 0, 0, 0, 0 };
			auto const index = (y * texture->getWidth() + x) * 4;
			return { pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3] };
		}

		bool containsVisiblePixel(RenderTargetPtr const& target)
		{
			auto pixels = readPixels(target);
			for (size_t index = 0; index < pixels.size(); index += 4)
			{
				if (pixels[index] || pixels[index + 1] || pixels[index + 2]) return true;
			}
			return false;
		}

		class CameraCulledShadowTestScene : public Scene
		{
		public:
			using Scene::Scene;
			std::vector<SceneModel3dPtr> get3dModelsInView(CameraPtr) override { return {}; }
		};

		class ThrowingAuxiliaryTestScene : public Scene
		{
		public:
			using Scene::Scene;
			std::vector<SceneModel3dPtr> get3dModelsInView(CameraPtr) override
			{
				throw std::runtime_error("injected auxiliary visibility failure");
			}
		};

		uint8_t maximumRed(RenderTargetPtr const& target)
		{
			auto pixels = readPixels(target);
			uint8_t maximum = 0;
			for (size_t index = 0; index < pixels.size(); index += 4)
			{
				maximum = std::max(maximum, pixels[index]);
			}
			return maximum;
		}
	}
	bool runRenderGraphGpuTests(RenderSystem* renderSystem, std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		if (!renderSystem) return fail("RenderSystem is null");
		std::string stage = "reported GPU capabilities";
		try
		{
			{
				auto const& caps = renderSystem->getCaps();
				GLint value = 0;
				GL_CHECK(glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &value));
				if (caps.maxCubeMapTextureSize != value) return fail("reported cubemap texture-size limit does not match OpenGL");
				GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &value));
				if (caps.maxVertexTextureUnits != static_cast<uint32_t>(value)) return fail("reported vertex texture-unit limit does not match OpenGL");
				GL_CHECK(glGetIntegerv(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, &value));
				if (caps.maxGeometryTextureUnits != static_cast<uint32_t>(value)) return fail("reported geometry texture-unit limit does not match OpenGL");
				GL_CHECK(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value));
				if (caps.maxFragmentTextureUnits != static_cast<uint32_t>(value)) return fail("reported fragment texture-unit limit does not match OpenGL");
				GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value));
				if (caps.maxVertexAttributes != static_cast<uint32_t>(value)) return fail("reported vertex-attribute limit does not match OpenGL");
				if (GLEW_VERSION_4_4 || GLEW_ARB_vertex_attrib_binding)
				{
					GL_CHECK(glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &value));
					if (caps.maxVertexAttributeStride != static_cast<uint32_t>(value)) return fail("reported vertex stride limit does not match OpenGL");
				}
				if (GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic)
				{
					GLfloat anisotropy = 1.0f;
					GL_CHECK(glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &anisotropy));
					if (std::abs(caps.maxAnisotropy - anisotropy) > 0.001f) return fail("reported anisotropy limit does not match OpenGL");
				}
				else if (caps.maxAnisotropy != 1.0f) return fail("anisotropy is reported on unsupported hardware");
			}

			stage = "persistent streaming buffers";
			{
				auto uniformBytes = new int8_t[64]{};
				std::shared_ptr<const int8_t> uniformData(uniformBytes, [](int8_t const* value) { delete[] value; });
				UniformBuffer uniform(renderSystem, uniformData, 64, 7);
				uniform.load();
				if (uniform.usesPersistentMapping() != renderSystem->getCaps().streamingGeometry)
					return fail("uniform buffer mapping path disagrees with the reported capability");
				auto& uniformCpu = uniform.getBufferData();
				for (size_t index = 0; index < uniformCpu.size(); ++index) uniformCpu[index] = static_cast<int8_t>(index);
				uniform.mapBufferData();
				uniformCpu[17] = 99;
				uniform.updateData(17, 1);
				GLint uniformName = 0;
				GLint64 uniformStart = 0;
				GL_CHECK(glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 7, &uniformName));
				GL_CHECK(glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 7, &uniformStart));
				if (!uniformName || (uniform.usesPersistentMapping() && uniformStart == 0)) return fail("uniform ring did not bind its active segment range");
				std::array<int8_t, 64> uniformReadback{};
				GL_CHECK(glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(uniformName)));
				GL_CHECK(glGetBufferSubData(GL_UNIFORM_BUFFER, uniformStart, uniformReadback.size(), uniformReadback.data()));
				if (uniformReadback[17] != 99 || uniformReadback[31] != 31) return fail("uniform ring lost complete or partial CPU updates");
				if (uniform.usesPersistentMapping())
				{
					GLint immutable = GL_FALSE;
					GL_CHECK(glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_IMMUTABLE_STORAGE, &immutable));
					if (immutable != GL_TRUE) return fail("reported persistent uniform buffer does not use immutable storage");
				}
				uniform.unload();

				auto vertexBytes = new int8_t[3 * 2 * sizeof(float)]{};
				std::shared_ptr<const int8_t> vertexData(vertexBytes, [](int8_t const* value) { delete[] value; });
				GLuint streamingVao = 0;
				GL_CHECK(glGenVertexArrays(1, &streamingVao));
				GL_CHECK(glBindVertexArray(streamingVao));
				{
					VertexBuffer vertex(renderSystem, mesh::VertexBufferStorageType::Dynamic, 3, 2 * sizeof(float), true, false, vertexData);
					vertex.setAttribute(0, mesh::Vertex::DataType::Float, 2, 0, false);
					vertex.load();
					if (vertex.usesPersistentMapping() != renderSystem->getCaps().streamingGeometry)
						return fail("vertex buffer mapping path disagrees with the reported capability");
					auto& vertexCpu = vertex.getBufferData();
					reinterpret_cast<float*>(vertexCpu.data())[0] = 0.5f;
					vertex.mapBufferData(3);
					vertex.bind();
					if (vertex.usesPersistentMapping())
					{
						GLint immutable = GL_FALSE;
						GL_CHECK(glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_IMMUTABLE_STORAGE, &immutable));
						if (immutable != GL_TRUE) return fail("reported persistent vertex buffer does not use immutable storage");
					}
				}
				GL_CHECK(glBindVertexArray(0));
				GL_CHECK(glDeleteVertexArrays(1, &streamingVao));
			}

			stage = "resource sort-ID stability";
			{
				auto* resources = renderSystem->getResourceManager();
				auto declareTexture = [&](std::string const& name)
				{
					return resources->declareResource(name, std::make_shared<ProgrammaticTextureStream>(resources)).first;
				};
				auto textureA = declareTexture("GpuTestSort.TextureA");
				auto textureB = declareTexture("GpuTestSort.TextureB");
				auto textureC = declareTexture("GpuTestSort.TextureC");
				auto textureAId = static_cast<Texture*>(textureA.get())->getSortId();
				auto textureBId = static_cast<Texture*>(textureB.get())->getSortId();
				auto textureCId = static_cast<Texture*>(textureC.get())->getSortId();
				resources->deleteResource(textureB->getName());
				bool removedTextureRejected = false, outOfRangeTextureRejected = false;
				try { resources->getTextureBySortId(textureBId); } catch (...) { removedTextureRejected = true; }
				try { resources->getTextureBySortId(UINT32_MAX); } catch (...) { outOfRangeTextureRejected = true; }
				if (!removedTextureRejected || !outOfRangeTextureRejected || resources->getTextureBySortId(textureAId) != textureA || resources->getTextureBySortId(textureCId) != textureC)
					return fail("removing a texture shifted or retained its sort-ID slot");
				auto textureD = declareTexture("GpuTestSort.TextureD");
				auto textureDId = static_cast<Texture*>(textureD.get())->getSortId();
				if (textureDId <= textureCId || resources->getTextureBySortId(textureDId) != textureD)
					return fail("a removed texture sort ID was unsafely reused");
				resources->deleteResource(textureA->getName());
				resources->deleteResource(textureC->getName());
				resources->deleteResource(textureD->getName());

				auto makeProgramStream = [&](std::string const& value)
				{
					mesh::MeshSpecification spec;
					auto* layout = spec.createVertexBufferAttributeLayout(false);
					layout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
					layout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
					auto parser = std::make_shared<program::Parser>();
					parser->setMeshSpecification(spec);
					parser->setVertexSource(VertexShaderFullscreenTemplate);
					parser->setFragmentSource("@@Version\nvoid main() { @Out(vec4 COLOUR) = vec4(" + value + ", 0.0, 0.0, 1.0); }");
					auto stream = std::make_shared<ProgrammaticProgramStream>(resources);
					stream->setParser(parser);
					return stream;
				};
				auto programA = resources->declareResource("GpuTestSort.ProgramA", makeProgramStream("0.1")).first;
				auto programB = resources->declareResource("GpuTestSort.ProgramB", makeProgramStream("0.2")).first;
				auto programC = resources->declareResource("GpuTestSort.ProgramC", makeProgramStream("0.3")).first;
				auto programAId = static_cast<Program*>(programA.get())->getSortId();
				auto programBId = static_cast<Program*>(programB.get())->getSortId();
				auto programCId = static_cast<Program*>(programC.get())->getSortId();
				resources->deleteResource(programB->getName());
				bool removedProgramRejected = false;
				try { resources->getProgramBySortId(programBId); } catch (...) { removedProgramRejected = true; }
				if (!removedProgramRejected || resources->getProgramBySortId(programAId) != programA || resources->getProgramBySortId(programCId) != programC)
					return fail("removing a program shifted or retained its sort-ID slot");
				auto alias = resources->declareResource("GpuTestSort.ProgramAlias", makeProgramStream("0.3"));
				if (alias.second || alias.first != programC) return fail("identical program source did not use the program cache");
				resources->deleteResource("GpuTestSort.ProgramAlias");
				if (resources->getProgramBySortId(programCId) != programC) return fail("removing a program alias removed its cached owner");
				resources->deleteResource(programA->getName());
				resources->deleteResource(programC->getName());
				auto replacement = resources->declareResource("GpuTestSort.ProgramReplacement", makeProgramStream("0.3")).first;
				auto replacementId = static_cast<Program*>(replacement.get())->getSortId();
				if (replacement == programC || replacementId <= programCId || resources->getProgramBySortId(replacementId) != replacement)
					return fail("removed program cache entry or sort ID was reused unsafely");
				resources->deleteResource(replacement->getName());
			}

			stage = "cubemap render targets";
			auto cachePath = std::filesystem::temp_directory_path() / "mpp_gpu_ibl_cache_test.exr"; { std::ofstream file(cachePath, std::ios::binary); file.put('\0'); }
			IblEnvironmentCache cache; IblEnvironmentCacheKey cacheKey; cacheKey.source = cachePath; auto cacheResult = std::make_shared<IblEnvironmentResources>(); cache.store(cacheKey, cacheResult); if (cache.find(cacheKey) != cacheResult) return fail("IBL cache did not return stored source generation"); std::error_code cacheTimeError; auto cacheTime = std::filesystem::last_write_time(cachePath, cacheTimeError); std::filesystem::last_write_time(cachePath, cacheTime + std::chrono::seconds(2), cacheTimeError); if (cacheTimeError || cache.find(cacheKey)) return fail("IBL cache did not invalidate changed source timestamp"); cache.store(cacheKey, cacheResult); cache.invalidate(cachePath); if (cache.find(cacheKey)) return fail("IBL cache explicit invalidation failed"); std::filesystem::remove(cachePath, cacheTimeError);
			bool rejectedInvalidIblFormat = false; try { renderSystem->createIblCubemap("GpuTestInvalidIblCubemap", 8, 1, GL_RGBA8); } catch (...) { rejectedInvalidIblFormat = true; } if (!rejectedInvalidIblFormat) return fail("IBL cubemap accepted an LDR format");
			// Seamless cube filtering is global state that setDefaultState turns on
			// once. This asserts it survived startup; it is re-checked after the IBL
			// generation below, because those passes save and restore a pile of GL
			// state and a restore that missed it would silently reintroduce the seams.
			if (!glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS)) return fail("seamless cubemap filtering is not enabled, so every cube edge will show a bilinear seam");
			GLint savedViewport[4]{}, savedScissor[4]{}, savedDraw = 0, savedRead = 0; GL_CHECK(glGetIntegerv(GL_VIEWPORT, savedViewport)); GL_CHECK(glGetIntegerv(GL_SCISSOR_BOX, savedScissor)); GL_CHECK(glGetIntegerv(GL_DRAW_BUFFER, &savedDraw)); GL_CHECK(glGetIntegerv(GL_READ_BUFFER, &savedRead)); auto savedScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
			auto cubemap = renderSystem->createIblCubemap("GpuTestIblCubemap", 8, 2, GL_RGBA16F);
			auto cubeTexture = dynamic_cast<RenderTexture*>(cubemap.get());
			if (!cubeTexture || cubeTexture->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP || cubeTexture->getMipLevels() != 2) return fail("IBL cubemap creation contract failed");
			for (uint32_t face = 0; face < 6; ++face)
			{
				RenderSystem::CubemapFaceRenderScope scope(*renderSystem, cubemap, face, 0);
				GL_CHECK(glClearColor((float)(face + 1), 0.0f, 0.0f, 1.0f));
				GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
				scope.finish();
			}
			GLint restoredViewport[4]{}, restoredScissor[4]{}, restoredDraw = 0, restoredRead = 0; GL_CHECK(glGetIntegerv(GL_VIEWPORT, restoredViewport)); GL_CHECK(glGetIntegerv(GL_SCISSOR_BOX, restoredScissor)); GL_CHECK(glGetIntegerv(GL_DRAW_BUFFER, &restoredDraw)); GL_CHECK(glGetIntegerv(GL_READ_BUFFER, &restoredRead)); if (!std::equal(std::begin(savedViewport), std::end(savedViewport), std::begin(restoredViewport)) || !std::equal(std::begin(savedScissor), std::end(savedScissor), std::begin(restoredScissor)) || savedDraw != restoredDraw || savedRead != restoredRead || savedScissorEnabled != glIsEnabled(GL_SCISSOR_TEST)) return fail("cubemap face scope leaked render state");
			for (uint32_t face = 0; face < 6; ++face)
			{
				std::vector<float> value(8 * 8 * 4);
				GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture->getColourAttachmentId(0)));
				GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, value.data()));
				GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
				if (std::abs(value[0] - (float)(face + 1)) > 0.05f) return fail("cubemap face HDR readback failed");
			}
			{ RenderSystem::CubemapFaceRenderScope scope(*renderSystem, cubemap, 2, 1); GL_CHECK(glClearColor(3.0f, 0.0f, 0.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT)); }
			std::vector<float> mipValue(4 * 4 * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 2, 1, GL_RGBA, GL_FLOAT, mipValue.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); if (std::abs(mipValue[0] - 3.0f) > 0.05f) return fail("cubemap mip HDR readback failed");

			// A linear HDR panorama with values above one verifies the public conversion path.
			auto panoramaStream = new ProgrammaticTextureStream(renderSystem->getResourceManager());
			// Linear filtering matters here: the seam check below compares the two
			// faces' edge texels, whose centres sit a fraction of a source texel
			// apart in longitude. Under the default GL_NEAREST they quantise to
			// different source texels and can never agree.
			panoramaStream->setTarget(TextureTarget::Texture2D); panoramaStream->setColourSpace(TextureColourSpace::Linear); panoramaStream->setInternalFormat(TextureInternalType::Float, false, 32, 3);
			panoramaStream->setFiltering(TextureParams::MinFilter::Linear, TextureParams::MagFilter::Linear);
			panoramaStream->setData([](std::string const&) { TextureData data; data.width = 8; data.height = 4; data.bitsPerPixel = 96; data.pixelFormat = GL_RGB; data.dataType = GL_FLOAT; data.data = new uint8_t[8 * 4 * 3 * sizeof(float)]; auto values = reinterpret_cast<float*>(data.data); for (size_t y = 0; y < 4; ++y) for (size_t x = 0; x < 8; ++x) { auto index = (y * 8 + x) * 3; values[index] = 1.25f + (float)x; values[index + 1] = (float)y; values[index + 2] = 0.5f; } return data; });
			auto panoramaResource = renderSystem->getResourceManager()->declareResource("GpuTestHdrPanorama", ResourceStreamPtr(panoramaStream)).first; panoramaResource->load();
			auto converted = renderSystem->convertEquirectangularToCubemap(dynamic_cast<Texture*>(panoramaResource.get()), "GpuTestConvertedPanorama", 8);
			auto convertedTexture = dynamic_cast<RenderTexture*>(converted.get()); if (!convertedTexture) return fail("equirectangular conversion did not return a cubemap render texture");
			std::array<float, 6> faceCentre{}; float positiveXRight = 0.0f, negativeZLeft = 0.0f;
			for (uint32_t face = 0; face < 6; ++face) { std::vector<float> pixels(8 * 8 * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, convertedTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, pixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); if (pixels[0] <= 1.0f) return fail("equirectangular conversion lost HDR values"); faceCentre[face] = pixels[(4 * 8 + 4) * 4]; if(face == 0) positiveXRight = pixels[(4 * 8 + 7) * 4]; if(face == 5) negativeZLeft = pixels[(4 * 8) * 4]; }
			// The horizontal HDR gradient encodes longitude. At the face centres the
			// documented convention orders -Z (longitude -pi/2), +X (0), +Z (+pi/2).
			if (!(faceCentre[5] < faceCentre[0] && faceCentre[0] < faceCentre[4])) return fail("equirectangular cubemap face orientation is incorrect");
			// The +X and -Z faces meet at longitude -45 degrees, but their outermost
			// texel centres sit at atan(1 - 1/8) = 41.2 degrees either side of it,
			// a 7.6 degree gap. The source encodes longitude at 45 degrees per texel,
			// so a continuous conversion differs by that gap, not by zero. Allow a
			// quarter texel and report the measured values, since a real seam bug and
			// a sampling-geometry mismatch are otherwise indistinguishable.
			auto const seamGap = std::abs(positiveXRight - negativeZLeft);
			if (seamGap > 0.25f) return fail("equirectangular cubemap seam is discontinuous: +X right " + std::to_string(positiveXRight) +
				" vs -Z left " + std::to_string(negativeZLeft) + " (gap " + std::to_string(seamGap) + ")");
			// A requested chain must be populated, not merely allocated: the specular
			// prefilter's solid-angle LOD silently clamps to level zero otherwise.
			// Successive box filtering makes the 1x1 level the mean of level zero, so
			// compare against the source face rather than a hardcoded constant.
			{
				auto chained = renderSystem->convertEquirectangularToCubemap(dynamic_cast<Texture*>(panoramaResource.get()), "GpuTestChainedPanorama", 8, 4);
				auto chainedTexture = dynamic_cast<RenderTexture*>(chained.get());
				if (!chainedTexture || chainedTexture->getMipLevels() != 4) return fail("equirectangular conversion did not create the requested mip chain");
				std::vector<float> base(8 * 8 * 4), smallest(4);
				GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, chainedTexture->getColourAttachmentId(0)));
				GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, GL_FLOAT, base.data()));
				GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 3, GL_RGBA, GL_FLOAT, smallest.data()));
				GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
				float mean = 0.0f; for (size_t texel = 0; texel < 64; ++texel) mean += base[texel * 4];
				mean /= 64.0f;
				if (mean <= 1.0f) return fail("chained equirectangular conversion lost HDR values");
				if (std::abs(smallest[0] - mean) > 0.05f) return fail("equirectangular conversion did not populate its mip chain");
			}
			GLint stateViewport[4]{}, stateAfter[4]{}; GL_CHECK(glGetIntegerv(GL_VIEWPORT, stateViewport)); bool rejectedInvalidSource = false; try { renderSystem->convertEquirectangularToCubemap(nullptr, "GpuTestInvalidPanorama", 8); } catch (...) { rejectedInvalidSource = true; } GL_CHECK(glGetIntegerv(GL_VIEWPORT, stateAfter)); if (!rejectedInvalidSource || !std::equal(std::begin(stateViewport), std::end(stateViewport), std::begin(stateAfter))) return fail("invalid equirectangular source changed render state");

			auto neutralEnvironment = renderSystem->createIblCubemap("GpuTestNeutralEnvironment", 4, 1, GL_RGBA16F); auto neutralTexture = dynamic_cast<RenderTexture*>(neutralEnvironment.get());
			for (uint32_t face = 0; face < 6; ++face) { RenderSystem::CubemapFaceRenderScope scope(*renderSystem, neutralEnvironment, face, 0); GL_CHECK(glClearColor(2.0f, 2.0f, 2.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT)); }
			auto irradiance = renderSystem->generateDiffuseIrradiance(neutralTexture, "GpuTestNeutralIrradiance", 4, 8); auto irradianceTexture = dynamic_cast<RenderTexture*>(irradiance.get()); if (!irradianceTexture) return fail("diffuse irradiance did not return a cubemap render texture");
			for (uint32_t face = 0; face < 6; ++face) { std::vector<float> pixels(4 * 4 * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, pixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); if (std::abs(pixels[0] - 2.0f) > 0.15f || std::abs(pixels[1] - 2.0f) > 0.15f || std::abs(pixels[2] - 2.0f) > 0.15f) return fail("neutral HDR environment did not preserve diffuse irradiance"); }
			auto directionalEnvironment = renderSystem->createIblCubemap("GpuTestDirectionalEnvironment", 4, 1, GL_RGBA16F); auto directionalTexture = dynamic_cast<RenderTexture*>(directionalEnvironment.get());
			for (uint32_t face = 0; face < 6; ++face) { RenderSystem::CubemapFaceRenderScope scope(*renderSystem, directionalEnvironment, face, 0); GL_CHECK(glClearColor(face == 0 ? 8.0f : 0.0f, 0.0f, 0.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT)); }
			auto directionalIrradiance = renderSystem->generateDiffuseIrradiance(directionalTexture, "GpuTestDirectionalIrradiance", 4, 64); auto repeatedIrradiance = renderSystem->generateDiffuseIrradiance(directionalTexture, "GpuTestDirectionalIrradianceRepeat", 4, 64); auto directionalOutput = dynamic_cast<RenderTexture*>(directionalIrradiance.get()); auto repeatedOutput = dynamic_cast<RenderTexture*>(repeatedIrradiance.get()); std::array<float, 2> directionalCentre{};
			for (uint32_t face = 0; face < 2; ++face) { std::vector<float> pixels(4 * 4 * 4), repeated(4 * 4 * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, directionalOutput->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, pixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, repeatedOutput->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, repeated.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); directionalCentre[face] = pixels[(2 * 4 + 2) * 4]; if (std::abs(directionalCentre[face] - repeated[(2 * 4 + 2) * 4]) > 0.001f) return fail("diffuse irradiance generation is not deterministic"); }
			if (!(directionalCentre[0] > directionalCentre[1])) return fail("directional diffuse irradiance is not oriented toward its source");
			// A constant environment cannot tell a cosine-weighted average apart from
			// the correct unweighted mean, which is why the checks above missed a
			// convolution integrating against a cos^2 lobe. Pin it with an oracle that
			// shares no code with the shader: a deterministic uniform-hemisphere
			// quadrature of E/pi over a single lit cube face. The environment is 16x16
			// per face so the bilinear ramp along the lit face's border stays narrow
			// against the oracle's hard-edged cone.
			{
				auto oracleEnvironment = renderSystem->createIblCubemap("GpuTestOracleEnvironment", 16, 1, GL_RGBA16F); auto oracleSource = dynamic_cast<RenderTexture*>(oracleEnvironment.get());
				for (uint32_t face = 0; face < 6; ++face) { RenderSystem::CubemapFaceRenderScope scope(*renderSystem, oracleEnvironment, face, 0); GL_CHECK(glClearColor(face == 0 ? 8.0f : 0.0f, 0.0f, 0.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT)); }
				auto oracleIrradiance = renderSystem->generateDiffuseIrradiance(oracleSource, "GpuTestOracleIrradiance", 4, 512); auto oracleOutput = dynamic_cast<RenderTexture*>(oracleIrradiance.get()); if (!oracleOutput) return fail("oracle diffuse irradiance did not return a cubemap render texture");
				std::vector<float> oraclePixels(4 * 4 * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, oracleOutput->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, GL_FLOAT, oraclePixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
				// Texel (2,2) of a 4x4 +X face: faceDirection maps pixel (2.5/4, 2.5/4)
				// to u = v = 0.25 and the +X face to (1, -v, -u).
				auto const oracleNormal = glm::normalize(glm::vec3(1.0f, -0.25f, -0.25f));
				auto const up = std::abs(oracleNormal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
				auto const tangent = glm::normalize(glm::cross(up, oracleNormal)); auto const bitangent = glm::cross(oracleNormal, tangent);
				uint32_t const cosSteps = 256, phiSteps = 256; double accumulated = 0.0;
				for (uint32_t cosStep = 0; cosStep < cosSteps; ++cosStep) for (uint32_t phiStep = 0; phiStep < phiSteps; ++phiStep)
				{
					auto const cosTheta = ((float)cosStep + 0.5f) / (float)cosSteps, sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
					auto const phi = 6.28318530718f * ((float)phiStep + 0.5f) / (float)phiSteps;
					auto const direction = tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) + oracleNormal * cosTheta;
					// Uniform on the hemisphere, so pdf = 1/2pi and E/pi = 2*mean(L*cos).
					if (direction.x > 0.0f && std::abs(direction.y) <= direction.x && std::abs(direction.z) <= direction.x) accumulated += 8.0 * cosTheta;
				}
				auto const reference = (float)(2.0 * accumulated / (double)(cosSteps * phiSteps));
				// Measured: the corrected convolution lands within 0.7% of the oracle,
				// the cos^2 form 21.7% above it. 6% leaves an order of magnitude of
				// headroom for driver and quadrature differences either way while
				// still failing the bug by a factor of three.
				if (std::abs(oraclePixels[(2 * 4 + 2) * 4] - reference) > 0.06f * reference) return fail("diffuse irradiance does not match the reference convolution (got " + std::to_string(oraclePixels[(2 * 4 + 2) * 4]) + ", expected " + std::to_string(reference) + "); a cosine-weighted average would read ~" + std::to_string(1.22f * reference));
			}
			GLint irradianceViewport[4]{}, irradianceViewportAfter[4]{}; GL_CHECK(glGetIntegerv(GL_VIEWPORT, irradianceViewport)); bool rejectedIrradianceSource = false; try { renderSystem->generateDiffuseIrradiance(nullptr, "GpuTestInvalidIrradiance", 4, 8); } catch (...) { rejectedIrradianceSource = true; } GL_CHECK(glGetIntegerv(GL_VIEWPORT, irradianceViewportAfter)); if (!rejectedIrradianceSource || !std::equal(std::begin(irradianceViewport), std::end(irradianceViewport), std::begin(irradianceViewportAfter))) return fail("invalid diffuse irradiance source changed render state");

			auto prefiltered = renderSystem->generatePrefilteredSpecular(neutralTexture, "GpuTestNeutralPrefilter", 4, 3, 8); auto prefilteredTexture = dynamic_cast<RenderTexture*>(prefiltered.get()); if (!prefilteredTexture || prefilteredTexture->getMipLevels() != 3) return fail("specular prefilter did not create requested mip chain");
			for (uint32_t mip = 0; mip < 3; ++mip) for (uint32_t face = 0; face < 6; ++face) { auto size = std::max<size_t>(1, 4 >> mip); std::vector<float> pixels(size * size * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, prefilteredTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGBA, GL_FLOAT, pixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); if (!std::isfinite(pixels[0]) || pixels[0] <= 1.0f) return fail("specular prefilter left a face/mip invalid or non-HDR"); }
			auto directionalPrefilter = renderSystem->generatePrefilteredSpecular(directionalTexture, "GpuTestDirectionalPrefilter", 4, 3, 64); auto repeatedPrefilter = renderSystem->generatePrefilteredSpecular(directionalTexture, "GpuTestDirectionalPrefilterRepeat", 4, 3, 64); auto directionalPrefilterTexture = dynamic_cast<RenderTexture*>(directionalPrefilter.get()); auto repeatedPrefilterTexture = dynamic_cast<RenderTexture*>(repeatedPrefilter.get()); float sharpPeak = 0.0f, roughPeak = 0.0f, sharpRepeat = 0.0f;
			for (uint32_t mip = 0; mip < 3; ++mip) { auto size = std::max<size_t>(1, 4 >> mip); std::vector<float> pixels(size * size * 4), repeated(size * size * 4); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, directionalPrefilterTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, mip, GL_RGBA, GL_FLOAT, pixels.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, repeatedPrefilterTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, mip, GL_RGBA, GL_FLOAT, repeated.data())); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); float peak = 0.0f; for (size_t index = 0; index < pixels.size(); index += 4) peak = std::max(peak, pixels[index]); if (mip == 0) { sharpPeak = peak; sharpRepeat = repeated[0]; if (std::abs(pixels[0] - sharpRepeat) > 0.001f) return fail("specular prefilter generation is not deterministic"); } if (mip == 2) roughPeak = peak; }
			if (!(sharpPeak > roughPeak)) return fail("specular prefilter roughness mip did not broaden directional reflection");
			GLint prefilterViewport[4]{}, prefilterViewportAfter[4]{}; GL_CHECK(glGetIntegerv(GL_VIEWPORT, prefilterViewport)); bool rejectedPrefilterSource = false; try { renderSystem->generatePrefilteredSpecular(nullptr, "GpuTestInvalidPrefilter", 4, 3, 8); } catch (...) { rejectedPrefilterSource = true; } GL_CHECK(glGetIntegerv(GL_VIEWPORT, prefilterViewportAfter)); if (!rejectedPrefilterSource || !std::equal(std::begin(prefilterViewport), std::end(prefilterViewport), std::begin(prefilterViewportAfter))) return fail("invalid specular prefilter source changed render state");

			GLint lutViewport[4]{}, lutViewportAfter[4]{}; GL_CHECK(glGetIntegerv(GL_VIEWPORT, lutViewport)); auto brdfLut = renderSystem->getOrCreatePbrBrdfIntegrationLut(); auto repeatedBrdfLut = renderSystem->getOrCreatePbrBrdfIntegrationLut(); GL_CHECK(glGetIntegerv(GL_VIEWPORT, lutViewportAfter)); auto lutTexture = dynamic_cast<RenderTexture*>(brdfLut.get()); if (!lutTexture || brdfLut != repeatedBrdfLut || lutTexture->getWidth() != 512 || lutTexture->getHeight() != 512 || !std::equal(std::begin(lutViewport), std::end(lutViewport), std::begin(lutViewportAfter))) return fail("BRDF integration LUT cache or state contract failed"); std::vector<float> lutPixels(512 * 512 * 2); GL_CHECK(glBindTexture(GL_TEXTURE_2D, lutTexture->getColourAttachmentId(0))); GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_FLOAT, lutPixels.data())); GLint lutWrapS = 0, lutWrapT = 0; GL_CHECK(glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &lutWrapS)); GL_CHECK(glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &lutWrapT)); GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0)); for (auto value : lutPixels) if (!std::isfinite(value) || value < 0.0f) return fail("BRDF integration LUT contains invalid values");
			// A quad that never rasterizes leaves a plausible-looking all-zero LUT that
			// the finite/non-negative sweep above happily accepts, so pin the content
			// analytically. At roughness 0 the GGX lobe collapses onto the normal and
			// the scale term is exactly the Schlick complement 1-(1-nDotV)^5.
			float const mirrorScaleAtGrazing = lutPixels[0], mirrorScaleAtNormal = lutPixels[511 * 2]; if (std::abs(mirrorScaleAtNormal - 1.0f) > 0.02f || mirrorScaleAtGrazing > 0.02f) return fail("BRDF integration LUT is not the split-sum integral at roughness 0 (scale " + std::to_string(mirrorScaleAtGrazing) + " at grazing, " + std::to_string(mirrorScaleAtNormal) + " head-on, expected ~0 and ~1)");
			// The shader reaches u=1 (nDotV=1) on every camera-facing surface, so a
			// repeating sampler would blend that head-on texel into the grazing one.
			// Measure how far apart the two columns really are before demanding the
			// clamp, so the assertion carries its own justification.
			float widestEdgeGap = 0.0f; for (uint32_t row = 0; row < 512; ++row) widestEdgeGap = std::max(widestEdgeGap, std::abs(lutPixels[row * 512 * 2] - lutPixels[(row * 512 + 511) * 2])); if (widestEdgeGap < 0.25f) return fail("BRDF integration LUT edge columns are unexpectedly similar (" + std::to_string(widestEdgeGap) + "), so the wrap-mode check proves nothing"); if (lutWrapS != GL_CLAMP_TO_EDGE || lutWrapT != GL_CLAMP_TO_EDGE) return fail("BRDF integration LUT is not clamped (wrap s/t " + std::to_string(lutWrapS) + "/" + std::to_string(lutWrapT) + "), so sampling at nDotV=1 wraps into the grazing-incidence column");
			if (!glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS)) return fail("IBL generation left seamless cubemap filtering disabled");
			// The neutral fallback stands in when no BRDF LUT is available. Its .rg is
			// read as (scale, bias), so it has to be (1, 0): white would add a full
			// unit of unconditional specular, black would delete the term.
			{
				auto fallback = renderSystem->getResourceManager()->getResource("__mpp_tex_pbr_brdf_lut__", true);
				auto fallbackTexture = dynamic_cast<Texture*>(fallback.get());
				if (!fallbackTexture) return fail("the neutral BRDF LUT fallback texture is missing");
				fallbackTexture->bind(0);
				std::array<uint8_t, 3> neutral{};
				GL_CHECK(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, neutral.data()));
				GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
				if (neutral[0] != 255 || neutral[1] != 0)
					return fail("the neutral BRDF LUT fallback is not (scale 1, bias 0) but (" + std::to_string(neutral[0]) + ", " + std::to_string(neutral[1]) + ")");
			}

			stage = "initial colour passes";
			GraphImageDesc colour;
			colour.format = GraphImageFormat::Rgba8;
			colour.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
			RenderGraph graph;
			auto first = graph.createImage("GpuTestFirst", colour);
			auto absoluteColour=colour;absoluteColour.absoluteSize={17,19};auto second = graph.createImage("GpuTestSecond", absoluteColour);
			auto firstPass = graph.addPass("GpuTestClear", GraphPassType::Fullscreen);
			first = graph.writeColour(firstPass, first, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(1, 0, 0, 0.25f));
			auto secondPass = graph.addPass("GpuTestChain", GraphPassType::Fullscreen);
			graph.readSampled(secondPass, first);
			second = graph.writeColour(secondPass, second, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 1, 0, 1));

			RenderGraphTargets targets(renderSystem);
			auto plan = graph.buildAllocationPlan({ 64, 48 });bool absolutePreserved=false;for(auto const& lifetime:plan.allocatedImages)if(lifetime.image.id==second.id&&lifetime.size==glm::uvec2(17,19))absolutePreserved=true;if(!absolutePreserved)return fail("absolute graph image dimensions followed the viewport");
			targets.allocate(plan);
			auto firstTarget = targets.get(first);
			if (!firstTarget || firstTarget->getWidth() != 64 || firstTarget->getHeight() != 48) return fail("initial graph target dimensions are wrong");
			std::weak_ptr<RenderTarget> releasedTarget = firstTarget;
			firstTarget.reset();
			RenderGraphExecutor executor(renderSystem);
			executor.setPassCallback(graph, firstPass, [](RenderGraphExecutionContext const&) {});
			executor.setPassCallback(graph, secondPass, [](RenderGraphExecutionContext const&) {});
			executor.execute(graph, targets, renderSystem->getCaps());
			if (!nearColour(readFirstPixel(targets.get(first)), { 255, 0, 0, 64 })) return fail("first pass clear colour/alpha readback failed");
			if (!nearColour(readFirstPixel(targets.get(second)), { 0, 255, 0, 255 })) return fail("second pass clear colour readback failed");

			auto resized = graph.buildAllocationPlan({ 37, 29 });
			targets.allocate(resized);
			auto resizedTarget = targets.get(first);
			if (!resizedTarget || resizedTarget->getWidth() != 37 || resizedTarget->getHeight() != 29) return fail("resized graph target dimensions are wrong");
			auto retainedTarget=resizedTarget;bool invalidPlanRejected=false;try{RenderGraphAllocationPlan invalidPlan;targets.allocate(invalidPlan);}catch(...){invalidPlanRejected=true;}if(!invalidPlanRejected||targets.get(first)!=retainedTarget)return fail("failed graph allocation did not retain the prior generation");
			resizedTarget.reset();retainedTarget.reset();

			stage = "depth cubemap graph faces";
			GraphImageDesc cubeDepth; cubeDepth.format = GraphImageFormat::Depth24; cubeDepth.shape = GraphImageShape::CubeMap; cubeDepth.absoluteSize = { 16, 16 };
			cubeDepth.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled; cubeDepth.depthCompare = true; cubeDepth.transient = false;
			RenderGraph cubeGraph; auto cubeImage = cubeGraph.createImage("GpuTestDepthCube", cubeDepth); std::vector<GraphImageHandle> cubeVersions; std::vector<GraphPassHandle> cubePasses;
			for (uint32_t face = 0; face < 6; ++face) { auto pass = cubeGraph.addPass("GpuTestDepthFace" + std::to_string(face)); cubePasses.push_back(pass); cubeImage = cubeGraph.writeDepth(pass, cubeImage, face == 0 ? GraphLoadOp::Clear : GraphLoadOp::Load, GraphStoreOp::Store, 0.1f * (face + 1), 0, face); cubeVersions.push_back(cubeImage); }
			// The final version alone must retain every prior face write. Point-shadow
			// consumers sample one completed cubemap, not six historical versions.
			auto retain = cubeGraph.addPass("GpuTestRetainDepthFaces"); cubeGraph.readSampled(retain, cubeVersions.back()); auto retained = cubeGraph.createImage("GpuTestCubeRetention", colour); cubeGraph.writeColour(retain, retained, GraphLoadOp::Clear);
			auto cubePlan = cubeGraph.buildAllocationPlan({ 16, 16 }); if (!cubePlan.valid) return fail("depth cubemap allocation plan was rejected");
			RenderGraphTargets cubeTargets(renderSystem); cubeTargets.allocate(cubePlan); auto cubeTarget = cubeTargets.get(cubeVersions.back()); auto depthCubeTexture = dynamic_cast<RenderTexture*>(cubeTarget.get());
			if (!depthCubeTexture || depthCubeTexture->getAttachmentTextureTarget() != GL_TEXTURE_CUBE_MAP || depthCubeTexture->getDepthFormat() != RenderTextureDepthFormat::Depth24 || !depthCubeTexture->usesDepthComparison()) return fail("allocated depth cubemap contract is incomplete");
			for (auto version : cubeVersions) if (cubeTargets.get(version) != cubeTarget) return fail("one logical cubemap's face writes did not share backing storage");
			GLint compareMode = GL_NONE; GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, depthCubeTexture->getDepthTextureId())); GL_CHECK(glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, &compareMode)); GL_CHECK(glBindTexture(GL_TEXTURE_CUBE_MAP, 0)); if (compareMode != GL_COMPARE_REF_TO_TEXTURE) return fail("depth cubemap comparison sampling was not enabled");
			GLchar objectLabel[256]{}; GLsizei objectLabelLength = 0; GL_CHECK(glGetObjectLabel(GL_TEXTURE, depthCubeTexture->getDepthTextureId(), sizeof(objectLabel), &objectLabelLength, objectLabel)); if (!objectLabelLength) return fail("depth cubemap texture was not labelled");
			{ RenderGraphExecutor cubeExecutor(renderSystem); for (uint32_t face = 0; face < cubePasses.size(); ++face) cubeExecutor.setPassCallback(cubeGraph, cubePasses[face], [face](RenderGraphExecutionContext const&) { float value = 0.1f * (face + 1); GL_CHECK(glClearBufferfv(GL_DEPTH, 0, &value)); }); cubeExecutor.setPassCallback(cubeGraph, retain, [](RenderGraphExecutionContext const&) {}); cubeExecutor.execute(cubeGraph, cubeTargets, renderSystem->getCaps()); }
			for (uint32_t face = 0; face < 6; ++face) { auto value = readFirstCubeDepth(cubeTarget, face); auto expected = 0.1f * (face + 1); if (std::abs(value - expected) > 0.002f) return fail("depth cubemap face " + std::to_string(face) + " readback was " + std::to_string(value)); }
			bool rejectedCubeMsaa = false; for (uint32_t samples : { 2u, 4u, 8u }) if (renderSystem->getCaps().supportsMsaa(samples)) { try { cubeTargets.allocatePhysical(cubePlan, samples); } catch (...) { rejectedCubeMsaa = true; } break; } if (!rejectedCubeMsaa) return fail("multisampled depth cubemap allocation was accepted");
			GraphImageDesc importedCubeDesc = cubeDepth; importedCubeDesc.external = true; RenderGraph importedCubeGraph; auto importedCube = importedCubeGraph.createImage("ImportedDepthCube", importedCubeDesc); RenderGraphTargets importedCubeTargets(renderSystem); importedCubeTargets.bindImported(importedCubeGraph, importedCube, cubeTarget);
			bool rejectedMismatchedImport = false; try { importedCubeTargets.bindImported(importedCubeGraph, importedCube, targets.get(second)); } catch (...) { rejectedMismatchedImport = true; } if (!rejectedMismatchedImport) return fail("mismatched 2D import was accepted for a depth cubemap");
			std::weak_ptr<RenderTarget> releasedCube = cubeTarget; importedCubeTargets.clear(); cubeTargets.clear(); cubeTarget.reset(); if (!releasedCube.expired()) return fail("destroyed depth cubemap remained owned by graph targets");

			stage = "point shadow quality options";
			ShadowOptions pointQuality;
			pointQuality.enabled = true;
			pointQuality.resolution = 16;
			pointQuality.light.type = ShadowLightType::Point;
			pointQuality.light.position = { 1.0f, 2.0f, 3.0f };
			pointQuality.light.range = 24.0f;
			pointQuality.nearPlane = 0.25f;
			pointQuality.constantBias = 0.001f;
			pointQuality.normalBias = 0.003f;
			pointQuality.filterMode = ShadowFilterMode::Hard;
			pointQuality.filterRadiusTexels = 2.0f;
			pointQuality.fadeStartNormalized = 0.75f;
			renderSystem->configureShadowDomain("GpuTestPointQuality", pointQuality);
			auto const& appliedPointQuality = renderSystem->getShadowDomainOptions("GpuTestPointQuality");
			if (appliedPointQuality.nearPlane != pointQuality.nearPlane || appliedPointQuality.light.range != pointQuality.light.range ||
				appliedPointQuality.constantBias != pointQuality.constantBias || appliedPointQuality.normalBias != pointQuality.normalBias ||
				appliedPointQuality.filterMode != ShadowFilterMode::Hard || appliedPointQuality.filterRadiusTexels != pointQuality.filterRadiusTexels ||
				appliedPointQuality.fadeStartNormalized != pointQuality.fadeStartNormalized)
				return fail("point shadow quality options were not retained by the public domain API");
			bool rejectedInvalidPointQuality = false;
			try { auto invalid = pointQuality; invalid.fadeStartNormalized = 1.01f; renderSystem->configureShadowDomain("GpuTestInvalidPointQuality", invalid); }
			catch (...) { rejectedInvalidPointQuality = true; }
			if (!rejectedInvalidPointQuality) return fail("point shadow accepted an out-of-range normalized fade start");
			rejectedInvalidPointQuality = false;
			try { auto invalid = pointQuality; invalid.nearPlane = 0.0f; renderSystem->configureShadowDomain("GpuTestInvalidPointNear", invalid); }
			catch (...) { rejectedInvalidPointQuality = true; }
			if (!rejectedInvalidPointQuality) return fail("point shadow accepted a non-positive near plane");

			stage = "authored point-shadow scene runtime";
			SceneDocument authoredPointScene;
			authoredPointScene.name = "GPU authored point shadow";
			authoredPointScene.models.push_back({ "Caster", SceneModelSource::Box });
			SceneLightDocument authoredFill; authoredFill.id = "Fill";
			SceneLightDocument authoredPoint; authoredPoint.id = "AuthoredPoint"; authoredPoint.type = SceneLightType::Point;
			authoredPoint.position = { 3.0f, 4.0f, 5.0f }; authoredPoint.range = 24.0f; authoredPoint.castsShadows = true;
			SceneLightDocument authoredRim; authoredRim.id = "Rim";
			authoredPointScene.lights = { authoredFill, authoredPoint, authoredRim };
			SceneRuntime authoredRuntime(renderSystem, renderSystem->getResourceManager());
			if (!authoredRuntime.rebuild(authoredPointScene, {}, {}, {}, "GpuTestAuthoredPoint"))
				return fail("a valid authored point-shadow scene did not instantiate");
			auto const& authoredShadow = renderSystem->getShadowDomainOptions("GpuTestAuthoredPoint");
			if (authoredShadow.light.type != ShadowLightType::Point || authoredShadow.light.position != authoredPointScene.lights[1].position ||
				authoredShadow.light.range != authoredPointScene.lights[1].range || authoredShadow.light.lightIndex != 1)
				return fail("scene runtime did not configure the authored point shadow or its independent direct-light index");
			auto const previousAuthoredScene = authoredRuntime.getScene();
			auto invalidAuthoredPointScene = authoredPointScene;
			invalidAuthoredPointScene.lights[1].position.x = std::numeric_limits<float>::infinity();
			if (authoredRuntime.rebuild(invalidAuthoredPointScene, {}, {}, {}, "GpuTestAuthoredPoint") || authoredRuntime.getScene() != previousAuthoredScene)
				return fail("an invalid point shadow replaced the previous valid runtime scene");

			stage = "point shadow domain cache";
			std::vector<SceneModel3dPtr> noCasters;
			if (!renderSystem->prepareShadowDomain("GpuTestPointQuality", noCasters)) return fail("new point-shadow revision was reported reusable");
			renderSystem->renderShadowDomain("GpuTestPointQuality", noCasters);
			auto initialCache = renderSystem->getShadowDomainDiagnostics("GpuTestPointQuality");
			if (!initialCache.cacheComplete || initialCache.facePassCount != 6 || initialCache.regenerationCount != 1 ||
				initialCache.renderedRevision != initialCache.revision || initialCache.invalidationReason != ShadowInvalidationReason::InitialConfiguration)
				return fail("initial point-shadow cubemap was not published as one six-face cache unit");
			if (renderSystem->prepareShadowDomain("GpuTestPointQuality", noCasters) ||
				renderSystem->prepareShadowDomain("GpuTestPointQuality", noCasters))
				return fail("stable point-shadow revision was not shared across pipeline requests");
			auto reusedCache = renderSystem->getShadowDomainDiagnostics("GpuTestPointQuality");
			if (reusedCache.reuseCount < 2 || !reusedCache.reusedLastRequest || reusedCache.facePassCount != 6)
				return fail("point-shadow reuse diagnostics did not report stationary/shared reuse");
			auto movedPoint = pointQuality; movedPoint.light.position.x += 1.0f;
			renderSystem->configureShadowDomain("GpuTestPointQuality", movedPoint);
			if (!renderSystem->prepareShadowDomain("GpuTestPointQuality", noCasters)) return fail("point-light movement reused a stale cubemap");
			renderSystem->renderShadowDomain("GpuTestPointQuality", noCasters);
			auto movedCache = renderSystem->getShadowDomainDiagnostics("GpuTestPointQuality");
			if (movedCache.facePassCount != 12 || movedCache.invalidationReason != ShadowInvalidationReason::LightChanged)
				return fail("point-light movement did not regenerate all six faces with diagnostics");
			auto changedQuality = movedPoint; changedQuality.normalBias += 0.001f;
			renderSystem->configureShadowDomain("GpuTestPointQuality", changedQuality);
			renderSystem->renderShadowDomain("GpuTestPointQuality", noCasters);
			auto optionCache = renderSystem->getShadowDomainDiagnostics("GpuTestPointQuality");
			if (optionCache.facePassCount != 18 || optionCache.invalidationReason != ShadowInvalidationReason::OptionsChanged)
				return fail("shadow-relevant option change did not regenerate all six faces");
			renderSystem->invalidateShadowDomain("GpuTestPointQuality");
			renderSystem->renderShadowDomain("GpuTestPointQuality", noCasters);
			auto explicitCache = renderSystem->getShadowDomainDiagnostics("GpuTestPointQuality");
			if (explicitCache.facePassCount != 24 || explicitCache.invalidationReason != ShadowInvalidationReason::Explicit)
				return fail("explicit named-domain invalidation did not regenerate all six faces");

			stage = "point shadow volume selection and automatic invalidation";
			// The shadow pass resolves a material per caster mesh and reads its caster
			// contract, so the casters have to be real models with real materials. A
			// core mesh built against a bare program name is not a usable stand-in:
			// its "material" resolves to a Program and the pass walks into it.
			// A batched or otherwise dynamic model declines the load-time position
			// scan, so its extents are a placeholder at its own local origin rather
			// than a measurement. Volume selection has to read that as unbounded:
			// culling against the placeholder selects by the distance from the light
			// to the model's origin, which drops a world-sized caster entirely as
			// soon as the light moves further than the range away from that origin.
			mesh::MeshSpecification casterSpec(mesh::Primitive::Type::Triangles);
			auto casterLayout = casterSpec.createVertexBufferAttributeLayout(false);
			casterLayout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
			casterLayout->createAttribute(mesh::Vertex::Component::Normal3, mesh::Vertex::DataType::Float, false);
			casterLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
			casterLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);
			auto casterParser = std::make_shared<program::Parser>();
			casterParser->setMeshSpecification(casterSpec);
			casterParser->setVertexSource(VertexShader3dTemplate);
			casterParser->setFragmentSource(R"(
@@Version
@@Uniform(vec4 GPU_TEST_COLOUR);
void main()
{
    @Out(vec4 COLOUR) = @Uniform(GPU_TEST_COLOUR);
}
)");
			auto casterProgramStream = std::make_shared<ProgrammaticProgramStream>(renderSystem->getResourceManager());
			casterProgramStream->setParser(casterParser);
			auto casterProgram = renderSystem->getResourceManager()->declareResource("GpuTestVolumeCaster.Program", casterProgramStream).first;
			casterProgram->load();
			auto casterMaterialStream = std::make_shared<ProgrammaticBasicMaterialStream>(renderSystem->getResourceManager());
			casterMaterialStream->setProgram(casterProgram->getName());
			casterMaterialStream->setUniform("GPU_TEST_COLOUR", glm::vec4(1.0f));
			auto casterMaterial = renderSystem->getResourceManager()->declareResource("GpuTestVolumeCaster.Material", casterMaterialStream).first;
			casterMaterial->load();
			auto measuredStream = std::make_shared<BoxModelStream>(renderSystem->getResourceManager(), casterSpec, casterMaterial->getName(), 1.0f, 1.0f, 1.0f);
			auto measuredModelResource = renderSystem->getResourceManager()->declareResource("GpuTestMeasuredCaster.Model", measuredStream).first;
			measuredModelResource->load();
			auto unmeasuredStream = std::make_shared<BoxModelStream>(renderSystem->getResourceManager(), casterSpec, casterMaterial->getName(), 1.0f, 1.0f, 1.0f);
			unmeasuredStream->setCalculateBounds(false);
			auto unmeasuredModelResource = renderSystem->getResourceManager()->declareResource("GpuTestUnmeasuredCaster.Model", unmeasuredStream).first;
			unmeasuredModelResource->load();
			auto* measuredModel = dynamic_cast<Model*>(measuredModelResource.get());
			auto const* unmeasuredModel = dynamic_cast<Model const*>(unmeasuredModelResource.get());
			if (!measuredModel || !unmeasuredModel || !measuredModel->hasBounds() || unmeasuredModel->hasBounds())
				return fail("a model that declined the bounds calculation did not report unmeasured extents");

			CameraCulledShadowTestScene casterScene(renderSystem);
			auto inRangeCaster = casterScene.add3dModel(measuredModelResource);
			auto outOfRangeCaster = casterScene.add3dModel(measuredModelResource);
			outOfRangeCaster->translate({ 100.0f, 0.0f, 0.0f });
			if (!casterScene.get3dModelsInView({}).empty()) return fail("camera-culling test scene unexpectedly returned visible models");
			auto volumeCasters = casterScene.get3dModelsInSphere({}, 10.0f);
			if (volumeCasters.size() != 1 || volumeCasters.front() != inRangeCaster)
				return fail("point-light volume did not retain the off-camera intersecting caster and exclude the out-of-range model");

			auto measuredDistantCaster = casterScene.add3dModel(measuredModelResource);
			measuredDistantCaster->translate({ 100.0f, 0.0f, 0.0f });
			auto unmeasuredDistantCaster = casterScene.add3dModel(unmeasuredModelResource);
			unmeasuredDistantCaster->translate({ 100.0f, 0.0f, 0.0f });
			auto unmeasuredSelection = casterScene.get3dModelsInSphere({}, 10.0f);
			if (std::find(unmeasuredSelection.begin(), unmeasuredSelection.end(), measuredDistantCaster) != unmeasuredSelection.end())
				return fail("point-light volume retained a measured caster lying outside the light range");
			if (std::find(unmeasuredSelection.begin(), unmeasuredSelection.end(), unmeasuredDistantCaster) == unmeasuredSelection.end())
				return fail("point-light volume culled a caster whose model never measured its bounds");
			casterScene.remove3dModel(measuredDistantCaster);
			casterScene.remove3dModel(unmeasuredDistantCaster);
			auto casterOptions = changedQuality; casterOptions.light.position = {}; casterOptions.light.range = 10.0f;
			renderSystem->configureShadowDomain("GpuTestVolumeCasters", casterOptions);
			renderSystem->renderShadowDomain("GpuTestVolumeCasters", volumeCasters);
			auto verifySceneInvalidation = [&](char const* route)
			{
				if (!renderSystem->prepareShadowDomain("GpuTestVolumeCasters", volumeCasters)) return false;
				renderSystem->renderShadowDomain("GpuTestVolumeCasters", volumeCasters);
				auto diagnostic = renderSystem->getShadowDomainDiagnostics("GpuTestVolumeCasters");
				if (diagnostic.invalidationReason != ShadowInvalidationReason::SceneChanged || diagnostic.facePassCount % 6 != 0)
				{
					if (failure) *failure = std::string(route) + " did not invalidate the complete point-shadow cubemap";
					return false;
				}
				return true;
			};
			inRangeCaster->translate({ 0.25f, 0.0f, 0.0f });
			if (!verifySceneInvalidation("model transform")) return false;
			inRangeCaster->getParams()->setModelFlags(0);
			if (!verifySceneInvalidation("visibility")) return false;
			inRangeCaster->getParams()->setModelFlags(ModelRenderParams::Flag_Visible);
			if (!verifySceneInvalidation("caster policy")) return false;
			inRangeCaster->getParams()->setModelInstanceCount(2);
			if (!verifySceneInvalidation("instance count")) return false;
			auto* firstMesh = measuredModel->getMesh(0);
			inRangeCaster->getParams()->setModelMaterial(firstMesh->getMaterial());
			if (!verifySceneInvalidation("material shadow contract")) return false;

			stage = "point shadow hardware fallback";
			ShadowOptions unsupportedPointShadow;
			unsupportedPointShadow.enabled = true;
			unsupportedPointShadow.light.type = ShadowLightType::Point;
			unsupportedPointShadow.light.range = 10.0f;
			unsupportedPointShadow.nearPlane = 0.1f;
			unsupportedPointShadow.resolution = (size_t)renderSystem->getCaps().maxCubeMapTextureSize + 1;
			renderSystem->configureShadowDomain("GpuTestUnsupportedPointShadow", unsupportedPointShadow);
			if (renderSystem->getShadowDomainDepthTarget("GpuTestUnsupportedPointShadow") ||
				renderSystem->getShadowDomainOptions("GpuTestUnsupportedPointShadow").enabled)
				return fail("unsupported point-shadow cubemap did not disable its domain");

			stage="physical MSAA colour/depth allocation and resolve";
			for(uint32_t samples:{2u,4u,8u})if(renderSystem->getCaps().supportsMsaa(samples)){targets.allocatePhysical(plan,samples);auto write=dynamic_cast<RenderTexture*>(targets.getWriteTarget(first).get());auto resolved=dynamic_cast<RenderTexture*>(targets.get(first).get());if(!write||!resolved||write==resolved||write->getSamples()!=samples||resolved->getSamples()!=1)return fail("MSAA colour write/resolve targets are invalid");executor.execute(graph,targets,renderSystem->getCaps());if(!nearColour(readFirstPixel(targets.get(first)),{255,0,0,64}))return fail("MSAA colour/alpha resolve readback failed");}
			GraphImageDesc depthDesc;depthDesc.format=GraphImageFormat::Depth24;depthDesc.usage=GraphImageUsage::DepthAttachment|GraphImageUsage::Sampled;RenderGraph depthGraph;auto depthImage=depthGraph.createImage("GpuTestMsaaDepth",depthDesc);auto depthPass=depthGraph.addPass("GpuTestMsaaDepthClear",GraphPassType::Fullscreen);depthImage=depthGraph.writeDepth(depthPass,depthImage,GraphLoadOp::Clear,GraphStoreOp::Store,0.25f);RenderGraphExecutor depthExecutor(renderSystem);depthExecutor.setPassCallback(depthGraph, depthPass,[](RenderGraphExecutionContext const&){});auto depthPlan=depthGraph.buildAllocationPlan({32,24});for(uint32_t samples:{2u,4u,8u})if(renderSystem->getCaps().supportsMsaa(samples)){targets.allocatePhysical(depthPlan,samples);auto write=dynamic_cast<RenderTexture*>(targets.getWriteTarget(depthImage).get());auto resolved=dynamic_cast<RenderTexture*>(targets.get(depthImage).get());if(!write||!resolved||write->getSamples()!=samples||resolved->getSamples()!=1)return fail("MSAA depth write/resolve targets are invalid");depthExecutor.execute(depthGraph,targets,renderSystem->getCaps());auto value=readFirstDepth(targets.get(depthImage));if(value<0.24f||value>0.26f)return fail("MSAA depth resolve readback failed: "+std::to_string(value));}

			stage = "curated format allocation";
			std::vector<GraphImageFormat> const supportedFormats{
				GraphImageFormat::R8, GraphImageFormat::Rg8, GraphImageFormat::Rgba8, GraphImageFormat::Srgb8Alpha8,
				GraphImageFormat::R16f, GraphImageFormat::Rg16f, GraphImageFormat::Rgba16f,
				GraphImageFormat::R32f, GraphImageFormat::Rg32f, GraphImageFormat::Rgba32f,
				GraphImageFormat::R11g11b10f, GraphImageFormat::Rgb10a2,
				GraphImageFormat::Depth16, GraphImageFormat::Depth24, GraphImageFormat::Depth32f,
				GraphImageFormat::Depth24Stencil8, GraphImageFormat::Depth32fStencil8
			};
			for (size_t formatIndex = 0; formatIndex < supportedFormats.size(); ++formatIndex)
			{
				stage = "curated format allocation index " + std::to_string(formatIndex);
				auto const format = supportedFormats[formatIndex];
				bool const depthFormat = format >= GraphImageFormat::Depth16;
				GraphImageDesc formatDesc;
				formatDesc.format = format;
				formatDesc.usage = depthFormat ? GraphImageUsage::DepthAttachment : GraphImageUsage::ColourAttachment;
				if (format == GraphImageFormat::Srgb8Alpha8) formatDesc.colourSpace = TextureColourSpace::Srgb;
				RenderGraph formatGraph;
				auto image = formatGraph.createImage("GpuFormat" + std::to_string(formatIndex), formatDesc);
				auto pass = formatGraph.addPass("GpuFormatPass" + std::to_string(formatIndex), GraphPassType::Fullscreen);
				image = depthFormat ? formatGraph.writeDepth(pass, image, GraphLoadOp::Clear) : formatGraph.writeColour(pass, image, GraphLoadOp::Clear);
				RenderGraphTargets formatTargets(renderSystem);
				formatTargets.allocate(formatGraph.buildAllocationPlan({ 8, 8 }));
				RenderGraphExecutor formatExecutor(renderSystem);
				formatExecutor.setPassCallback(formatGraph, pass, [](RenderGraphExecutionContext const&) {});
				formatExecutor.execute(formatGraph, formatTargets, renderSystem->getCaps());
				if (!formatTargets.get(image)) return fail("curated graph format allocation failed");
			}
			stage = "depth diagnostic";
			GraphImageDesc diagnosticDepthDesc;
			diagnosticDepthDesc.format = GraphImageFormat::Depth32f;
			diagnosticDepthDesc.usage = GraphImageUsage::DepthAttachment;
			RenderGraph diagnosticDepthGraph;
			auto diagnosticDepthImage = diagnosticDepthGraph.createImage("GpuDiagnosticDepth", diagnosticDepthDesc);
			auto diagnosticDepthPass = diagnosticDepthGraph.addPass("GpuDiagnosticDepthPass", GraphPassType::Fullscreen);
			diagnosticDepthImage = diagnosticDepthGraph.writeDepth(diagnosticDepthPass, diagnosticDepthImage, GraphLoadOp::Clear, GraphStoreOp::Store, 0.0f);
			RenderGraphTargets diagnosticDepthTargets(renderSystem);
			diagnosticDepthTargets.allocate(diagnosticDepthGraph.buildAllocationPlan({ 8, 8 }));
			RenderGraphExecutor diagnosticDepthExecutor(renderSystem);
			diagnosticDepthExecutor.setPassCallback(diagnosticDepthGraph, diagnosticDepthPass, [](RenderGraphExecutionContext const&) {});
			diagnosticDepthExecutor.execute(diagnosticDepthGraph, diagnosticDepthTargets, renderSystem->getCaps());
			RenderTextureOptions diagnosticDepthOutputOptions;
			auto diagnosticDepthOutput = renderSystem->createRenderTexture("GpuDiagnosticDepthOutput", 8, 8, diagnosticDepthOutputOptions);
			RenderSystem::TextureDiagnosticOptions diagnosticDepthInspect;
			diagnosticDepthInspect.mode = RenderSystem::TextureDiagnosticMode::Depth;
			renderSystem->renderTextureDiagnostic(static_cast<RenderTexture*>(diagnosticDepthTargets.get(diagnosticDepthImage).get()), diagnosticDepthOutput, diagnosticDepthInspect);
			if (!nearColour(readFirstPixel(diagnosticDepthOutput), { 255, 255, 255, 255 })) return fail("depth diagnostic visualization failed");

			stage = "SSAO depth-only tracer bullet";
			{
				// Exercise the runtime gates through the generated forward graph before
				// checking the AO pixels in an isolated deterministic depth fixture.
				RenderPipelineOptions pipelineOptions;
				pipelineOptions.mode = RenderPipelineMode::GraphLegacyForward;
				auto pipeline = renderSystem->getOrCreateRenderPipeline("GpuTestSsaoPipeline", pipelineOptions);
				auto gateScene = renderSystem->createScene("Default");
				gateScene->setViewport(0, 0, 64, 64);
				gateScene->setClearColour(Colour(0.8f, 0.8f, 0.8f, 1.0f));
				auto gateCamera = std::make_shared<Camera>(glm::vec3(0.0f, 0.0f, 4.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.0f);
				auto waterFlagModelResource = renderSystem->getResourceManager()->getResource("__mpp_mesh_fullscreen_quad__");
				auto waterFlagModel = gateScene->add3dModel(waterFlagModelResource);
				if (waterFlagModel->getDeferToWaterPass()) return fail("scene model water-pass deferral did not default false");
				waterFlagModel->setDeferToWaterPass(true);
				if (!waterFlagModel->getDeferToWaterPass()) return fail("scene model water-pass deferral flag was not retained");
				gateScene->remove3dModel(waterFlagModel);

				// A named but disabled domain has no depth target. Generated graphs must
				// omit its external shadow image while the disabled ShadowFrame keeps
				// direct lighting active; binding a null import used to abort rendering.
				ShadowOptions disabledShadow;
				disabledShadow.enabled = false;
				disabledShadow.light.type = ShadowLightType::Point;
				disabledShadow.light.range = 24.0f;
				disabledShadow.nearPlane = 0.25f;
				disabledShadow.resolution = 16;
				renderSystem->configureShadowDomain("GpuTestDisabledPointShadow", disabledShadow);
				pipeline->setShadowDomain("GpuTestDisabledPointShadow");
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				for (auto const& stats : pipeline->getLastGraphExecutionStats())
					if (stats.name.rfind("PointShadowFace", 0) == 0)
						return fail("disabled point-shadow domain inserted a cubemap face pass");
				auto enabledShadow = disabledShadow;
				enabledShadow.enabled = true;
				renderSystem->configureShadowDomain("GpuTestDisabledPointShadow", enabledShadow);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				renderSystem->configureShadowDomain("GpuTestDisabledPointShadow", disabledShadow);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));

				auto executedSsao = [&]
				{
					for (auto const& stats : pipeline->getLastGraphExecutionStats()) if (stats.name == "SSAO") return true;
					return false;
				};
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (executedSsao()) return fail("disabled SSAO was inserted into the graph");
				for (auto const& stats : pipeline->getLastGraphExecutionStats())
				{
					if (stats.name == "SceneColourCopy" || stats.name == "WaterScene")
						return fail("default-off generated graph inserted water topology");
					if (stats.name == "LegacyScene" && stats.storedDepthOutputCount != 0)
						return fail("default-off generated graph stored otherwise-unused scene depth");
				}
				AmbientOcclusionOptions gateOptions; gateOptions.method = AmbientOcclusionMethod::Ssao;
				pipeline->setAmbientOcclusionOptions(gateOptions);
				BloomOptions gateBloom; gateBloom.enabled = true; gateBloom.blurPasses = 0;
				pipeline->setBloomOptions(gateBloom);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (!executedSsao()) return fail("enabled SSAO was not inserted into the graph");
				size_t ssaoOrder = SIZE_MAX, bloomOrder = SIZE_MAX;
				auto const& gateStats = pipeline->getLastGraphExecutionStats();
				for (size_t index = 0; index < gateStats.size(); ++index)
				{
					if (gateStats[index].name == "SSAO") ssaoOrder = index;
					else if (gateStats[index].name == "BloomExtract") bloomOrder = index;
				}
				if (ssaoOrder == SIZE_MAX || bloomOrder == SIZE_MAX || ssaoOrder >= bloomOrder)
					return fail("SSAO was not fixed immediately before the bloom sequence");
				for (auto const& stats : gateStats)
					if (stats.name == "SceneColourCopy" || stats.name == "WaterScene")
						return fail("default-off generated graph inserted water topology");
				if (pipelineOptions.generatedWater) return fail("generated water did not default disabled");

				RenderPipelineOptions waterDepthOptions;
				waterDepthOptions.mode = RenderPipelineMode::GraphLegacyForward;
				waterDepthOptions.generatedWater = true;
				waterDepthOptions.waterReflections.technique = WaterReflectionTechnique::ScreenSpace;
				waterDepthOptions.outputs = { { "Water", "WaterComposite" } };
				auto waterDepthPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestGeneratedWaterDepthPipeline", waterDepthOptions);
				bool rejectedUnavailableOutput = false;
				try { waterDepthPipeline->getOutputRenderTarget("Water"); }
				catch (std::exception const&) { rejectedUnavailableOutput = true; }
				if (!rejectedUnavailableOutput)
					return fail("named pipeline output was available before graph execution");
				waterDepthPipeline->requestGraphImageCapture();
				waterDepthPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				bool storedWaterDepth = false, boundWaterDepth = false;
				for (auto const& stats : waterDepthPipeline->getLastGraphExecutionStats())
				{
					if (stats.name == "LegacyScene") storedWaterDepth = stats.storedDepthOutputCount == 1;
					else if (stats.name == "WaterScene") boundWaterDepth = stats.samplerBindingCount >= 2;
				}
				if (!storedWaterDepth || !boundWaterDepth)
					return fail("generated water did not sample and store otherwise-unused scene depth");
				// WaterScene is a scene pass but seeds its target with a fullscreen copy.
				// Verify that the copy covers the offscreen target instead of inheriting
				// the 3D camera and window-sized quad transform.
				auto waterComposite = waterDepthPipeline->getGraphImageRenderTarget({ 3, 1 });
				auto namedWater = waterDepthPipeline->getOutputRenderTarget("Water");
				if (!waterComposite || namedWater != waterComposite ||
					!nearColour(readFirstPixel(namedWater), { 204, 204, 204, 255 }))
					return fail("named pipeline output did not follow the final generated water image version");
				bool rejectedUnknownOutput = false;
				try { waterDepthPipeline->getOutputRenderTarget("Missing"); }
				catch (std::exception const&) { rejectedUnknownOutput = true; }
				if (!rejectedUnknownOutput)
					return fail("unknown named pipeline output did not fail explicitly");
				auto captures = waterDepthPipeline->takeGraphImageCaptures();
				auto capturedPass = [&](char const* name, bool depth)
				{
					return find_if(captures.begin(), captures.end(), [&](auto const& capture)
					{
						return capture.passName == name && capture.depth == depth &&
							capture.width == 64 && capture.height == 64 && !capture.pixels.empty();
					}) != captures.end();
				};
				if (!capturedPass("LegacyScene", false) || !capturedPass("LegacyScene", true) ||
					!capturedPass("SceneColourCopy", false) || !capturedPass("WaterScene", false))
					return fail("one-shot graph image capture missed a stored generated-water pass output");
				for (auto const& capture : captures)
					if (capture.passName.rfind("PlanarReflection", 0) == 0)
						return fail("Screen-space capture contained a Planar output");
				if (!waterDepthPipeline->takeGraphImageCaptures().empty())
					return fail("taking graph image captures did not consume them");
				gateScene->setViewport(0, 0, 73, 41);
				waterDepthPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				auto resizedNamedWater = waterDepthPipeline->getOutputRenderTarget("Water");
				if (resizedNamedWater == namedWater || resizedNamedWater->getWidth() != 73 ||
					resizedNamedWater->getHeight() != 41 ||
					resizedNamedWater != waterDepthPipeline->getGraphImageRenderTarget({ 3, 1 }))
					return fail("named pipeline output did not follow graph target reallocation");
				gateScene->setViewport(0, 0, 64, 64);
				renderSystem->removeRenderPipeline("GpuTestGeneratedWaterDepthPipeline");

				RenderPipelineOptions waterOptions;
				waterOptions.mode = RenderPipelineMode::GraphLegacyForward;
				waterOptions.generatedWater = true;
				waterOptions.waterReflections.technique = WaterReflectionTechnique::ScreenSpace;
				waterOptions.ambientOcclusion = gateOptions;
				waterOptions.bloom = gateBloom;
				auto waterPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestGeneratedWaterPipeline", waterOptions);
				auto verifyGeneratedWater = [&](uint32_t width, uint32_t height)
				{
					gateScene->setViewport(0, 0, width, height);
					waterPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
					size_t aoComposite = SIZE_MAX, copy = SIZE_MAX, water = SIZE_MAX, bloom = SIZE_MAX, presentation = SIZE_MAX;
					for (size_t index = 0; index < waterPipeline->getLastGraphExecutionStats().size(); ++index)
					{
						auto const& stats = waterPipeline->getLastGraphExecutionStats()[index];
						if (stats.name == "AmbientOcclusionComposite") aoComposite = index;
						else if (stats.name == "SceneColourCopy")
						{
							copy = index;
							if (stats.samplerBindingCount != 1 || stats.maxColourOutputMipLevels < 2)
								return false;
						}
						else if (stats.name == "WaterScene")
						{
							water = index;
							if (stats.samplerBindingCount < 2 || stats.colourOutputCount != 1 || stats.depthOutputCount != 0)
								return false;
						}
						else if (stats.name == "BloomExtract") bloom = index;
						else if (stats.name == "ToneMapPresentation") presentation = index;
					}
					return aoComposite < copy && copy < water && water < bloom && bloom < presentation;
				};
				if (!verifyGeneratedWater(64, 64) || !verifyGeneratedWater(96, 32))
					return fail("opted-in generated water topology, sampler binding, mip preservation, or resize execution failed");
				renderSystem->removeRenderPipeline("GpuTestGeneratedWaterPipeline");
				// The resize check leaves the gate scene at whichever size it last
				// rendered, and everything below sizes its expectations against the
				// 64x64 gate viewport.
				gateScene->setViewport(0, 0, 64, 64);

				// Planar owns one reflected-scene branch per supplied plane and never
				// acquires Screen-space's frozen colour copy or mip chain.
				RenderPipelineOptions planarSeamOptions;
				planarSeamOptions.mode = RenderPipelineMode::GraphLegacyForward;
				planarSeamOptions.generatedWater = true;
				planarSeamOptions.waterReflections.technique = WaterReflectionTechnique::Planar;
				planarSeamOptions.waterReflections.planarResolution = PlanarReflectionResolution::Quarter;
				for (size_t planeCount = 0; planeCount <= WaterReflectionOptions::MaxPlanarPlanes; ++planeCount)
				{
					planarSeamOptions.waterReflections.planarPlanes.clear();
					for (size_t index = 0; index < planeCount; ++index)
						planarSeamOptions.waterReflections.planarPlanes.push_back(
							{ 3.0f + float(index), ReflectionPlaneSide::Above });
					auto pipelineName = "GpuTestPlanarWaterBranches" + std::to_string(planeCount);
					auto candidate = renderSystem->getOrCreateRenderPipeline(pipelineName, planarSeamOptions);
					candidate->requestGraphImageCapture();
					// Every active reflected-scene branch must execute again on the next
					// frame; the graph owns no temporal cache or refresh throttle.
					for (int frame = 0; frame < 2; ++frame)
					{
						candidate->render(gateScene, gateCamera, glm::vec2(0.0f));
						size_t planarPasses = 0, waterPasses = 0;
						std::array<bool, WaterReflectionOptions::MaxPlanarPlanes> ranks{};
						for (auto const& stats : candidate->getLastGraphExecutionStats())
						{
							if (stats.name == "SceneColourCopy")
								return fail("Planar reflection-source contract reused Screen-space copy topology");
							if (stats.name.rfind("PlanarReflection", 0) == 0)
							{
								++planarPasses;
								for (size_t rank = 0; rank < ranks.size(); ++rank)
									ranks[rank] |= stats.name == "PlanarReflection" + std::to_string(rank);
								if (stats.colourOutputCount != 1 || stats.depthOutputCount != 1 || stats.maxColourOutputMipLevels != 1)
									return fail("Planar reflection pass lost its single colour/depth, no-mip output contract");
								auto const rank = stats.name.substr(std::string("PlanarReflection").size());
								if (stats.primaryColourOutputName != "PlanarReflection" + rank ||
									stats.primaryColourOutputWidth != 16 || stats.primaryColourOutputHeight != 16)
									return fail("Planar pass telemetry lost its stable image name or dimensions: " +
										stats.primaryColourOutputName + " " + std::to_string(stats.primaryColourOutputWidth) +
										"x" + std::to_string(stats.primaryColourOutputHeight));
							}
							else if (stats.name == "WaterScene")
							{
								++waterPasses;
								if (stats.samplerBindingCount < 6)
									return fail("Planar Water pass did not bind its bounded reflected-scene sampler set");
							}
						}
						if (planarPasses != planeCount || waterPasses != (planeCount ? 1u : 0u))
							return fail("zero-through-four Planar descriptors did not create exactly matching dynamic branches");
						for (size_t rank = 0; rank < ranks.size(); ++rank)
							if (ranks[rank] != (rank < planeCount))
								return fail("dynamic Planar branches lost stable rank-based names");
					}
					auto planarCaptures = candidate->takeGraphImageCaptures();
					size_t capturedPlanarImages = 0;
					for (auto const& capture : planarCaptures)
						if (capture.passName.rfind("PlanarReflection", 0) == 0 && !capture.depth)
						{
							++capturedPlanarImages;
							if (capture.passName != capture.imageName || capture.width != 16 || capture.height != 16)
								return fail("Planar capture lost its stable rank-based pass/image name or dimensions");
						}
					if (capturedPlanarImages != planeCount || !candidate->takeGraphImageCaptures().empty())
						return fail("Planar capture did not follow active topology or consume exactly once");
					for (size_t rank = 0; rank < planeCount; ++rank)
					{
						auto target = candidate->getGraphImageRenderTarget(
							{ static_cast<uint32_t>(2 + 2 * rank), 1 });
						if (!target || target->getWidth() != 16 || target->getHeight() != 16 ||
							!nearColour(readFirstPixel(target), { 0, 0, 0, 0 }))
							return fail("dynamic Quarter Planar image dimensions, transparent clear, or alpha validity were incorrect");
					}
					renderSystem->removeRenderPipeline(pipelineName);
				}

				// The same public failure path used by allocation/pass recovery is sticky:
				// it retains Planar as the selected technique, removes its resources and
				// passes, and still runs opaque depth plus WaterScene compositing.
				auto failedOptions = planarSeamOptions;
				failedOptions.waterReflections.planarPlanes = { { 1.0f, ReflectionPlaneSide::Above } };
				auto failedPipeline = renderSystem->getOrCreateRenderPipeline(
					"GpuTestFailedPlanarWater", failedOptions);
				failedPipeline->disablePlanarReflectionsAfterFailure("injected reflected-render failure");
				failedPipeline->requestGraphImageCapture();
				failedPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				bool failedOpaque = false, failedWater = false, hiddenScreenSpace = false, failedPlanarPass = false;
				for (auto const& stats : failedPipeline->getLastGraphExecutionStats())
				{
					failedOpaque |= stats.name == "LegacyScene" && stats.storedDepthOutputCount == 1;
					failedWater |= stats.name == "WaterScene";
					hiddenScreenSpace |= stats.name == "SceneColourCopy";
					failedPlanarPass |= stats.name.rfind("PlanarReflection", 0) == 0;
				}
				if (!failedPipeline->planarReflectionRuntimeFailed() ||
					failedPipeline->getPlanarReflectionFailureMessage().find("injected") == std::string::npos ||
					failedPipeline->getOptions().waterReflections.technique != WaterReflectionTechnique::Planar ||
					!failedOpaque || !failedWater || hiddenScreenSpace || failedPlanarPass)
					return fail("Planar runtime failure did not disable reflection while retaining Planar Water compositing");
				for (auto const& capture : failedPipeline->takeGraphImageCaptures())
					if (capture.passName.rfind("PlanarReflection", 0) == 0)
						return fail("failed Planar topology emitted a reflection capture");
				renderSystem->removeRenderPipeline("GpuTestFailedPlanarWater");

				gateScene->setViewport(0, 0, 65, 33);
				for (auto const& [resolution, expected, name] : {
					std::tuple{ PlanarReflectionResolution::Full, glm::uvec2(65, 33), "Full" },
					std::tuple{ PlanarReflectionResolution::Half, glm::uvec2(33, 17), "Half" },
					std::tuple{ PlanarReflectionResolution::Quarter, glm::uvec2(17, 9), "Quarter" } })
				{
					auto resolutionOptions = planarSeamOptions;
					resolutionOptions.waterReflections.planarResolution = resolution;
					resolutionOptions.waterReflections.planarPlanes = { { 0.0f, ReflectionPlaneSide::Above } };
					auto pipelineName = "GpuTestPlanarResolution" + std::string(name);
					auto resolutionPipeline = renderSystem->getOrCreateRenderPipeline(pipelineName, resolutionOptions);
					resolutionPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
					auto target = resolutionPipeline->getGraphImageRenderTarget({ 2, 1 });
					if (!target || glm::uvec2(target->getWidth(), target->getHeight()) != expected)
						return fail("Planar " + std::string(name) + " resolution did not round relative to the active world target");
					renderSystem->removeRenderPipeline(pipelineName);
				}
				gateScene->setViewport(0, 0, 64, 64);

				// Render asymmetric real scene geometry through the generated pass. Red
				// is a normally culled box (proving mirrored winding), blue is deliberately
				// two-sided, green is below the retained plane, yellow is classified as
				// Water, and magenta is transparent. The shader emits full colour only when
				// the pass-scoped virtual-camera marker is active.
				mesh::MeshSpecification planarMeshSpec(mesh::Primitive::Type::Triangles);
				auto planarLayout = planarMeshSpec.createVertexBufferAttributeLayout(false);
				planarLayout->createAttribute(mesh::Vertex::Component::Position3, mesh::Vertex::DataType::Float, false);
				planarLayout->createAttribute(mesh::Vertex::Component::Normal3, mesh::Vertex::DataType::Float, false);
				planarLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
				planarLayout->createAttribute(mesh::Vertex::Component::Colour4, mesh::Vertex::DataType::Float, false);
				auto planarParser = std::make_shared<program::Parser>();
				planarParser->setMeshSpecification(planarMeshSpec);
				planarParser->setVertexSource(VertexShader3dTemplate);
				planarParser->setFragmentSource(R"(
@@Version
@@Uniform(vec4 GPU_TEST_COLOUR);
@@Uniform(float GPU_TEST_PASS_MODE);
@@Uniform(int MPP_VIRTUAL_CAMERA);
void main()
{
    bool virtualCamera = @Uniform(MPP_VIRTUAL_CAMERA) == 1;
    float validity = virtualCamera ? 1.0 : 0.25;
    vec3 colour = mix(
        @Uniform(GPU_TEST_COLOUR).rgb * validity,
        vec3(validity, 0.0, 0.0), @Uniform(GPU_TEST_PASS_MODE));
    @Out(vec4 COLOUR) = vec4(colour, 1.0);
}
)");
				auto planarProgramStream = std::make_shared<ProgrammaticProgramStream>(renderSystem->getResourceManager());
				planarProgramStream->setParser(planarParser);
				auto planarProgram = renderSystem->getResourceManager()->declareResource(
					"GpuTestPlanar.Program", planarProgramStream).first;
				planarProgram->load();
				auto planarMaterialStream = std::make_shared<ProgrammaticBasicMaterialStream>(renderSystem->getResourceManager());
				planarMaterialStream->setProgram(planarProgram->getName());
				planarMaterialStream->setUniform("GPU_TEST_COLOUR", glm::vec4(1.0f));
				planarMaterialStream->setUniform("GPU_TEST_PASS_MODE", 0.0f);
				auto planarMaterial = renderSystem->getResourceManager()->declareResource(
					"GpuTestPlanar.Material", planarMaterialStream).first;
				planarMaterial->load();
				auto planarModelStream = std::make_shared<BoxModelStream>(renderSystem->getResourceManager(),
					planarMeshSpec, planarMaterial->getName(), 0.9f, 0.9f, 0.9f);
				auto planarModel = renderSystem->getResourceManager()->declareResource(
					"GpuTestPlanar.Model", planarModelStream).first;
				planarModel->load();
				auto planarScene = renderSystem->createScene("Default");
				planarScene->setViewport(0, 0, 64, 64);
				planarScene->setClearColour(Colour(0.0f, 0.0f, 0.0f, 0.0f));
				auto addPlanarFixture = [&](glm::vec3 const& position, glm::vec4 const& colour,
					bool cull, bool water, bool transparent = false)
				{
					auto model = planarScene->add3dModel(planarModel);
					model->translate(position);
					auto uniforms = std::make_shared<UniformCollection>();
					uniforms->setUniform("GPU_TEST_COLOUR", colour);
					model->getParams()->setModelUniforms(uniforms);
					model->getParams()->setModelFlags(ModelRenderParams::Flag_Visible |
						ModelRenderParams::Flag_CastShadows | (cull ? ModelRenderParams::Flag_CullBackFaces : 0));
					if (transparent) model->getParams()->setModelBlend(true);
					model->setDeferToWaterPass(water);
				};
				addPlanarFixture(glm::vec3(-1.0f, 1.1f, 0.0f), glm::vec4(1, 0, 0, 1), true, false);
				addPlanarFixture(glm::vec3(1.0f, 1.1f, 0.0f), glm::vec4(0, 0, 1, 1), false, false);
				addPlanarFixture(glm::vec3(0.0f, -1.1f, 0.0f), glm::vec4(0, 1, 0, 1), true, false);
				addPlanarFixture(glm::vec3(0.0f, 1.1f, -1.5f), glm::vec4(1, 1, 0, 1), true, true);
				addPlanarFixture(glm::vec3(0.0f, 1.1f, 1.5f), glm::vec4(1, 0, 1, 1), true, false, true);
				auto planarCamera = std::make_shared<Camera>(glm::vec3(0.0f, 2.5f, 6.0f),
					0.0f, 0.0f, 0.0f, 55.0f, 1.0f);
				planarCamera->setLookAt(planarCamera->getPosition(), glm::vec3(0.0f, 0.5f, 0.0f));
				RenderPipelineOptions renderedPlanarOptions;
				renderedPlanarOptions.mode = RenderPipelineMode::GraphLegacyForward;
				renderedPlanarOptions.generatedWater = true;
				renderedPlanarOptions.waterReflections.technique = WaterReflectionTechnique::Planar;
				renderedPlanarOptions.waterReflections.planarResolution = PlanarReflectionResolution::Full;
				renderedPlanarOptions.waterReflections.planarPlanes = { { 0.0f, ReflectionPlaneSide::Above } };
				auto renderedPlanarPipeline = renderSystem->getOrCreateRenderPipeline(
					"GpuTestRenderedPlanarPipeline", renderedPlanarOptions);
				renderedPlanarPipeline->render(planarScene, planarCamera, glm::vec2(0.0f));
				auto renderedPlanarTarget = renderedPlanarPipeline->getGraphImageRenderTarget({ 2, 1 });
				auto planarPixels = readPixels(renderedPlanarTarget);
				bool foundRed = false, foundBlue = false, foundForbiddenGreen = false,
					foundForbiddenMagenta = false, foundValidAlpha = false;
				for (size_t pixel = 0; pixel + 3 < planarPixels.size(); pixel += 4)
				{
					foundRed |= planarPixels[pixel] > 180 && planarPixels[pixel + 1] < 40 && planarPixels[pixel + 2] < 40;
					foundBlue |= planarPixels[pixel + 2] > 180 && planarPixels[pixel] < 40 && planarPixels[pixel + 1] < 40;
					foundForbiddenGreen |= planarPixels[pixel + 1] > 100;
					foundForbiddenMagenta |= planarPixels[pixel] > 100 && planarPixels[pixel + 2] > 100;
					foundValidAlpha |= planarPixels[pixel + 3] > 250;
				}
				if (!foundRed || !foundBlue || foundForbiddenGreen || foundForbiddenMagenta || !foundValidAlpha)
					return fail("rendered Planar output lost reflected content, winding/two-sided geometry, clipping, opaque/non-Water filtering, virtual-camera state, or alpha validity");
				renderSystem->removeRenderPipeline("GpuTestRenderedPlanarPipeline");

				// Crossing the plane changes only its retained side. The below view must
				// continue to produce valid reflected content, now excluding every fixture
				// that the preceding above view retained.
				renderedPlanarOptions.waterReflections.planarPlanes = {
					{ 0.0f, ReflectionPlaneSide::Below } };
				auto renderedBelowPipeline = renderSystem->getOrCreateRenderPipeline(
					"GpuTestRenderedPlanarBelowPipeline", renderedPlanarOptions);
				auto planarCameraBelow = std::make_shared<Camera>(
					glm::vec3(0.0f, -2.5f, 6.0f), 0.0f, 0.0f, 0.0f, 55.0f, 1.0f);
				planarCameraBelow->setLookAt(
					planarCameraBelow->getPosition(), glm::vec3(0.0f, -0.5f, 0.0f));
				renderedBelowPipeline->render(
					planarScene, planarCameraBelow, glm::vec2(0.0f));
				auto belowPixels = readPixels(
					renderedBelowPipeline->getGraphImageRenderTarget({ 2, 1 }));
				bool foundBelowGreen = false, foundForbiddenAbove = false;
				for (size_t pixel = 0; pixel + 3 < belowPixels.size(); pixel += 4)
				{
					foundBelowGreen |= belowPixels[pixel + 1] > 180 &&
						belowPixels[pixel] < 40 && belowPixels[pixel + 2] < 40;
					foundForbiddenAbove |= belowPixels[pixel] > 100 || belowPixels[pixel + 2] > 100;
				}
				if (!foundBelowGreen || foundForbiddenAbove)
					return fail("rendered Planar content was unstable while crossing from the above side to the below side");
				renderSystem->removeRenderPipeline("GpuTestRenderedPlanarBelowPipeline");

				// Different horizontal planes own independent camera projections and
				// clipping results. Plane zero sees the fixture above it; plane two sees
				// no retained fixture, so their images must remain observably distinct.
				renderedPlanarOptions.waterReflections.planarPlanes = {
					{ 0.0f, ReflectionPlaneSide::Above },
					{ 2.0f, ReflectionPlaneSide::Above } };
				auto multiElevationPipeline = renderSystem->getOrCreateRenderPipeline(
					"GpuTestMultiElevationPlanarPipeline", renderedPlanarOptions);
				multiElevationPipeline->render(planarScene, planarCamera, glm::vec2(0.0f));
				auto lowPlaneTarget =
					multiElevationPipeline->getGraphImageRenderTarget({ 2, 1 });
				auto highPlaneTarget =
					multiElevationPipeline->getGraphImageRenderTarget({ 4, 1 });
				if (!lowPlaneTarget || !highPlaneTarget ||
					readPixels(lowPlaneTarget) == readPixels(highPlaneTarget))
					return fail("different Planar elevations produced indistinguishable reflected results");
				renderSystem->removeRenderPipeline("GpuTestMultiElevationPlanarPipeline");

				// Exercise the public auxiliary render-scene seam rather than its draw
				// internals. The exact matrix view clips against a vertical plane, publishes
				// HDR colour plus sampled depth at the declared odd resolution, and leaves
				// the host renderer/camera untouched.
				auto auxiliaryScene = renderSystem->createScene("Default");
				auto rejectedAuxiliaryScene = renderSystem->createScene("Default");
				auxiliaryScene->setClearColour(Colour(0.0f, 0.0f, 0.0f, 0.0f));
				rejectedAuxiliaryScene->setClearColour(Colour(0.0f, 0.0f, 0.0f, 0.0f));
				auto addAuxiliaryFixture = [&](ScenePtr const& targetScene, glm::vec3 const& position, glm::vec4 const& colour)
				{
					auto model = targetScene->add3dModel(planarModel);
					model->translate(position);
					auto uniforms = std::make_shared<UniformCollection>();
					uniforms->setUniform("GPU_TEST_COLOUR", colour);
					model->getParams()->setModelUniforms(uniforms);
					model->getParams()->setModelFlags(ModelRenderParams::Flag_Visible |
						ModelRenderParams::Flag_CastShadows | ModelRenderParams::Flag_CullBackFaces);
					model->getParams()->setModelBlend(false);
				};
				addAuxiliaryFixture(auxiliaryScene, glm::vec3(1.1f, 0.0f, 0.0f), glm::vec4(0, 0, 1, 1));
				addAuxiliaryFixture(rejectedAuxiliaryScene, glm::vec3(-1.1f, 0.0f, 0.0f), glm::vec4(0, 1, 0, 1));
				auto auxiliaryHostCamera = std::make_shared<Camera>(
					glm::vec3(0.0f, 0.0f, 6.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.0f);
				auxiliaryHostCamera->setProjectionJitter(glm::vec2(0.013f, -0.021f));
				auto const hostViewBefore = auxiliaryHostCamera->getViewTransform();
				auto const hostProjectionBefore = auxiliaryHostCamera->getProjectionTransform();
				auto const hostRevisionBefore = auxiliaryHostCamera->getRevision();
				auto const hostJitterBefore = auxiliaryHostCamera->getProjectionJitter();

				RenderPipelineOptions auxiliaryOptions;
				auxiliaryOptions.mode = RenderPipelineMode::GraphLegacyForward;
				auto auxiliaryPipeline = renderSystem->getOrCreateRenderPipeline(
					"GpuTestAuxiliaryPipeline", auxiliaryOptions);
				auto sentinelTarget = renderSystem->createRenderTexture(
					"GpuTestAuxiliarySentinel", 13, 11, RenderTextureOptions{});
				renderSystem->setRenderTarget(sentinelTarget);
				renderSystem->setViewport(1, 2, 7, 5);
				GraphRasterState sentinelRaster;
				sentinelRaster.explicitState = true;
				sentinelRaster.depthTest = false;
				sentinelRaster.depthWrite = false;
				sentinelRaster.cullMode = GraphCullMode::Front;
				sentinelRaster.frontFace = GraphFrontFace::Clockwise;
				sentinelRaster.blend = true;
				sentinelRaster.scissor = true;
				sentinelRaster.scissorRectangle = { 1, 2, 7, 5 };
				renderSystem->applyRasterState(sentinelRaster, 1, 7, 5);
				auto const expectedSentinelRaster = renderSystem->captureRasterState(1);
				renderSystem->setAmbientColour(Colour(0.17f, 0.23f, 0.31f, 1.0f));
				renderSystem->setLightCount(1);
				renderSystem->setPbrLights({ PbrLight{} });
				auto const sentinelFrameView = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
				auto const sentinelFrameProjection = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
				renderSystem->setCameraFrame(sentinelFrameView, sentinelFrameProjection,
					glm::vec2(7.0f, 5.0f), 0.2f, 321.0f, 7.0f);
				auto const sentinelCameraFrame = renderSystem->mCameraFrameBuffer->getBufferData();
				auto const sentinelLegacyLights = renderSystem->mLightsBuffer->getBufferData();
				auto const sentinelPbrLights = renderSystem->mPbrLightsBuffer->getBufferData();

				UniformCollection sentinelPassUniforms;
				sentinelPassUniforms.setUniform("GPU_TEST_PASS_MODE", 0.75f);
				renderSystem->setActivePipelineUniformOverrides(sentinelPassUniforms);
				auto sentinelSamplerResource = renderSystem->mNoTexture;
				renderSystem->setActivePipelineSamplerOverrides({
					{ "GPU_TEST_SENTINEL_SAMPLER", sentinelSamplerResource } });

				AuxiliarySceneView auxiliaryView;
				auxiliaryView.slot = "PortalCandidate0";
				auxiliaryView.uniformOverrides.setUniform(
					"GPU_TEST_PASS_MODE", 1.0f);
				auxiliaryView.samplerOverrides.emplace(
					"GPU_TEST_PASS_SAMPLER", sentinelSamplerResource);
				auxiliaryView.width = 47;
				auxiliaryView.height = 31;
				auxiliaryView.nearDistance = 0.1f;
				auxiliaryView.farDistance = 100.0f;
				auxiliaryView.view = glm::lookAt(glm::vec3(4.0f, 0.0f, 6.0f),
					glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				auxiliaryView.projection = glm::perspective(
					glm::radians(60.0f), 47.0f / 31.0f, 0.1f, 100.0f);
				auxiliaryView.worldClipPlane = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
				auto auxiliaryOutputs = renderSystem->renderAuxiliaryScene(
					auxiliaryScene, auxiliaryHostCamera, auxiliaryPipeline->getName(), auxiliaryView);
				auto auxiliaryTexture = dynamic_cast<RenderTexture*>(auxiliaryOutputs.colour.get());
				auto const auxiliaryPixels = readPixels(auxiliaryOutputs.colour);
				bool foundFullVirtualCameraColour = false;
				std::array<uint8_t, 3> auxiliaryMax{};
				for (size_t pixel = 0; pixel + 3 < auxiliaryPixels.size(); pixel += 4)
				{
					foundFullVirtualCameraColour |= auxiliaryPixels[pixel] > 180 ||
						auxiliaryPixels[pixel + 1] > 180 || auxiliaryPixels[pixel + 2] > 180;
					for (size_t channel = 0; channel < 3; ++channel)
						auxiliaryMax[channel] = std::max(auxiliaryMax[channel], auxiliaryPixels[pixel + channel]);
				}
				auto rejectedView = auxiliaryView;
				rejectedView.slot = "RejectedOnly";
				auto rejectedOutputs = renderSystem->renderAuxiliaryScene(
					rejectedAuxiliaryScene, auxiliaryHostCamera, auxiliaryPipeline->getName(), rejectedView);
				auto const& auxiliaryDiagnostics = renderSystem->getAuxiliarySceneDiagnostics(
					auxiliaryPipeline->getName(), auxiliaryView.slot);
				if (!auxiliaryTexture || auxiliaryOutputs.depth != auxiliaryOutputs.colour ||
					auxiliaryTexture->getWidth() != 47 || auxiliaryTexture->getHeight() != 31 ||
					auxiliaryTexture->getBitsPerPixel() < 64 || !auxiliaryTexture->getDepthTextureId() ||
					minimumDepth(auxiliaryOutputs.depth) < 0.0f || minimumDepth(auxiliaryOutputs.depth) >= 1.0f ||
					!foundFullVirtualCameraColour || containsVisiblePixel(rejectedOutputs.colour) ||
					!auxiliaryDiagnostics.succeeded || !auxiliaryDiagnostics.failureReason.empty() ||
					auxiliaryDiagnostics.passName.find("PortalCandidate0") == std::string::npos ||
					auxiliaryDiagnostics.colourOutputName != auxiliaryOutputs.colourName ||
					auxiliaryDiagnostics.depthOutputName != auxiliaryOutputs.depthName ||
					auxiliaryDiagnostics.width != 47 || auxiliaryDiagnostics.height != 31)
					return fail(std::format("public auxiliary scene validation failed: texture={} sameDepth={} size={}x{} bpp={} depthId={} minDepth={} marker={} rejectedVisible={} diagnostic={} reason='{}' pass='{}' colourNameMatch={} depthNameMatch={} diagnosticSize={}x{} maxRgb={}/{}/{}",
						auxiliaryTexture != nullptr, auxiliaryOutputs.depth == auxiliaryOutputs.colour,
						auxiliaryTexture ? auxiliaryTexture->getWidth() : 0,
						auxiliaryTexture ? auxiliaryTexture->getHeight() : 0,
						auxiliaryTexture ? auxiliaryTexture->getBitsPerPixel() : 0,
						auxiliaryTexture ? auxiliaryTexture->getDepthTextureId() : 0,
						minimumDepth(auxiliaryOutputs.depth), foundFullVirtualCameraColour,
						containsVisiblePixel(rejectedOutputs.colour), auxiliaryDiagnostics.succeeded,
						auxiliaryDiagnostics.failureReason, auxiliaryDiagnostics.passName,
						auxiliaryDiagnostics.colourOutputName == auxiliaryOutputs.colourName,
						auxiliaryDiagnostics.depthOutputName == auxiliaryOutputs.depthName,
						auxiliaryDiagnostics.width, auxiliaryDiagnostics.height,
						auxiliaryMax[0], auxiliaryMax[1], auxiliaryMax[2]));
				if (auxiliaryPipeline->getAuxiliarySceneOutputs(auxiliaryView.slot).colour != auxiliaryOutputs.colour)
					return fail("completed auxiliary outputs were not inspectable by deterministic slot");

				auto passUniformsRestored = [&]
				{
					auto const& uniforms = renderSystem
						->getActivePipelineUniformOverrides().getUniformData();
					auto found = uniforms.find("GPU_TEST_PASS_MODE");
					float const sentinel = 0.75f;
					return uniforms.size() == 1 && found != uniforms.end() &&
						found->second.size == sizeof(float) &&
						std::memcmp(found->second.data, &sentinel, sizeof(sentinel)) == 0;
				};
				auto rendererStateRestored = [&]
				{
					return renderSystem->mRenderTarget == sentinelTarget &&
						renderSystem->mViewportX == 1 && renderSystem->mViewportY == 2 &&
						renderSystem->mViewportWidth == 7 && renderSystem->mViewportHeight == 5 &&
						renderSystem->captureRasterState(1) == expectedSentinelRaster &&
						renderSystem->mCameraFrameView == sentinelFrameView &&
						renderSystem->mCameraFrameProjection == sentinelFrameProjection &&
						renderSystem->mCameraFrameBuffer->getBufferData() == sentinelCameraFrame &&
						renderSystem->mLightsBuffer->getBufferData() == sentinelLegacyLights &&
						renderSystem->mPbrLightsBuffer->getBufferData() == sentinelPbrLights &&
						passUniformsRestored() &&
						renderSystem->getActivePipelineSamplerOverrides().size() == 1 &&
						renderSystem->getActivePipelineSamplerOverrides().contains("GPU_TEST_SENTINEL_SAMPLER") &&
						renderSystem->getActivePipelineSamplerOverrides().at("GPU_TEST_SENTINEL_SAMPLER") == sentinelSamplerResource;
				};
				if (!rendererStateRestored() ||
					auxiliaryHostCamera->getRevision() != hostRevisionBefore ||
					auxiliaryHostCamera->getProjectionJitter() != hostJitterBefore ||
					auxiliaryHostCamera->getViewTransform() != hostViewBefore ||
					auxiliaryHostCamera->getProjectionTransform() != hostProjectionBefore)
					return fail("successful auxiliary execution did not restore renderer and host-camera state");

				auto throwingScene = std::make_shared<ThrowingAuxiliaryTestScene>(renderSystem);
				auto failingView = auxiliaryView;
				failingView.slot = "InjectedFailure";
				bool auxiliaryFailureThrown = false;
				try
				{
					renderSystem->renderAuxiliaryScene(
						throwingScene, auxiliaryHostCamera, auxiliaryPipeline->getName(), failingView);
				}
				catch (std::exception const&) { auxiliaryFailureThrown = true; }
				auto const& failedAuxiliaryDiagnostics = renderSystem->getAuxiliarySceneDiagnostics(
					auxiliaryPipeline->getName(), failingView.slot);
				if (!auxiliaryFailureThrown || failedAuxiliaryDiagnostics.succeeded ||
					failedAuxiliaryDiagnostics.failureReason.find("injected auxiliary visibility failure") == std::string::npos ||
					failedAuxiliaryDiagnostics.colourOutputName.find("InjectedFailure") == std::string::npos ||
					!rendererStateRestored() ||
					auxiliaryHostCamera->getRevision() != hostRevisionBefore ||
					auxiliaryHostCamera->getProjectionJitter() != hostJitterBefore ||
					auxiliaryHostCamera->getViewTransform() != hostViewBefore ||
					auxiliaryHostCamera->getProjectionTransform() != hostProjectionBefore)
					return fail("failed auxiliary execution lost its reason or contaminated camera-frame, lights, raster, target, viewport, or host Camera state");

				ScenePassOverrides primaryPassOverrides;
				primaryPassOverrides.uniforms.setUniform("GPU_TEST_PASS_MODE", 0.25f);
				primaryPassOverrides.samplers.emplace("GPU_TEST_PASS_SAMPLER", sentinelSamplerResource);
				auxiliaryPipeline->render(auxiliaryScene, auxiliaryHostCamera,
					glm::vec2(0.0f), primaryPassOverrides);
				if (!passUniformsRestored() ||
					renderSystem->getActivePipelineSamplerOverrides().size() != 1 ||
					!renderSystem->getActivePipelineSamplerOverrides().contains("GPU_TEST_SENTINEL_SAMPLER"))
					return fail("primary ScenePassOverrides contaminated caller uniform or sampler state");

				renderSystem->setActivePipelineUniformOverrides({});
				renderSystem->setActivePipelineSamplerOverrides({});
				renderSystem->renderToScreen();
				renderSystem->resetViewport();
				renderSystem->removeRenderPipeline("GpuTestAuxiliaryPipeline");

				gateOptions.method = AmbientOcclusionMethod::Gtao;
				pipeline->setAmbientOcclusionOptions(gateOptions);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				bool executedGtao = false;
				for (auto const& stats : pipeline->getLastGraphExecutionStats()) executedGtao |= stats.name == "GTAO";
				if (!executedGtao || executedSsao()) return fail("switching ambient-occlusion method did not replace SSAO with GTAO");

				// Generated legacy and PBR pipelines both own location 2, route it to
				// GTAO, and remove it again when depth normals are restored. PBR also
				// keeps the location-1 emissive mask and AO-before-bloom ordering.
				gateOptions.gtao.normalSource = GTAONormalSource::Mrt;
				pipeline->setAmbientOcclusionOptions(gateOptions);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				auto sceneOutputCount = [](RenderPipeline const& candidate, std::string const& scenePass)
				{
					for (auto const& stats : candidate.getLastGraphExecutionStats()) if (stats.name == scenePass) return stats.colourOutputCount;
					return uint32_t{ 0 };
				};
				if (sceneOutputCount(*pipeline, "LegacyScene") != 3) return fail("generated legacy MRT-normal GTAO did not execute with the stable three-output scene contract");
				gateOptions.gtao.normalSource = GTAONormalSource::Depth;
				pipeline->setAmbientOcclusionOptions(gateOptions);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (sceneOutputCount(*pipeline, "LegacyScene") != 1) return fail("generated legacy depth-normal GTAO retained MRT-normal attachments");

				RenderPipelineOptions pbrOptions; pbrOptions.mode = RenderPipelineMode::GraphPbrForward;
				pbrOptions.ambientOcclusion = gateOptions; pbrOptions.ambientOcclusion.gtao.normalSource = GTAONormalSource::Mrt;
				pbrOptions.bloom.enabled = true; pbrOptions.bloom.useMrtEmissiveMask = true; pbrOptions.bloom.blurPasses = 0;
				auto pbrPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestGtaoMrtPbrPipeline", pbrOptions);
				pbrPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (sceneOutputCount(*pbrPipeline, "PbrScene") != 3) return fail("generated PBR GTAO normals did not coexist with the bloom emissive MRT");
				size_t gtaoOrder = SIZE_MAX, bloomCompositeOrder = SIZE_MAX;
				for (size_t index = 0; index < pbrPipeline->getLastGraphExecutionStats().size(); ++index)
				{
					auto const& name = pbrPipeline->getLastGraphExecutionStats()[index].name;
					if (name == "GTAO") gtaoOrder = index;
					else if (name == "BloomComposite") bloomCompositeOrder = index;
				}
				if (gtaoOrder == SIZE_MAX || bloomCompositeOrder == SIZE_MAX || gtaoOrder >= bloomCompositeOrder)
					return fail("generated PBR MRT-normal GTAO did not retain AO-before-bloom ordering");
				pbrOptions.ambientOcclusion.gtao.normalSource = GTAONormalSource::Depth;
				pbrPipeline->setAmbientOcclusionOptions(pbrOptions.ambientOcclusion);
				pbrPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (sceneOutputCount(*pbrPipeline, "PbrScene") != 2) return fail("generated PBR depth normals did not remove location-2 resource cost while retaining bloom MRT");
				renderSystem->removeRenderPipeline("GpuTestGtaoMrtPbrPipeline");

				// sceneExtraOutputs appends one more scene-pass colour attachment per
				// declared entry, following the same allocation pattern as GTAO's MRT
				// normals slot, and must fail fast (like that slot) when the request
				// exceeds what the hardware or scene materials can support.
				RenderPipelineOptions extraOptions; extraOptions.mode = RenderPipelineMode::GraphLegacyForward;
				extraOptions.sceneExtraOutputs = { { "GpuTestExtra", GraphImageFormat::R8 } };
				auto extraPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestSceneExtraPipeline", extraOptions);
				extraPipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (sceneOutputCount(*extraPipeline, "LegacyScene") != 2)
					return fail("scene extra output did not append an additional MRT colour attachment");
				renderSystem->removeRenderPipeline("GpuTestSceneExtraPipeline");

				RenderPipelineOptions overflowOptions; overflowOptions.mode = RenderPipelineMode::GraphLegacyForward;
				size_t const overflowCount = (size_t)std::max(renderSystem->getCaps().maxColourAttachments, renderSystem->getCaps().maxDrawBuffers) + 4;
				for (size_t index = 0; index < overflowCount; ++index)
					overflowOptions.sceneExtraOutputs.push_back({ "GpuTestExtraOverflow" + std::to_string(index), GraphImageFormat::R8 });
				auto overflowPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestSceneExtraOverflowPipeline", overflowOptions);
				bool rejectedExtraOverflow = false;
				try { overflowPipeline->render(gateScene, gateCamera, glm::vec2(0.0f)); } catch (...) { rejectedExtraOverflow = true; }
				if (!rejectedExtraOverflow) return fail("scene extra outputs exceeding the attachment budget were accepted");
				renderSystem->removeRenderPipeline("GpuTestSceneExtraOverflowPipeline");

				// AmbientOcclusionOptions::modulationInput names a graph image (typically
				// a sceneExtraOutputs entry) that the composite pass optionally samples;
				// a valid name must wire through without throwing, an unknown one must
				// fail fast like the other named-output lookups in this file.
				RenderPipelineOptions modulationOptions; modulationOptions.mode = RenderPipelineMode::GraphLegacyForward;
				modulationOptions.sceneExtraOutputs = { { "GpuTestModulation", GraphImageFormat::R8 } };
				modulationOptions.ambientOcclusion.method = AmbientOcclusionMethod::Ssao;
				modulationOptions.ambientOcclusion.modulationInput = "SceneExtra.GpuTestModulation";
				auto modulationPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestAoModulationPipeline", modulationOptions);
				bool modulationRenderThrew = false;
				try { modulationPipeline->render(gateScene, gateCamera, glm::vec2(0.0f)); } catch (...) { modulationRenderThrew = true; }
				if (modulationRenderThrew) return fail("ambient-occlusion modulationInput naming a declared scene extra output was rejected");
				renderSystem->removeRenderPipeline("GpuTestAoModulationPipeline");

				auto unknownModulationOptions = modulationOptions;
				unknownModulationOptions.ambientOcclusion.modulationInput = "SceneExtra.NoSuchImage";
				auto unknownModulationPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestAoModulationUnknownPipeline", unknownModulationOptions);
				bool rejectedUnknownModulation = false;
				try { unknownModulationPipeline->render(gateScene, gateCamera, glm::vec2(0.0f)); } catch (...) { rejectedUnknownModulation = true; }
				if (!rejectedUnknownModulation) return fail("ambient-occlusion modulationInput naming an unknown graph image was accepted");
				renderSystem->removeRenderPipeline("GpuTestAoModulationUnknownPipeline");

				GraphPassDebugOptions graphPasses; graphPasses.ambientOcclusion = false;
				pipeline->setGraphPassDebugOptions(graphPasses);
				pipeline->render(gateScene, gateCamera, glm::vec2(0.0f));
				if (executedSsao()) return fail("the SSAO graph debug gate did not suppress the pass");
				renderSystem->removeRenderPipeline("GpuTestSsaoPipeline");

				// A white plane at z=-4 with a nearer slab at z=-3 forms a known
				// screen-space crease. Its shaded colour is deliberately uniform so
				// any difference comes from depth-only AO, not scene lighting.
				constexpr size_t size = 64;
				RenderTextureOptions colourOptions;
				colourOptions.params.minFilter = GL_NEAREST; colourOptions.params.magFilter = GL_NEAREST;
				colourOptions.params.wrap = GL_CLAMP_TO_EDGE;
				auto sceneColour = renderSystem->createRenderTexture("GpuTestSsaoScene", size, size, colourOptions);
				auto disabledOutput = renderSystem->createRenderTexture("GpuTestSsaoDisabled", size, size, colourOptions);
				auto enabledOutput = renderSystem->createRenderTexture("GpuTestSsaoEnabled", size, size, colourOptions);
				auto rawOutput = renderSystem->createRenderTexture("GpuTestSsaoRaw", size, size, colourOptions);
				auto blurredOutput = renderSystem->createRenderTexture("GpuTestSsaoBlurred", size, size, colourOptions);
				auto zeroRadiusOutput = renderSystem->createRenderTexture("GpuTestSsaoZeroRadius", size, size, colourOptions);
				auto gtaoOutput = renderSystem->createRenderTexture("GpuTestGtaoRaw", size, size, colourOptions);
				auto gtaoMrtFlatOutput = renderSystem->createRenderTexture("GpuTestGtaoMrtFlat", size, size, colourOptions);
				auto gtaoMrtTiltedOutput = renderSystem->createRenderTexture("GpuTestGtaoMrtTilted", size, size, colourOptions);
				RenderTextureOptions normalsOptions = colourOptions; normalsOptions.colourInternalFormat = GL_RG16F;
				auto gtaoNormals = renderSystem->createRenderTexture("GpuTestGtaoNormals", size, size, normalsOptions);
				RenderTextureOptions ssaoDepthOptions;
				ssaoDepthOptions.numAttachments = 0;
				ssaoDepthOptions.depthAttachment = RenderTextureDepthAttachment::DepthTexture;
				ssaoDepthOptions.depthFormat = RenderTextureDepthFormat::Depth24;
				ssaoDepthOptions.depthParams.params.minFilter = GL_NEAREST;
				ssaoDepthOptions.depthParams.params.magFilter = GL_NEAREST;
				ssaoDepthOptions.depthParams.params.wrap = GL_CLAMP_TO_EDGE;
				auto sceneDepthTarget = renderSystem->createRenderTexture("GpuTestSsaoDepth", size, size, ssaoDepthOptions);
				auto sceneDepthTexture = static_cast<RenderTexture*>(sceneDepthTarget.get());
				auto projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
				auto deviceDepth = [&](float viewZ)
				{
					auto clip = projection * glm::vec4(0.0f, 0.0f, viewZ, 1.0f);
					return clip.z / clip.w * 0.5f + 0.5f;
				};
				std::vector<float> depthValues(size * size, deviceDepth(-4.0f));
				for (size_t y = 8; y < 56; ++y) for (size_t x = 16; x < 32; ++x)
					depthValues[y * size + x] = deviceDepth(-3.0f);
				GL_CHECK(glBindTexture(GL_TEXTURE_2D, sceneDepthTexture->getDepthTextureId()));
				GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)size, (GLsizei)size, GL_DEPTH_COMPONENT, GL_FLOAT, depthValues.data()));
				GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
				renderSystem->pushRenderTarget(sceneColour); renderSystem->setViewport(0, 0, size, size);
				GL_CHECK(glClearColor(0.8f, 0.8f, 0.8f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
				renderSystem->popRenderTarget();

				renderSystem->pushProjectionMatrix(); renderSystem->pushCameraMatrix(); renderSystem->pushModelMatrix();
				renderSystem->setProjection2dOrthographic(); renderSystem->resetTransform();
				renderSystem->scaleTransform2d(glm::vec2((float)size / renderSystem->getWindowWidth(), (float)size / renderSystem->getWindowHeight()));
				SSAOOptions ssao;
				bool ssaoEnabled = false;
				auto renderSsaoFixture = [&](RenderTargetPtr const& output)
				{
					renderSystem->pushRenderTarget(output); renderSystem->setViewport(0, 0, size, size);
					if (ssaoEnabled)
					{
						renderSystem->pushRenderTarget(rawOutput); renderSystem->setViewport(0, 0, size, size);
						renderSystem->renderSSAORaw(sceneDepthTexture, projection, glm::inverse(projection), ssao);
						renderSystem->popRenderTarget();
						renderSystem->renderSSAOCombine(dynamic_cast<Texture*>(sceneColour.get()), dynamic_cast<Texture*>(rawOutput.get()));
					}
					else renderSystem->renderFullscreenQuad(dynamic_cast<Texture*>(sceneColour.get()), BlendMode::One, BlendMode::Zero);
					renderSystem->popRenderTarget();
				};
				renderSsaoFixture(disabledOutput);
				ssaoEnabled = true;
				renderSsaoFixture(enabledOutput);
				// Render the raw AO term once more, then denoise it with a larger radius.
				renderSystem->pushRenderTarget(rawOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderSSAORaw(sceneDepthTexture, projection, glm::inverse(projection), ssao);
				renderSystem->popRenderTarget();
				renderSystem->pushRenderTarget(blurredOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderSSAOBlur(dynamic_cast<Texture*>(rawOutput.get()), sceneDepthTexture, 4);
				renderSystem->popRenderTarget();

				// renderSSAOCombine's optional modulation input blends
				// appliedAmbient = mix(1.0, ao, modulation): modulation=0 must fall back
				// to the unmodified scene colour, and modulation=1 must reproduce the
				// plain (unmodulated) combine exactly.
				auto modulationZero = renderSystem->createRenderTexture("GpuTestSsaoModulationZero", size, size, colourOptions);
				auto modulationOne = renderSystem->createRenderTexture("GpuTestSsaoModulationOne", size, size, colourOptions);
				auto modulatedZeroOutput = renderSystem->createRenderTexture("GpuTestSsaoModulatedZero", size, size, colourOptions);
				auto modulatedOneOutput = renderSystem->createRenderTexture("GpuTestSsaoModulatedOne", size, size, colourOptions);
				renderSystem->pushRenderTarget(modulationZero); renderSystem->setViewport(0, 0, size, size);
				GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
				renderSystem->popRenderTarget();
				renderSystem->pushRenderTarget(modulationOne); renderSystem->setViewport(0, 0, size, size);
				GL_CHECK(glClearColor(1.0f, 1.0f, 1.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
				renderSystem->popRenderTarget();
				renderSystem->pushRenderTarget(modulatedZeroOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderSSAOCombine(dynamic_cast<Texture*>(sceneColour.get()), dynamic_cast<Texture*>(rawOutput.get()), dynamic_cast<Texture*>(modulationZero.get()));
				renderSystem->popRenderTarget();
				renderSystem->pushRenderTarget(modulatedOneOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderSSAOCombine(dynamic_cast<Texture*>(sceneColour.get()), dynamic_cast<Texture*>(rawOutput.get()), dynamic_cast<Texture*>(modulationOne.get()));
				renderSystem->popRenderTarget();

				ssao.radius = 0.0f;
				renderSystem->pushRenderTarget(zeroRadiusOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderSSAORaw(sceneDepthTexture, projection, glm::inverse(projection), ssao);
				renderSystem->popRenderTarget();
				GTAOOptions gtao; gtao.radius = 2.0f; gtao.intensity = 4.0f; gtao.thickness = 1.0f; gtao.falloffStart = 0.0f; gtao.falloffEnd = 1.0f; gtao.sliceCount = 8; gtao.stepsPerSlice = 8;
				renderSystem->pushRenderTarget(gtaoOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderGTAORaw(sceneDepthTexture, projection, glm::inverse(projection), gtao);
				renderSystem->popRenderTarget();
				gtao.normalSource = GTAONormalSource::Mrt;
				bool rejectedMissingMrtNormals = false;
				try { renderSystem->renderGTAORaw(sceneDepthTexture, projection, glm::inverse(projection), gtao); } catch (...) { rejectedMissingMrtNormals = true; }
				if (!rejectedMissingMrtNormals) return fail("GTAO MRT normal source accepted a missing normals texture");
				std::fill(depthValues.begin(), depthValues.end(), deviceDepth(-4.0f));
				GL_CHECK(glBindTexture(GL_TEXTURE_2D, sceneDepthTexture->getDepthTextureId()));
				GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)size, (GLsizei)size, GL_DEPTH_COMPONENT, GL_FLOAT, depthValues.data())); GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
				auto uploadNormal = [&](glm::vec3 normal)
				{
					normal = glm::normalize(normal); glm::vec2 oct = glm::vec2(normal) / (abs(normal.x) + abs(normal.y) + abs(normal.z));
					if (normal.z < 0.0f) oct = (glm::vec2(1.0f) - glm::abs(glm::vec2(oct.y, oct.x))) * glm::sign(oct);
					std::vector<float> encoded(size * size * 2); for (size_t pixel = 0; pixel < size * size; ++pixel) { encoded[pixel * 2] = oct.x * 0.5f + 0.5f; encoded[pixel * 2 + 1] = oct.y * 0.5f + 0.5f; }
					GL_CHECK(glBindTexture(GL_TEXTURE_2D, static_cast<RenderTexture*>(gtaoNormals.get())->getColourAttachmentId(0)));
					GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)size, (GLsizei)size, GL_RG, GL_FLOAT, encoded.data())); GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
				};
				uploadNormal(glm::vec3(0.0f, 0.0f, 1.0f));
				renderSystem->pushRenderTarget(gtaoMrtFlatOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderGTAORaw(sceneDepthTexture, dynamic_cast<Texture*>(gtaoNormals.get()), projection, glm::inverse(projection), gtao);
				renderSystem->popRenderTarget();
				uploadNormal(glm::vec3(0.8f, 0.0f, 0.6f));
				renderSystem->pushRenderTarget(gtaoMrtTiltedOutput); renderSystem->setViewport(0, 0, size, size);
				renderSystem->renderGTAORaw(sceneDepthTexture, dynamic_cast<Texture*>(gtaoNormals.get()), projection, glm::inverse(projection), gtao);
				renderSystem->popRenderTarget();
				renderSystem->popModelMatrix(); renderSystem->popCameraMatrix(); renderSystem->popProjectionMatrix();

				auto const occludedDisabled = readPixel(disabledOutput, 34, 32);
				auto const occludedEnabled = readPixel(enabledOutput, 34, 32);
				auto const openDisabled = readPixel(disabledOutput, 52, 32);
				auto const openEnabled = readPixel(enabledOutput, 52, 32);
				if (occludedEnabled[0] + 5 >= occludedDisabled[0])
					return fail("enabled SSAO did not measurably darken the occluded crease (disabled=" + std::to_string(occludedDisabled[0]) + ", enabled=" + std::to_string(occludedEnabled[0]) + ")");
				if (std::abs((int)openEnabled[0] - (int)openDisabled[0]) > 2)
					return fail("enabled SSAO meaningfully changed an open-area pixel (disabled=" + std::to_string(openDisabled[0]) + ", enabled=" + std::to_string(openEnabled[0]) + ")");
				auto const occludedModulatedZero = readPixel(modulatedZeroOutput, 34, 32);
				auto const occludedModulatedOne = readPixel(modulatedOneOutput, 34, 32);
				if (std::abs((int)occludedModulatedZero[0] - (int)occludedDisabled[0]) > 1)
					return fail("modulation=0 did not fall back to the unmodified scene colour (scene=" + std::to_string(occludedDisabled[0]) + ", modulated=" + std::to_string(occludedModulatedZero[0]) + ")");
				if (std::abs((int)occludedModulatedOne[0] - (int)occludedEnabled[0]) > 1)
					return fail("modulation=1 did not reproduce the plain (unmodulated) combine result (plain=" + std::to_string(occludedEnabled[0]) + ", modulated=" + std::to_string(occludedModulatedOne[0]) + ")");
				auto const zeroRadius = readPixel(zeroRadiusOutput, 34, 32);
				if (std::abs((int)zeroRadius[0] - 255) > 1)
					return fail("SSAO radius zero did not collapse to neutral ambient occlusion (got " + std::to_string(zeroRadius[0]) + ")");
				auto const gtaoOccluded = readPixel(gtaoOutput, 34, 32);
				auto const gtaoOpen = readPixel(gtaoOutput, 52, 32);
				if ((int)gtaoOpen[0] - (int)gtaoOccluded[0] < 3)
					return fail("GTAO did not detect the depth-fixture horizon (occluded=" + std::to_string(gtaoOccluded[0]) + ", open=" + std::to_string(gtaoOpen[0]) + ")");
				auto const gtaoMrtFlat = readPixel(gtaoMrtFlatOutput, 32, 32);
				auto const gtaoMrtTilted = readPixel(gtaoMrtTiltedOutput, 32, 32);
				if (std::abs((int)gtaoMrtFlat[0] - (int)gtaoMrtTilted[0]) < 3)
					return fail("GTAO MRT normals did not affect a constant-depth fixture (flat=" + std::to_string(gtaoMrtFlat[0]) + ", tilted=" + std::to_string(gtaoMrtTilted[0]) + ")");
				auto variance = [&](RenderTargetPtr const& target)
				{
					auto pixels = readPixels(target); double mean = 0.0, squared = 0.0; size_t count = 0;
					for (size_t y = 16; y < 48; ++y) for (size_t x = 34; x < 48; ++x)
					{
						double value = pixels[(y * size + x) * 4]; mean += value; squared += value * value; ++count;
					}
					mean /= count; return squared / count - mean * mean;
				};
				auto const rawVariance = variance(rawOutput), blurredVariance = variance(blurredOutput);
				if (!(blurredVariance < rawVariance * 0.9))
					return fail("SSAO blur did not measurably reduce raw AO variance (raw=" + std::to_string(rawVariance) + ", blurred=" + std::to_string(blurredVariance) + ")");
			}

			stage = "transient aliasing";
			RenderGraph aliasGraph;
			auto aliasFirst = aliasGraph.createImage("GpuTestAliasFirst", colour);
			auto aliasMiddle = aliasGraph.createImage("GpuTestAliasMiddle", colour);
			auto aliasLast = aliasGraph.createImage("GpuTestAliasLast", colour);
			// Identical to the others except for one sampler field, so the pairwise
			// plan-versus-allocator check below has a case where a loose predicate on
			// either side would actually disagree rather than being trivially met.
			GraphImageDesc aliasVariantDesc = colour; aliasVariantDesc.params.lodBias = 1.5f;
			auto aliasVariant = aliasGraph.createImage("GpuTestAliasVariant", aliasVariantDesc);
			auto aliasPass0 = aliasGraph.addPass("GpuTestAlias0", GraphPassType::Fullscreen);
			aliasFirst = aliasGraph.writeColour(aliasPass0, aliasFirst, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(1, 0, 0, 1));
			auto aliasPass1 = aliasGraph.addPass("GpuTestAlias1", GraphPassType::Fullscreen);
			aliasGraph.readSampled(aliasPass1, aliasFirst);
			aliasMiddle = aliasGraph.writeColour(aliasPass1, aliasMiddle, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 1, 0, 1));
			auto aliasPass2 = aliasGraph.addPass("GpuTestAlias2", GraphPassType::Fullscreen);
			aliasGraph.readSampled(aliasPass2, aliasMiddle);
			aliasLast = aliasGraph.writeColour(aliasPass2, aliasLast, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 0, 1, 1));
			auto aliasPass3 = aliasGraph.addPass("GpuTestAlias3", GraphPassType::Fullscreen);
			aliasGraph.readSampled(aliasPass3, aliasLast);
			aliasGraph.writeColour(aliasPass3, aliasVariant, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(1, 1, 0, 1));
			auto aliasPlan = aliasGraph.buildAllocationPlan({ 24, 24 });
			if (!aliasPlan.valid || aliasPlan.allocatedImages.size() != 4 || aliasPlan.estimatedPhysicalBytes == 0)
				return fail("allocation introspection report is incomplete");
			if (aliasPlan.allocatedImages[0].physicalAllocation != aliasPlan.allocatedImages[2].physicalAllocation ||
				aliasPlan.allocatedImages[0].physicalAllocation == aliasPlan.allocatedImages[1].physicalAllocation)
				return fail("allocation introspection alias groups are incorrect");
			RenderGraphTargets aliasTargets(renderSystem);
			aliasTargets.allocate(aliasPlan);
			if (aliasTargets.get(aliasFirst) != aliasTargets.get(aliasLast)) return fail("non-overlapping transient lifetimes were not aliased");
			if (aliasTargets.get(aliasFirst) == aliasTargets.get(aliasMiddle)) return fail("overlapping transient lifetimes were aliased");
			// The plan's grouping is what PipelineEditor reports; this is what the
			// renderer actually did. They are produced by separate code and only agree
			// because both now consult graphImagesCanAlias, so assert it directly over
			// every pair rather than trusting the shared call.
			for (size_t left = 0; left < aliasPlan.allocatedImages.size(); ++left)
				for (size_t right = left + 1; right < aliasPlan.allocatedImages.size(); ++right)
				{
					auto const& leftImage = aliasPlan.allocatedImages[left]; auto const& rightImage = aliasPlan.allocatedImages[right];
					bool const plannedTogether = leftImage.physicalAllocation == rightImage.physicalAllocation;
					if (plannedTogether != (aliasTargets.get(leftImage.image) == aliasTargets.get(rightImage.image)))
						return fail("planned allocation grouping disagrees with the allocated targets for '" + leftImage.debugName + "' and '" + rightImage.debugName + "'");
				}
			auto const rasterStateBefore = renderSystem->captureRasterState(1);
			auto const cacheStatsBefore = renderSystem->getRasterStateCacheStats();
			renderSystem->applyRasterState(rasterStateBefore, 1, 8, 8);
			auto const cacheStatsAfter = renderSystem->getRasterStateCacheStats();
			if (cacheStatsAfter.applied != cacheStatsBefore.applied || cacheStatsAfter.skipped <= cacheStatsBefore.skipped)
				return fail("reapplying cached raster state issued redundant OpenGL changes");
			GraphRasterState rasterState;
			rasterState.explicitState = true;
			rasterState.depthTest = false;
			rasterState.cullMode = GraphCullMode::None;
			rasterState.blend = true;
			rasterState.sourceColourBlend = GraphBlendFactor::SourceAlpha;
			rasterState.destinationColourBlend = GraphBlendFactor::OneMinusSourceAlpha;
			aliasGraph.setPassRasterState(aliasPass0, rasterState);
			bool observedRasterState = false;
			RenderGraphExecutor aliasExecutor(renderSystem);
			aliasExecutor.setPassCallback(aliasGraph, aliasPass0, [&](RenderGraphExecutionContext const&) { observedRasterState = glIsEnabled(GL_BLEND) && !glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_CULL_FACE); });
			aliasExecutor.setPassCallback(aliasGraph, aliasPass1, [](RenderGraphExecutionContext const&) {});
			aliasExecutor.setPassCallback(aliasGraph, aliasPass2, [](RenderGraphExecutionContext const&) {});
			aliasExecutor.setPassCallback(aliasGraph, aliasPass3, [](RenderGraphExecutionContext const&) {});
			aliasExecutor.execute(aliasGraph, aliasTargets, renderSystem->getCaps());
			if (!observedRasterState) return fail("explicit graph raster state was not applied");
			if (renderSystem->captureRasterState(1) != rasterStateBefore) return fail("graph execution did not restore the cached raster-state snapshot");
			if (aliasExecutor.getLastExecutionStats().size() != 4) return fail("per-pass execution statistics were not recorded");
			auto const& aliasOrder=aliasExecutor.getLastExecutionOrder();if(aliasOrder.size()!=4||aliasOrder[0].id!=aliasPass0.id||aliasOrder[1].id!=aliasPass1.id||aliasOrder[2].id!=aliasPass2.id||aliasOrder[3].id!=aliasPass3.id)return fail("executor did not retain its last successful compiled pass order");
			if (!nearColour(readFirstPixel(aliasTargets.get(aliasLast)), { 0, 0, 255, 255 })) return fail("aliased transient final output readback failed");

			stage = "depth-prepass raster handoff";
			{
				auto beforePrepass = renderSystem->captureRasterState(1);
				auto prepassInputState = beforePrepass;
				prepassInputState.depthCompare = GraphCompareOp::Less;
				renderSystem->applyRasterState(prepassInputState, 1, 1, 1);
				auto depthCamera = std::make_shared<Camera>(
					glm::vec3(0.0f, 0.0f, 5.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.0f);
				renderSystem->renderDepthPrepass({}, depthCamera, 1);
				auto afterPrepass = renderSystem->captureRasterState(1);
				if (afterPrepass.depthCompare != GraphCompareOp::LessEqual)
					return fail("depth prepass did not hand the material pass a less-equal depth comparison");
				renderSystem->applyRasterState(beforePrepass, 1, 1, 1);
			}

			stage = "raster-state cache CPU benchmark";
			for (size_t passCount : { 10u, 50u, 100u })
			{
				RenderGraph benchmarkGraph;
				GraphImageDesc benchmarkImageDesc;
				benchmarkImageDesc.absoluteSize = { 1, 1 };
				benchmarkImageDesc.usage = GraphImageUsage::ColourAttachment;
				std::vector<GraphPassHandle> benchmarkPasses;
				for (size_t index = 0; index < passCount; ++index)
				{
					auto image = benchmarkGraph.createImage("RasterStateBenchmark.Image." + std::to_string(index), benchmarkImageDesc);
					auto pass = benchmarkGraph.addPass("RasterStateBenchmark.Pass." + std::to_string(index), GraphPassType::Fullscreen);
					benchmarkGraph.writeColour(pass, image, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
					benchmarkGraph.setPassRasterState(pass, rasterState);
					benchmarkPasses.push_back(pass);
				}
				auto benchmarkPlan = benchmarkGraph.buildAllocationPlan({ 1, 1 });
				if (!benchmarkPlan.valid) return fail("raster-state benchmark graph did not compile");
				RenderGraphTargets benchmarkTargets(renderSystem);
				benchmarkTargets.allocate(benchmarkPlan);
				RenderGraphExecutor benchmarkExecutor(renderSystem);
				for (auto pass : benchmarkPasses) benchmarkExecutor.setPassCallback(benchmarkGraph, pass, [](RenderGraphExecutionContext const&) {});
				benchmarkExecutor.execute(benchmarkGraph, benchmarkTargets, renderSystem->getCaps());
				auto measure = [&]()
				{
					GL_CHECK(glFinish());
					auto const begin = std::chrono::steady_clock::now();
					for (size_t frame = 0; frame < 10; ++frame) benchmarkExecutor.execute(benchmarkGraph, benchmarkTargets, renderSystem->getCaps());
					auto const end = std::chrono::steady_clock::now();
					return std::chrono::duration<double, std::milli>(end - begin).count() / 10.0;
				};
				auto const cachedMilliseconds = measure();
				for (auto pass : benchmarkPasses)
					benchmarkExecutor.setPassCallback(benchmarkGraph, pass, [](RenderGraphExecutionContext const&)
					{
						GLint values[4]{}; GLboolean mask = GL_TRUE;
						(void)glIsEnabled(GL_DEPTH_TEST); (void)glIsEnabled(GL_CULL_FACE); (void)glIsEnabled(GL_BLEND);
						(void)glIsEnabled(GL_MULTISAMPLE); (void)glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE); (void)glIsEnabled(GL_SCISSOR_TEST);
						GL_CHECK(glGetBooleanv(GL_DEPTH_WRITEMASK, &mask)); GL_CHECK(glGetIntegerv(GL_DEPTH_FUNC, values));
						GL_CHECK(glGetIntegerv(GL_CULL_FACE_MODE, values)); GL_CHECK(glGetIntegerv(GL_FRONT_FACE, values)); GL_CHECK(glGetIntegerv(GL_POLYGON_MODE, values));
						GL_CHECK(glGetIntegerv(GL_BLEND_EQUATION_RGB, values)); GL_CHECK(glGetIntegerv(GL_BLEND_EQUATION_ALPHA, values));
						GL_CHECK(glGetIntegerv(GL_BLEND_SRC_RGB, values)); GL_CHECK(glGetIntegerv(GL_BLEND_DST_RGB, values));
						GL_CHECK(glGetIntegerv(GL_BLEND_SRC_ALPHA, values)); GL_CHECK(glGetIntegerv(GL_BLEND_DST_ALPHA, values)); GL_CHECK(glGetIntegerv(GL_SCISSOR_BOX, values));
					});
				auto const queriedMilliseconds = measure();
				renderSystem->infoMessage("Raster-state CPU benchmark (" + std::to_string(passCount) + " lightweight passes): cached=" +
					std::to_string(cachedMilliseconds) + " ms/frame, query baseline=" + std::to_string(queriedMilliseconds) + " ms/frame.");
			}

			stage = "fullscreen sampler routing";
			// renderGraphFullscreen used to bind each texture to the binding's own
			// position and point the sampler at it with getUniformId, which always
			// returns -1 for a sampler. Units therefore came from Program::bind, in
			// the shader's declaration order, so bindings authored in a different
			// order landed on the wrong samplers without a word. Bind them reversed
			// and check each texture arrives where its name says it should.
			{
				mesh::MeshSpecification samplerMeshSpec;
				auto samplerLayout = samplerMeshSpec.createVertexBufferAttributeLayout(false);
				samplerLayout->createAttribute(mesh::Vertex::Component::Position2, mesh::Vertex::DataType::Float, false);
				samplerLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mesh::Vertex::DataType::Float, false);
				auto samplerParser = std::make_shared<program::Parser>();
				samplerParser->setMeshSpecification(samplerMeshSpec);
				samplerParser->setVertexSource(VertexShaderFullscreenTemplate);
				samplerParser->setFragmentSource(R"(
@@Version

@@Texture(sampler2D GPU_TEST_FIRST);
@@Texture(sampler2D GPU_TEST_SECOND);

void main()
{
    @Out(vec4 COLOUR) = vec4(texture(@Texture(GPU_TEST_FIRST), @In(TEXCOORDS)).r,
        texture(@Texture(GPU_TEST_SECOND), @In(TEXCOORDS)).r, 0.0, 1.0);
}
)");
				auto samplerStream = new ProgrammaticProgramStream(renderSystem->getResourceManager());
				samplerStream->setParser(samplerParser);
				auto samplerProgram = renderSystem->getResourceManager()->declareResource("GpuTestSamplerRouting.Program", ResourceStreamPtr(samplerStream)).first;
				auto* reflectedSamplerProgram = static_cast<Program*>(samplerProgram.get());
				if (reflectedSamplerProgram->getViewPosId() != -1 || reflectedSamplerProgram->getModelMatrixId() != -1 ||
					reflectedSamplerProgram->getModelCameraProjectionMatrixId() != -1 || reflectedSamplerProgram->getNormalMatrixId() != -1 ||
					reflectedSamplerProgram->getHalfWindowSizeId() != -1 || reflectedSamplerProgram->getPointSizeId() != -1)
					return fail("unloaded program reflection locations were not initialized to -1");
				auto const unloadedOutputRevision = reflectedSamplerProgram->getFragmentOutputRevision();
				samplerProgram->create(); samplerProgram->load();
				auto const loadedOutputRevision = reflectedSamplerProgram->getFragmentOutputRevision();
				if (loadedOutputRevision <= unloadedOutputRevision) return fail("program load did not advance its fragment-output revision");
				std::string outputDiagnostic;
				if (!reflectedSamplerProgram->validateFragmentOutputLocations(1, outputDiagnostic)) return fail("linked sampler program lost fragment output zero");
				if ((GLEW_VERSION_4_3 || GLEW_ARB_program_interface_query) && reflectedSamplerProgram->validateFragmentOutputLocations(2, outputDiagnostic))
					return fail("cached fragment-output reflection accepted a missing MRT output");
				samplerProgram->unload();
				auto const releasedOutputRevision = reflectedSamplerProgram->getFragmentOutputRevision();
				if (releasedOutputRevision <= loadedOutputRevision) return fail("program unload did not invalidate its fragment-output revision");
				if (samplerProgram->getId() != 0 || reflectedSamplerProgram->getNumSamplers() != 0 || !reflectedSamplerProgram->getUniformNames().empty() ||
					reflectedSamplerProgram->getViewPosId() != -1 || reflectedSamplerProgram->getModelMatrixId() != -1 ||
					reflectedSamplerProgram->getModelCameraProjectionMatrixId() != -1 || reflectedSamplerProgram->getNormalMatrixId() != -1 ||
					reflectedSamplerProgram->getHalfWindowSizeId() != -1 || reflectedSamplerProgram->getPointSizeId() != -1)
					return fail("program unload retained stale reflection state");
				samplerProgram->load();
				if (reflectedSamplerProgram->getFragmentOutputRevision() <= releasedOutputRevision) return fail("program reload did not publish a new fragment-output revision");
				// Distinguishable in the red channel only, which is what the shader reads.
				RenderTextureOptions sourceOptions; sourceOptions.params.minFilter = GL_NEAREST; sourceOptions.params.magFilter = GL_NEAREST; sourceOptions.params.wrap = GL_CLAMP_TO_EDGE;
				auto firstSource = renderSystem->createRenderTexture("GpuTestSamplerFirst", 4, 4, sourceOptions);
				auto secondSource = renderSystem->createRenderTexture("GpuTestSamplerSecond", 4, 4, sourceOptions);
				auto routingTarget = renderSystem->createRenderTexture("GpuTestSamplerRouting", 4, 4, sourceOptions);
				for (auto const& [source, red] : { std::pair<RenderTargetPtr, float>{ firstSource, 1.0f }, std::pair<RenderTargetPtr, float>{ secondSource, 0.25f } })
				{
					renderSystem->pushRenderTarget(source); renderSystem->setViewport(0, 0, 4, 4);
					GL_CHECK(glClearColor(red, 0.0f, 0.0f, 1.0f)); GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));
					renderSystem->popRenderTarget();
				}
				auto renderRouting = [&](std::vector<std::pair<std::string, Texture*>> const& bindings)
				{
					renderSystem->pushRenderTarget(routingTarget); renderSystem->setViewport(0, 0, 4, 4);
					renderSystem->pushModelMatrix(); renderSystem->pushCameraMatrix(); renderSystem->pushProjectionMatrix();
					renderSystem->setProjection2dOrthographic(); renderSystem->resetTransform();
					renderSystem->scaleTransform2d(glm::vec2(4.0f / (float)renderSystem->getWindowWidth(), 4.0f / (float)renderSystem->getWindowHeight()));
					try { renderSystem->renderGraphFullscreen(samplerProgram, bindings, UniformCollection()); }
					catch (...) { renderSystem->popModelMatrix(); renderSystem->popCameraMatrix(); renderSystem->popProjectionMatrix(); renderSystem->popRenderTarget(); throw; }
					renderSystem->popModelMatrix(); renderSystem->popCameraMatrix(); renderSystem->popProjectionMatrix(); renderSystem->popRenderTarget();
				};
				auto* firstTexture = dynamic_cast<Texture*>(firstSource.get());
				auto* secondTexture = dynamic_cast<Texture*>(secondSource.get());
				renderRouting({ { "GPU_TEST_SECOND", secondTexture }, { "GPU_TEST_FIRST", firstTexture } });
				auto const routed = readFirstPixel(routingTarget);
				if (!nearColour(routed, { 255, 64, 0, 255 }))
					return fail("fullscreen sampler bindings were routed by declaration order rather than by name (got r=" + std::to_string(routed[0]) + " g=" + std::to_string(routed[1]) + ", expected r=255 g=64)");
				bool rejectedUnknownSampler = false;
				try { renderRouting({ { "GPU_TEST_MISSING", firstTexture } }); } catch (...) { rejectedUnknownSampler = true; }
				if (!rejectedUnknownSampler) return fail("a fullscreen pass binding an undeclared sampler was accepted");

				auto invalidParser = std::make_shared<program::Parser>();
				invalidParser->setMeshSpecification(samplerMeshSpec);
				invalidParser->setVertexSource(VertexShaderFullscreenTemplate);
				invalidParser->setFragmentSource("@@Version\nvoid main() { this is invalid GLSL; }");
				auto invalidStream = std::make_shared<ProgrammaticProgramStream>(renderSystem->getResourceManager());
				invalidStream->setParser(invalidParser);
				auto invalidProgramResource = renderSystem->getResourceManager()->declareResource("GpuTestTransactionalInvalid.Program", invalidStream).first;
				auto* invalidProgram = static_cast<Program*>(invalidProgramResource.get());
				bool invalidProgramRejected = false;
				try { invalidProgramResource->load(); } catch (...) { invalidProgramRejected = true; }
				if (!invalidProgramRejected || invalidProgramResource->getId() != 0 || invalidProgram->getLiveIdCount() != 0 ||
					invalidProgram->getNumSamplers() != 0 || !invalidProgram->getUniformNames().empty() || invalidProgram->getViewPosId() != -1)
					return fail("failed program load published a GL name or partial reflection state");
				renderSystem->getResourceManager()->deleteResource(invalidProgramResource->getName());
			}

			stage = "dontCare store on the default framebuffer";
			// The default framebuffer names its buffers GL_COLOR/GL_DEPTH, so the
			// attachment enums used for an FBO are GL_INVALID_ENUM there. GL_CHECK
			// inside the executor turns that into a throw, so reaching the end of this
			// block is the assertion.
			{
				RenderGraph screenGraph;
				GraphImageDesc screenDesc; screenDesc.format = GraphImageFormat::Rgba8;
				screenDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Presentation;
				screenDesc.external = true; screenDesc.transient = false;
				auto screenImage = screenGraph.createImage("GpuTestScreen", screenDesc);
				screenGraph.setImageImportName(screenImage, "screen");
				auto screenPass = screenGraph.addPass("GpuTestScreenPass", GraphPassType::Fullscreen);
				auto screenWritten = screenGraph.writeColour(screenPass, screenImage, GraphLoadOp::Clear, GraphStoreOp::DontCare, glm::vec4(0, 0, 0, 1));
				auto screenPlan = screenGraph.buildAllocationPlan({ 16, 16 });
				if (!screenPlan.valid) return fail("default-framebuffer graph did not compile");
				RenderGraphTargets screenTargets(renderSystem);
				screenTargets.allocate(screenPlan);
				screenTargets.bindImported(screenWritten, renderSystem->getScreenRenderTarget());
				RenderGraphExecutor screenExecutor(renderSystem);
				screenExecutor.setPassCallback(screenGraph, screenPass, [](RenderGraphExecutionContext const&) {});
				screenExecutor.execute(screenGraph, screenTargets, renderSystem->getCaps());
			}

			stage = "pass identity across topology edits";
			// GraphPassHandle::id is a positional index, and removePass renumbers it.
			// Executor state used to be keyed on that index, so after an edit a pass
			// silently inherited whichever entry now sat at its slot -- for a stateful
			// scene pass that means running on another pass's history.
			RenderGraph identityGraph;
			RenderGraphExecutor identityExecutor(renderSystem);
			std::string identityMismatch;
			auto identityExpect = [&](std::string const& expected)
			{
				return [&identityMismatch, expected](RenderGraphExecutionContext const& context)
				{
					if (context.getPass().name != expected && identityMismatch.empty())
						identityMismatch = "the callback registered for '" + expected + "' ran as '" + context.getPass().name + "'";
				};
			};
			std::array<GraphPassHandle, 3> identityPasses{};
			for (uint32_t index = 0; index < 3; ++index)
			{
				auto const name = "GpuTestIdentity" + std::to_string(index);
				auto image = identityGraph.createImage(name + ".Image", colour);
				identityPasses[index] = identityGraph.addPass(name, GraphPassType::Fullscreen);
				identityGraph.writeColour(identityPasses[index], image, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 0, 0, 1));
				identityExecutor.setPassCallback(identityGraph, identityPasses[index], identityExpect(name));
			}
			identityGraph.removePass(identityPasses[0]);
			auto identityPlan = identityGraph.buildAllocationPlan({ 8, 8 });
			if (!identityPlan.valid) return fail("pass identity graph did not compile");
			RenderGraphTargets identityTargets(renderSystem);
			identityTargets.allocate(identityPlan);
			identityExecutor.execute(identityGraph, identityTargets, renderSystem->getCaps());
			if (!identityMismatch.empty()) return fail("render graph executor state did not follow its pass across a topology edit: " + identityMismatch);

			stage = "compiled process-flow order";
			static_assert(std::is_const_v<RenderPipelineFlowSnapshotPtr::element_type>);
			if(std::string(renderFlowEventKindName(RenderFlowEventKind::BatchSubmission))!="batch submission"||std::string(renderFlowEventKindName(RenderFlowEventKind::Presentation))!="presentation"||std::string(renderFlowEventKindName(RenderFlowEventKind::GlState))!="GL state"||renderFlowPassRenderDocLabel({3},"Lighting",GraphPassType::Fullscreen)!="RenderGraph Pass 3: Lighting [Fullscreen]"||std::string(renderFlowGeometryRenderDocLabel(false))!="Draw: Opaque + Masked Geometry"||renderFlowOutputRenderDocLabel("Main",RenderFlowEventKind::Taa)!="Output Main: TAA")return fail("render-flow/RenderDoc naming contract failed");
			RenderPipelineFlowSnapshot recorderSnapshot;renderSystem->beginRenderFlowCapture(&recorderSnapshot);GraphPassHandle recorderPass{7};renderSystem->beginRenderFlowPass(recorderPass,"RecorderPass");RenderBatchSubmission repeated;repeated.meshName="Repeated";repeated.materialName="Material";repeated.programName="Program";repeated.count=3;renderSystem->recordRenderFlowBatch(repeated);renderSystem->recordRenderFlowBatch(repeated);renderSystem->endRenderFlowPass(recorderPass,"RecorderPass");if(!renderSystem->endRenderFlowCapture()||recorderSnapshot.batches.size()!=2||recorderSnapshot.batches[0].sequence==recorderSnapshot.batches[1].sequence||recorderSnapshot.physicalEvents.size()!=4||recorderSnapshot.physicalEvents[0].kind!=RenderFlowEventKind::PassBegin||recorderSnapshot.physicalEvents[1].kind!=RenderFlowEventKind::BatchSubmission||recorderSnapshot.physicalEvents[2].kind!=RenderFlowEventKind::BatchSubmission||recorderSnapshot.physicalEvents[3].kind!=RenderFlowEventKind::PassEnd)return fail("exact duplicate batch recorder contract failed");renderSystem->recordRenderFlowEvent(RenderFlowEventKind::Presentation,"DisabledRecorder");if(recorderSnapshot.physicalEvents.size()!=4)return fail("disabled telemetry retained an event");RenderPipelineFlowSnapshot stateSnapshot;renderSystem->beginRenderFlowCapture(&stateSnapshot);renderSystem->beginRenderFlowPass(recorderPass,"StateRecorder");renderSystem->recordRenderFlowStateChanges({"Program: StateProgram","Cull face: back"});renderSystem->endRenderFlowPass(recorderPass,"StateRecorder");if(!renderSystem->endRenderFlowCapture()||stateSnapshot.physicalEvents.size()!=3||stateSnapshot.physicalEvents[1].kind!=RenderFlowEventKind::GlState||stateSnapshot.physicalEvents[1].stateChanges.size()!=2)return fail("GL state flow recorder contract failed");
			RenderGraph orderGraph;auto orderFirstImage=orderGraph.createImage("GpuTestFlowFirst",colour);auto orderDisabledImage=orderGraph.createImage("GpuTestFlowDisabled",colour);auto orderLastImage=orderGraph.createImage("GpuTestFlowLast",colour);auto authoredFirst=orderGraph.addPass("GpuTestFlowFirst",GraphPassType::Fullscreen);auto authoredDisabled=orderGraph.addPass("GpuTestFlowDisabled",GraphPassType::Fullscreen);auto authoredLast=orderGraph.addPass("GpuTestFlowLast",GraphPassType::Fullscreen);orderFirstImage=orderGraph.writeColour(authoredFirst,orderFirstImage,GraphLoadOp::Clear,GraphStoreOp::Store,glm::vec4(1,0,0,1));orderDisabledImage=orderGraph.writeColour(authoredDisabled,orderDisabledImage,GraphLoadOp::Clear,GraphStoreOp::Store,glm::vec4(0,0,1,1));orderLastImage=orderGraph.writeColour(authoredLast,orderLastImage,GraphLoadOp::Clear,GraphStoreOp::Store,glm::vec4(0,1,0,1));orderGraph.setPassEnabled(authoredDisabled,false);auto orderPlan=orderGraph.buildAllocationPlan({8,8});if(!orderPlan.valid)return fail("process-flow order graph did not compile"+(orderPlan.diagnostics.empty()?std::string():": "+orderPlan.diagnostics.front()));RenderGraphTargets orderTargets(renderSystem);orderTargets.allocate(orderPlan);RenderGraphExecutor orderExecutor(renderSystem);orderExecutor.setPassCallback(orderGraph, authoredFirst,[](RenderGraphExecutionContext const&){});orderExecutor.setPassCallback(orderGraph, authoredLast,[](RenderGraphExecutionContext const&){});orderExecutor.execute(orderGraph,orderTargets,renderSystem->getCaps());auto const& actualOrder=orderExecutor.getLastExecutionOrder();if(actualOrder.size()!=2||actualOrder[0].id!=authoredFirst.id||actualOrder[1].id!=authoredLast.id)return fail("actual process-flow order did not match enabled compiled pass order");

			stage = "mip chain";
			GraphImageDesc mipColour = colour;
			mipColour.mipLevels = 3;
			mipColour.params.minFilter = GL_LINEAR_MIPMAP_LINEAR;
			RenderGraph mipGraph;
			auto mipImage = mipGraph.createImage("GpuTestMipChain", mipColour);
			auto mipViewOutput = mipGraph.createImage("GpuTestMipViewOutput", colour);
			auto mipPass = mipGraph.addPass("GpuTestMipWrite", GraphPassType::Fullscreen);
			mipImage = mipGraph.writeColour(mipPass, mipImage, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(1, 0, 1, 1));
			auto mipViewPass = mipGraph.addPass("GpuTestMipView", GraphPassType::Fullscreen);
			mipGraph.bindSampler(mipViewPass, "MIP_INPUT", mipImage, 2);
			mipViewOutput = mipGraph.writeColour(mipViewPass, mipViewOutput, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0));
			RenderGraphTargets mipTargets(renderSystem);
			mipTargets.allocate(mipGraph.buildAllocationPlan({ 16, 8 }));
			RenderGraphExecutor mipExecutor(renderSystem);
			mipExecutor.setPassCallback(mipGraph, mipPass, [](RenderGraphExecutionContext const&) {});
			bool observedMipView = false;
			mipExecutor.setPassCallback(mipGraph, mipViewPass, [&](RenderGraphExecutionContext const& context)
			{
				auto texture = static_cast<RenderTexture*>(context.getImage(mipImage).get());
				GLint base = -1, maximum = -1;
				GL_CHECK(glBindTexture(GL_TEXTURE_2D, texture->getColourAttachmentId(0)));
				GL_CHECK(glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &base));
				GL_CHECK(glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maximum));
				observedMipView = base == 2 && maximum == 2;
			});
			mipExecutor.execute(mipGraph, mipTargets, renderSystem->getCaps());
			if (!observedMipView) return fail("explicit sampled mip view was not applied");
			if (!nearColour(readFirstPixel(mipTargets.get(mipImage), 2), { 255, 0, 255, 255 })) return fail("generated mip level readback failed");
			RenderTextureOptions diagnosticOptions;
			auto diagnosticTarget = renderSystem->createRenderTexture("GpuTestDiagnostic", 8, 8, diagnosticOptions);
			RenderSystem::TextureDiagnosticOptions inspectOptions;
			inspectOptions.mode = RenderSystem::TextureDiagnosticMode::Green;
			inspectOptions.mipLevel = 2;
			renderSystem->renderTextureDiagnostic(static_cast<RenderTexture*>(mipTargets.get(mipImage).get()), diagnosticTarget, inspectOptions);
			auto diagnosticPixel = readFirstPixel(diagnosticTarget);
			if (!nearColour(diagnosticPixel, { 0, 0, 0, 255 })) return fail("graph image mip/channel diagnostic visualization failed (pixel=" + std::to_string(diagnosticPixel[0]) + "," + std::to_string(diagnosticPixel[1]) + "," + std::to_string(diagnosticPixel[2]) + "," + std::to_string(diagnosticPixel[3]) + ")");
			GL_CHECK(glFinish());
			mipExecutor.execute(mipGraph, mipTargets, renderSystem->getCaps());
			if (mipExecutor.getLastExecutionStats().empty() || !mipExecutor.getLastExecutionStats().front().gpuTimingAvailable) return fail("asynchronous per-pass GPU timing was not collected");

			stage = "explicit mip attachment";
			RenderGraph explicitMipGraph;
			auto explicitMip = explicitMipGraph.createImage("GpuTestExplicitMip", mipColour);
			auto explicitMipPass = explicitMipGraph.addPass("GpuTestExplicitMipWrite", GraphPassType::Fullscreen);
			explicitMip = explicitMipGraph.writeColour(explicitMipPass, explicitMip, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 1, 1, 1), 2);
			RenderGraphTargets explicitMipTargets(renderSystem);
			auto explicitMipPlan = explicitMipGraph.buildAllocationPlan({ 16, 8 });
			explicitMipTargets.allocate(explicitMipPlan);
			auto const explicitMipGeneration = explicitMipTargets.getGeneration();
			explicitMipTargets.allocate(explicitMipPlan);
			if (explicitMipTargets.getGeneration() != explicitMipGeneration) return fail("unchanged graph allocation replaced its attachment generation");
			RenderGraphExecutor explicitMipExecutor(renderSystem);
			explicitMipExecutor.setPassCallback(explicitMipGraph, explicitMipPass, [](RenderGraphExecutionContext const&) {});
			explicitMipExecutor.execute(explicitMipGraph, explicitMipTargets, renderSystem->getCaps());
			auto const firstFramebufferCache = explicitMipExecutor.getFramebufferCacheStats();
			if (firstFramebufferCache.misses != 1 || firstFramebufferCache.entries != 1) return fail("explicit mip framebuffer view was not entered in the cache");
			explicitMipExecutor.execute(explicitMipGraph, explicitMipTargets, renderSystem->getCaps());
			if (explicitMipExecutor.getFramebufferCacheStats().hits <= firstFramebufferCache.hits) return fail("unchanged explicit mip framebuffer view did not hit the cache");
			if (!nearColour(readFirstPixel(explicitMipTargets.get(explicitMip), 2), { 0, 255, 255, 255 })) return fail("explicit mip attachment readback failed");
			auto const cacheBeforeAttachmentReplacement = explicitMipExecutor.getFramebufferCacheStats();
			explicitMipTargets.allocate(explicitMipGraph.buildAllocationPlan({ 20, 10 }));
			if (explicitMipTargets.getGeneration() == explicitMipGeneration) return fail("resized graph allocation retained its attachment generation");
			explicitMipExecutor.execute(explicitMipGraph, explicitMipTargets, renderSystem->getCaps());
			auto const cacheAfterAttachmentReplacement = explicitMipExecutor.getFramebufferCacheStats();
			if (cacheAfterAttachmentReplacement.invalidations <= cacheBeforeAttachmentReplacement.invalidations || cacheAfterAttachmentReplacement.misses <= cacheBeforeAttachmentReplacement.misses)
				return fail("replaced graph attachments did not invalidate cached framebuffer views");
			RenderTextureOptions importGenerationOptions;
			auto importGenerationA = renderSystem->createRenderTexture("GpuTestImportGenerationA", 4, 4, importGenerationOptions);
			auto importGenerationB = renderSystem->createRenderTexture("GpuTestImportGenerationB", 4, 4, importGenerationOptions);
			RenderGraphTargets importGenerationTargets(renderSystem);
			importGenerationTargets.bindImported({ 0, 0 }, importGenerationA);
			auto const firstImportGeneration = importGenerationTargets.getGeneration();
			importGenerationTargets.bindImported({ 0, 0 }, importGenerationA);
			if (importGenerationTargets.getGeneration() != firstImportGeneration) return fail("rebinding an unchanged graph import advanced its attachment generation");
			if (!importGenerationA->resize(5, 4)) return fail("graph import resize failed");
			importGenerationTargets.bindImported({ 0, 0 }, importGenerationA);
			auto const resizedImportGeneration = importGenerationTargets.getGeneration();
			if (resizedImportGeneration == firstImportGeneration) return fail("resized graph import did not advance its attachment generation");
			importGenerationTargets.bindImported({ 0, 0 }, importGenerationB);
			if (importGenerationTargets.getGeneration() == resizedImportGeneration) return fail("replacing a graph import did not advance its attachment generation");

			mipColour.mipLevels = 8;
			RenderGraph invalidMipGraph;
			auto invalidMip = invalidMipGraph.createImage("GpuTestInvalidMipChain", mipColour);
			auto invalidMipPass = invalidMipGraph.addPass("GpuTestInvalidMipWrite", GraphPassType::Fullscreen);
			invalidMipGraph.writeColour(invalidMipPass, invalidMip);
			if (invalidMipGraph.buildAllocationPlan({ 8, 8 }).valid) return fail("oversized mip chain was accepted");

			stage = "water SSR graph plumbing";
			{
				// The exact topology PbrPipelineMrt/Full author: an opaque scene pass
				// storing depth, a mip-chained copy of its colour, and a water pass
				// that samples both and draws back over the scene colour it was
				// copied from. The point of the test is that this shape allocates,
				// validates against the built-in factory contracts, keeps its mip
				// chain across a resize, and executes without GL errors.
				GraphImageDesc waterHdr;
				waterHdr.format = GraphImageFormat::Rgba16f;
				waterHdr.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
				GraphImageDesc waterResolved = waterHdr;
				waterResolved.mipLevels = 3;
				waterResolved.params.minFilter = GL_LINEAR_MIPMAP_LINEAR;
				GraphImageDesc waterDepth;
				waterDepth.format = GraphImageFormat::Depth24;
				waterDepth.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled;

				RenderGraph waterGraph;
				auto sceneHdr = waterGraph.createImage("GpuTestWaterSceneHdr", waterHdr);
				auto resolved = waterGraph.createImage("GpuTestWaterResolved", waterResolved);
				auto sceneDepth = waterGraph.createImage("GpuTestWaterDepth", waterDepth);
				auto opaquePass = waterGraph.addPass("GpuTestWaterOpaque", GraphPassType::Scene);
				waterGraph.setPassCallbackFactory(opaquePass, "MPP.PbrScene");
				sceneHdr = waterGraph.writeColour(opaquePass, sceneHdr, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 0, 0, 1));
				sceneDepth = waterGraph.writeDepth(opaquePass, sceneDepth, GraphLoadOp::Clear, GraphStoreOp::Store);
				auto copyPass = waterGraph.addPass("GpuTestWaterCopy", GraphPassType::Fullscreen);
				waterGraph.setPassCallbackFactory(copyPass, "MPP.SceneColourCopy");
				waterGraph.bindSampler(copyPass, "TEX1", sceneHdr);
				resolved = waterGraph.writeColour(copyPass, resolved, GraphLoadOp::DontCare, GraphStoreOp::Store);
				auto waterPass = waterGraph.addPass("GpuTestWaterScene", GraphPassType::Scene);
				waterGraph.setPassCallbackFactory(waterPass, "MPP.WaterScene");
				waterGraph.bindSampler(waterPass, "PBR_SCENE_COLOUR_RESOLVED", resolved);
				waterGraph.bindSampler(waterPass, "PBR_SCENE_DEPTH", sceneDepth);
				sceneHdr = waterGraph.writeColour(waterPass, sceneHdr, GraphLoadOp::Load, GraphStoreOp::Store);
				GraphRasterState waterRaster;
				waterRaster.explicitState = true;
				waterRaster.depthTest = false;
				waterRaster.depthWrite = false;
				waterGraph.setPassRasterState(waterPass, waterRaster);

				auto waterCompiled = waterGraph.compile(renderSystem->getCaps());
				if (!waterCompiled.valid)
					return fail("water SSR graph did not compile" + (waterCompiled.diagnostics.empty() ? std::string() : ": " + waterCompiled.diagnostics.front()));

				RenderGraphPassFactoryRegistry waterRegistry;
				registerBuiltInRenderGraphPasses(waterRegistry);
				auto const waterDiagnostics = waterRegistry.validate(waterGraph);
				if (waterDiagnostics.hasErrors())
					return fail("water SSR graph failed its pass factory contracts: " + waterDiagnostics.getDiagnostics().front().message);

				// Executed through explicit callbacks: the built-in scene passes need a
				// live frame context, which this GPU harness has no scene for. The
				// attachment, sampler-view and clear work under test is the executor's.
				RenderGraphExecutor waterExecutor(renderSystem);
				for (auto pass : { opaquePass, copyPass, waterPass })
					waterExecutor.setPassCallback(waterGraph, pass, [](RenderGraphExecutionContext const&) {});
				for (auto const& viewport : { glm::uvec2(64, 48), glm::uvec2(96, 32) })
				{
					auto waterPlan = waterGraph.buildAllocationPlan(viewport);
					if (!waterPlan.valid)
						return fail("water SSR graph allocation failed" + (waterPlan.diagnostics.empty() ? std::string() : ": " + waterPlan.diagnostics.front()));
					RenderGraphTargets waterTargets(renderSystem);
					waterTargets.allocate(waterPlan);
					waterExecutor.execute(waterGraph, waterTargets, renderSystem->getCaps());
					auto resolvedTarget = dynamic_cast<RenderTexture*>(waterTargets.get(resolved).get());
					if (!resolvedTarget) return fail("resolved scene colour has no render texture");
					if (resolvedTarget->getMipLevels() != 3)
						return fail("resolved scene colour lost its mip chain across a resize; it reports " + std::to_string(resolvedTarget->getMipLevels()) + " levels");
					if (resolvedTarget->getWidth() != viewport.x || resolvedTarget->getHeight() != viewport.y)
						return fail("resolved scene colour did not follow the viewport");
				}
			}

			stage = "particle simulation frame policy";
			{
				if (clampParticleDeltaSeconds(3.0f) != MaximumParticleDeltaSeconds)
					return fail("particle simulation did not clamp a multi-second frame stall");
				ParticleSpawnAccumulator lowRate;
				uint32_t const expected[]{ 0u, 0u, 0u, 1u };
				for (uint32_t count : expected)
					if (lowRate.accumulate(0.5f, 1.0f, 0.5f) != count)
						return fail("fractional particle spawn accumulation quantised a low-rate emitter");
			}

			stage = "particle vertical slice";
			{
				// The thin end-to-end path: a compute dispatch and an attribute-less
				// indirect draw of GPU-written arguments, surviving GL_CHECK, the debug
				// context and graph state management, in both a generated graph and an
				// authored template. Unavailable compute is not fatal anywhere here.
				auto const& particleCaps = renderSystem->getCaps();
				if (particleCaps.supportsCompute &&
					(particleCaps.maxComputeWorkGroupInvocations == 0 || particleCaps.maxComputeWorkGroupCount[0] == 0 ||
						particleCaps.maxComputeWorkGroupSize[0] == 0 || particleCaps.maxShaderStorageBlockSize == 0 ||
						particleCaps.maxShaderStorageBufferBindings == 0))
					return fail("compute support was reported without querying its limits");

				auto particleScene = renderSystem->createScene("Default");
				particleScene->setViewport(0, 0, 64, 64);
				particleScene->setClearColour(Colour(0.0f, 0.0f, 0.0f, 1.0f));
				auto particleCamera = std::make_shared<Camera>(glm::vec3(0.0f, 0.0f, 6.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.0f);

				// One template per billboard mode plus alpha and weighted-OIT templates
				// exercises every authored command span. The burst is submitted by the first graph.
				std::array<ParticleEmitterTemplate, 8> particleTemplates{};
				for (uint32_t index = 0; index < particleTemplates.size(); ++index)
				{
					auto& emitter = particleTemplates[index];
					emitter.simulation.emissionState = { 1u, 1u, 1u, 0u };
					emitter.simulation.shapeSeedModulesBudget[3] = 1u;
					emitter.simulation.lifetimeSizeRanges = { 10.0f, 10.0f, 0.35f, 0.35f };
					emitter.simulation.initialVelocityMin = emitter.simulation.initialVelocityMax = { 0.0f, 1.0f, 0.0f, 0.0f };
					emitter.localTransform = glm::translate(glm::mat4(1.0f), { (float(index) - 3.5f) * 0.5f, 0.0f, 0.0f });
					emitter.appearance.appearance[1] = 1.0f;
					emitter.appearance.modes[2] = std::min(index, uint32_t(ParticleBillboardMode::VelocityStretched));
					emitter.appearance.modes[3] = index < 6u ? uint32_t(ParticleBlendClass::Additive) :
						(index == 6u ? uint32_t(ParticleBlendClass::Alpha) : uint32_t(ParticleBlendClass::WeightedOit));
				}
				auto particleEffect = renderSystem->getParticleSystem().createEffect(particleTemplates);

				// Particles are the only thing in this scene, so any lit pixel in the
				// pass's own stored output came from the indirect draw.
				auto drewParticles = [](std::vector<GraphImageCapture> const& captures)
				{
					for (auto const& capture : captures)
						if ((capture.passName == "ParticleAlpha" || capture.passName == "ParticleAdditive" ||
							capture.passName == "ParticleWeightedOit" || capture.passName == "Particles") && !capture.depth)
							for (auto value : capture.pixels) if (value) return true;
					return false;
				};

				RenderPipelineOptions withoutParticles;
				withoutParticles.mode = RenderPipelineMode::GraphLegacyForward;
				auto plainPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestParticlesOffPipeline", withoutParticles);
				plainPipeline->render(particleScene, particleCamera, glm::vec2(0.0f));
				for (auto const& stats : plainPipeline->getLastGraphExecutionStats())
					if (stats.name == "ParticleMeshes" || stats.name == "ParticleAlpha" || stats.name == "ParticleAdditive" ||
						stats.name == "ParticleWeightedOit" || stats.name == "ParticleWeightedOitResolve" ||
						stats.name == "TrailAlpha" || stats.name == "TrailAdditive")
						return fail("default-off generated graph inserted a particle primitive pass");
				renderSystem->removeRenderPipeline("GpuTestParticlesOffPipeline");

				RenderPipelineOptions generatedParticleOptions;
				generatedParticleOptions.mode = RenderPipelineMode::GraphLegacyForward;
				generatedParticleOptions.generatedParticles = true;
				auto generatedPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestGeneratedParticlePipeline", generatedParticleOptions);
				generatedPipeline->requestGraphImageCapture();
				generatedPipeline->render(particleScene, particleCamera, glm::vec2(0.0f));
				size_t executedParticlePasses = 0;
				size_t executedTrailPasses = 0;
				bool executedMeshParticlePass = false;
				bool executedOitResolve = false;
				for (auto const& stats : generatedPipeline->getLastGraphExecutionStats())
				{
					if (stats.name == "ParticleMeshes")
					{
						executedMeshParticlePass = stats.colourOutputCount == 1u && stats.depthOutputCount == 1u;
					}
					else if (stats.name == "ParticleAlpha" || stats.name == "ParticleAdditive" || stats.name == "ParticleWeightedOit")
					{
						++executedParticlePasses;
						// Soft particles sample live depth but never attach or write it.
						size_t const expectedOutputs = stats.name == "ParticleWeightedOit" ? 2u : 1u;
						if (stats.colourOutputCount != expectedOutputs || stats.depthOutputCount != 0)
							return fail("a generated blend-class particle pass has the wrong colour/depth attachment contract");
					}
					else if (stats.name == "TrailAlpha" || stats.name == "TrailAdditive")
					{
						++executedTrailPasses;
						if (stats.colourOutputCount != 1u || stats.depthOutputCount != 0u)
							return fail("a generated trail ribbon pass has the wrong colour/depth attachment contract");
					}
					else if (stats.name == "ParticleWeightedOitResolve") executedOitResolve = stats.colourOutputCount == 1;
				}
				if (!executedMeshParticlePass || executedParticlePasses != 3 || executedTrailPasses != 2 || !executedOitResolve)
					return fail("generated particle primitives did not insert mesh, billboard, trail, and OIT passes");
				bool const particlesAvailable = renderSystem->particlesAvailable();
				std::vector<uint8_t> poolAfterGeneratedDraw;
				if (particlesAvailable)
				{
					// Query the linked spawn kernel rather than trusting the C++ declaration:
					// GL_TOP_LEVEL_ARRAY_STRIDE is the driver's resolved std430 stride.
					auto spawnProgram = renderSystem->getResourceManager()->getResource("__mpp_particle_spawn__", true);
					if (!spawnProgram) return fail("the available particle system has no spawn kernel");
					GLuint variable = glGetProgramResourceIndex(spawnProgram->getId(), GL_BUFFER_VARIABLE, "PARTICLES[0].padding");
					if (variable == GL_INVALID_INDEX) return fail("could not introspect the particle record in the spawn kernel");
					GLenum properties[]{ GL_OFFSET, GL_TOP_LEVEL_ARRAY_STRIDE };
					GLint values[2]{};
					GL_CHECK(glGetProgramResourceiv(spawnProgram->getId(), GL_BUFFER_VARIABLE, variable, 2, properties, 2, nullptr, values));
					if (values[0] != 60 || values[1] != GLint(sizeof(ParticleRecord)))
						return fail("the particle record is not exactly 64 bytes under std430");

					auto simulationProgram = renderSystem->getResourceManager()->getResource("__mpp_particle_simulation__", true);
					if (!simulationProgram) return fail("the available particle system has no simulation kernel");

					// Snapshot the allocated tail of the GPU pool. Rendering another
					// particle graph in this same renderer frame must be a pure draw: a
					// second simulation would change age even when its tiny dt did not move
					// a particle far enough to alter a captured RGB8 pixel.
					GLint poolBuffer = 0;
					GL_CHECK(glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &poolBuffer));
					if (poolBuffer == 0) return fail("particle pool was not left bound after drawing");
					GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, GLuint(poolBuffer)));
					GLint poolBytes = 0;
					GL_CHECK(glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &poolBytes));
					size_t const snapshotBytes = 8u * 24u * sizeof(ParticleRecord);
					if (size_t(poolBytes) < snapshotBytes) return fail("particle pool is smaller than the bootstrap allocation");
					poolAfterGeneratedDraw.resize(snapshotBytes);
					GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, poolBytes - GLint(snapshotBytes), snapshotBytes, poolAfterGeneratedDraw.data()));
					GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
				}
				if (drewParticles(generatedPipeline->takeGraphImageCaptures()) != particlesAvailable)
					return fail("GraphLegacyForward particles did not draw exactly when the particle system reported itself available");
				renderSystem->removeRenderPipeline("GpuTestGeneratedParticlePipeline");

				// The same pass, authored into a graph template instead of generated.
				auto authoredGraph = std::make_shared<RenderGraph>();
				GraphImageDesc authoredColourDesc;
				authoredColourDesc.format = GraphImageFormat::Rgba16f;
				authoredColourDesc.usage = GraphImageUsage::ColourAttachment | GraphImageUsage::Sampled;
				auto authoredColour = authoredGraph->createImage("GpuTestParticleColour", authoredColourDesc);
				auto authoredPass = authoredGraph->addPass("Particles", GraphPassType::Scene);
				authoredGraph->setPassCallbackFactory(authoredPass, "MPP.ParticleScene");
				UniformCollection authoredParameters;
				authoredParameters.setUniform("BLEND_MODE", int32_t(ParticleBlendClass::Additive));
				authoredGraph->setPassParameters(authoredPass, authoredParameters);
				authoredColour = authoredGraph->writeColour(authoredPass, authoredColour, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
				GraphRasterState authoredRaster;
				authoredRaster.explicitState = true;
				authoredRaster.depthTest = false;
				authoredRaster.depthWrite = false;
				authoredRaster.cullMode = GraphCullMode::None;
				authoredRaster.blend = true;
				authoredRaster.sourceColourBlend = GraphBlendFactor::SourceAlpha;
				authoredRaster.destinationColourBlend = GraphBlendFactor::One;
				authoredRaster.sourceAlphaBlend = GraphBlendFactor::Zero;
				authoredRaster.destinationAlphaBlend = GraphBlendFactor::One;
				authoredRaster.multisample = false;
				authoredGraph->setPassRasterState(authoredPass, authoredRaster);

				RenderGraphPassFactoryRegistry particleRegistry;
				registerBuiltInRenderGraphPasses(particleRegistry);
				auto const particleDiagnostics = particleRegistry.validate(*authoredGraph);
				if (particleDiagnostics.hasErrors())
					return fail("the authored particle pass failed its factory contract: " + particleDiagnostics.getDiagnostics().front().message);

				auto authoredStream = std::make_shared<RenderGraphStream>(renderSystem->getResourceManager());
				authoredStream->setGraph(authoredGraph);
				auto authoredTemplate = renderSystem->getResourceManager()->declareResource("GpuTestParticleGraph", authoredStream).first;
				authoredTemplate->load();
				authoredTemplate->create();

				RenderPipelineOptions authoredParticleOptions;
				authoredParticleOptions.mode = RenderPipelineMode::XmlGraphPbrForward;
				authoredParticleOptions.graphTemplate = authoredTemplate;
				auto authoredPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestAuthoredParticlePipeline", authoredParticleOptions);
				authoredPipeline->requestGraphImageCapture();
				authoredPipeline->render(particleScene, particleCamera, glm::vec2(0.0f));
				if (drewParticles(authoredPipeline->takeGraphImageCaptures()) != particlesAvailable)
					return fail("XmlGraphPbrForward particles did not draw the same as the generated graph");
				if (particlesAvailable)
				{
					GLint poolBuffer = 0;
					GL_CHECK(glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &poolBuffer));
					GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, GLuint(poolBuffer)));
					GLint poolBytes = 0;
					GL_CHECK(glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &poolBytes));
					std::vector<uint8_t> poolAfterAuthoredDraw(poolAfterGeneratedDraw.size());
					GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, poolBytes - GLint(poolAfterAuthoredDraw.size()),
						poolAfterAuthoredDraw.size(), poolAfterAuthoredDraw.data()));
					GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
					if (poolAfterAuthoredDraw != poolAfterGeneratedDraw)
						return fail("particles simulated more than once in one rendered frame");

					// Exercise the linked kernel against a tiny known pool. This isolates
					// every module bit and both survivor/death paths from bootstrap random
					// values while still executing the production GLSL on the driver.
					std::array<ParticleRecord, 8> particles{};
					for (uint32_t index = 0; index < particles.size(); ++index)
					{
						particles[index].velocityLifetime = { (index == 3u || index >= 4u) ? 0.0f : 2.0f, 0.0f, 0.0f, 10.0f };
						particles[index].emitterIndex = std::min(index, 6u);
						particles[index].angularVelocity = 3.0f;
					}
					particles[4].positionAge = { 0.5f, 0.5f, 0.5f, 0.0f };
					particles[7].emitterIndex = 0u;
					particles[7].positionAge[3] = 0.95f;
					particles[7].velocityLifetime[3] = 1.0f;
					std::array<EmitterSimData, 7> emitters{};
					emitters[1].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Gravity);
					emitters[1].gravityAndDrag = { 0.0f, -10.0f, 0.0f, 0.0f };
					emitters[2].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Drag);
					emitters[2].gravityAndDrag[3] = 5.0f;
					emitters[3].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Noise);
					emitters[3].noiseFrequencyStrength = { 0.0f, 0.0f, 0.0f, 2.0f };
					emitters[4].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::CurlNoise);
					emitters[4].curlNoiseFrequencyStrength = { 1.0f, 1.0f, 1.0f, 1.0f };
					emitters[5].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::Turbulence);
					emitters[5].turbulenceFrequencyStrength = { 1.0f, 1.0f, 1.0f, 2.0f };
					emitters[5].turbulenceOctavesLacunarityGain = { 3.0f, 2.0f, 0.5f, 0.0f };
					emitters[6].shapeSeedModulesBudget[2] = uint32_t(ParticleBehaviourModule::VectorField);
					emitters[6].vectorFieldFrequencyStrength = { 1.0f, 1.0f, 1.0f, 2.0f };
					std::array<uint32_t, 8> freeIndices{};
					std::array<uint32_t, 8> activeA{ 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u }, activeB{};
					std::array<uint32_t, 15> counters{ 0u, 8u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 1u, 1u, 1u, 1u, 1u, 1u };
					std::array<GLuint, 6> buffers{};
					auto bindTestBuffer = [&](uint32_t binding, auto const& values)
					{
						GLuint buffer = 0;
						GL_CHECK(glGenBuffers(1, &buffer));
						GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer));
						GL_CHECK(glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(values), values.data(), GL_DYNAMIC_COPY));
						GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer));
						buffers[binding] = buffer;
					};
					bindTestBuffer(0, particles);
					bindTestBuffer(1, freeIndices);
					bindTestBuffer(2, activeA);
					bindTestBuffer(3, activeB);
					bindTestBuffer(4, counters);
					bindTestBuffer(5, emitters);

					GLuint noiseTexture = 0, vectorFieldTexture = 0;
					std::array<float, 4u * 4u * 4u * 4u> noiseTexels{};
					for (uint32_t z = 0; z < 4u; ++z) for (uint32_t y = 0; y < 4u; ++y) for (uint32_t x = 0; x < 4u; ++x)
					{
						auto const texel = size_t((z * 16u + y * 4u + x) * 4u);
						noiseTexels[texel] = 1.0f;
						noiseTexels[texel + 1u] = 0.5f;
						noiseTexels[texel + 2u] = (float(y) + 0.5f) / 4.0f;
						noiseTexels[texel + 3u] = 1.0f;
					}
					GL_CHECK(glGenTextures(1, &noiseTexture));
					GL_CHECK(glActiveTexture(GL_TEXTURE0));
					GL_CHECK(glBindTexture(GL_TEXTURE_3D, noiseTexture));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT));
					GL_CHECK(glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, 4, 4, 4, 0, GL_RGBA, GL_FLOAT, noiseTexels.data()));
					std::array<float, 4> vectorFieldTexel{ 0.5f, 1.0f, 0.5f, 1.0f };
					GL_CHECK(glGenTextures(1, &vectorFieldTexture));
					GL_CHECK(glActiveTexture(GL_TEXTURE3));
					GL_CHECK(glBindTexture(GL_TEXTURE_3D, vectorFieldTexture));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
					GL_CHECK(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
					GL_CHECK(glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, 1, 1, 1, 0, GL_RGBA, GL_FLOAT, vectorFieldTexel.data()));

					auto simulationResource = renderSystem->getResourceManager()->getResource("__mpp_particle_simulation__", true);
					auto* simulation = dynamic_cast<ComputeProgram*>(simulationResource.get());
					if (!simulation) return fail("particle simulation resource is not a compute program");
					simulation->use();
					simulation->setUniform("ACTIVE_LIST_INDEX", 0u);
					simulation->setUniform("EMITTER_COUNT", 7u);
					simulation->setUniform("TEMPLATE_COUNT", 7u);
					simulation->setUniform("DELTA_SECONDS", 0.1f);
					simulation->setUniform("SIMULATION_SECONDS", 0.0f);
					simulation->setUniform("NOISE_TEXTURE", int32_t(0));
					simulation->setUniform("VECTOR_FIELD_TEXTURE", int32_t(3));
					simulation->setUniform("HAS_VECTOR_FIELD", int32_t(1));
					simulation->setUniform("HAS_COLLISION_DEPTH", int32_t(0));
					simulation->setUniform("HAS_SIGNED_DISTANCE_FIELD", int32_t(0));
					simulation->dispatch(1u);
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

					auto readTestBuffer = [&](uint32_t binding, auto& values)
					{
						GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[binding]));
						GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(values), values.data()));
					};
					readTestBuffer(0, particles);
					readTestBuffer(1, freeIndices);
					readTestBuffer(3, activeB);
					readTestBuffer(4, counters);
					auto near = [](float left, float right) { return std::abs(left - right) < 0.0001f; };
					std::string simulationFailure;
					if (!near(particles[0].positionAge[0], 0.2f) || !near(particles[0].positionAge[1], 0.0f))
						simulationFailure = "disabled behaviour modules changed velocity";
					else if (!near(particles[1].velocityLifetime[1], -1.0f) || !near(particles[1].positionAge[1], -0.1f))
						simulationFailure = "gravity module did not integrate independently";
					else if (!near(particles[2].velocityLifetime[0], 1.0f) || !near(particles[2].positionAge[0], 0.1f))
						simulationFailure = "drag module did not integrate independently";
					else if (!(particles[3].velocityLifetime[0] > 0.19f) || !(particles[3].positionAge[0] > 0.019f))
						simulationFailure = "3D-texture noise module did not integrate independently";
					else if (!(particles[4].velocityLifetime[0] > 0.19f) || !(particles[4].positionAge[0] > 0.519f))
						simulationFailure = "curl-noise module did not integrate independently";
					else if (!(particles[5].velocityLifetime[0] > 0.19f) || !(particles[5].positionAge[0] > 0.019f))
						simulationFailure = "multi-octave turbulence module did not integrate independently";
					else if (!(particles[6].velocityLifetime[1] > 0.19f) || !(particles[6].positionAge[1] > 0.019f))
						simulationFailure = "arbitrary vector-field module did not integrate independently";
					else if (!near(particles[0].rotation, 0.3f) || !near(particles[0].positionAge[3], 0.1f))
						simulationFailure = "base age or angular-velocity integration failed";
					else if (counters[0] != 1u || counters[2] != 7u || counters[8] != 1u || freeIndices[0] != 7u)
						simulationFailure = "survivor active-list append or dead-particle reclamation failed";
					else
					{
						auto survivors = activeB;
						std::sort(survivors.begin(), survivors.begin() + 7);
						for (uint32_t index = 0; index < 7u; ++index)
							if (survivors[index] != index) simulationFailure = "survivors were not written to the opposite active list";
					}

					GL_CHECK(glUseProgram(0));
					GL_CHECK(glActiveTexture(GL_TEXTURE3));
					GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
					GL_CHECK(glDeleteTextures(1, &vectorFieldTexture));
					GL_CHECK(glActiveTexture(GL_TEXTURE0));
					GL_CHECK(glBindTexture(GL_TEXTURE_3D, 0));
					GL_CHECK(glDeleteTextures(1, &noiseTexture));
					GL_CHECK(glDeleteBuffers(GLsizei(buffers.size()), buffers.data()));
					if (!simulationFailure.empty()) return fail(simulationFailure);

					// Exercise all four production compaction kernels against an
					// interleaved survivor list. The CPU reference checks both counts and
					// every contiguous template run; indirect instance counts and range
					// starts must have been authored by the prefix kernel.
					std::array<ParticleRecord, 8> compactParticles{};
					uint32_t const particleEmitters[]{ 2u, 0u, 3u, 1u, 0u, 2u, 2u, 1u };
					for (uint32_t index = 0; index < compactParticles.size(); ++index)
						compactParticles[index].emitterIndex = particleEmitters[index];
					std::array<uint32_t, 8> compactActiveA{ 6u, 1u, 4u, 0u, 7u, 2u, 5u, 3u };
					std::array<uint32_t, 8> compactActiveB{};
					std::array<EmitterSimData, 4> compactEmitters{};
					uint32_t const emitterTemplates[]{ 2u, 0u, 2u, 1u };
					for (uint32_t index = 0; index < compactEmitters.size(); ++index)
						compactEmitters[index].emissionState[3] = emitterTemplates[index];
					compactEmitters[1].shapeSeedModulesBudget[3] = 2u;
					std::array<uint32_t, 12> compactCounters{ 8u, 8u, 0u, 0u, 0u, 0u, 0u, 0u, 99u, 99u, 99u, 99u };
					std::array<uint32_t, 12> compactScratch{};
					std::array<TemplateRenderData, 4> compactTemplates{};
					std::array<uint32_t, 8> renderIndices{};
					std::array<ParticleDrawArraysIndirectCommand, 4> drawCommands{};
					std::array<uint32_t, 3> compactDispatch{};
					std::array<uint32_t, 8> compactFreeIndices{};
					std::array<ParticleSpawnCommand, 1> budgetCommands{ ParticleSpawnCommand{ 1u, 1u, 7u, 0u } };
					std::vector<GLuint> compactBuffers;
					auto createTestBuffer = [&](uint32_t binding, auto const& values)
					{
						GLuint buffer = 0;
						GL_CHECK(glGenBuffers(1, &buffer));
						GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer));
						GL_CHECK(glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(values), values.data(), GL_DYNAMIC_COPY));
						GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer));
						compactBuffers.push_back(buffer);
						return buffer;
					};
					auto readBuffer = [&](GLuint buffer, auto& values)
					{
						GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer));
						GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(values), values.data()));
					};
					auto getCompute = [&](char const* name)
					{
						auto resource = renderSystem->getResourceManager()->getResource(name, true);
						return dynamic_cast<ComputeProgram*>(resource.get());
					};

					GLuint compactParticleBuffer = createTestBuffer(0, compactParticles);
					GLuint compactActiveABuffer = createTestBuffer(2, compactActiveA);
					GLuint compactActiveBBuffer = createTestBuffer(3, compactActiveB);
					GLuint compactCounterBuffer = createTestBuffer(4, compactCounters);
					GLuint compactEmitterBuffer = createTestBuffer(5, compactEmitters);
					GLuint compactScratchBuffer = createTestBuffer(6, compactScratch);
					GLuint compactDispatchBuffer = createTestBuffer(7, compactDispatch);
					GLuint compactTemplateBuffer = createTestBuffer(7, compactTemplates);

					auto* compactPrepare = getCompute("__mpp_particle_compaction_prepare__");
					auto* compactCount = getCompute("__mpp_particle_compaction_count__");
					auto* compactPrefix = getCompute("__mpp_particle_compaction_prefix__");
					auto* compactScatter = getCompute("__mpp_particle_compaction_scatter__");
					if (!compactPrepare || !compactCount || !compactPrefix || !compactScatter)
						return fail("particle compaction programs were not registered");

					compactPrepare->use();
					compactPrepare->setUniform("ACTIVE_LIST_INDEX", 0u);
					compactPrepare->setUniform("TEMPLATE_COUNT", 4u);
					compactPrepare->setUniform("TEMPLATE_CAPACITY", 4u);
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, compactDispatchBuffer));
					compactPrepare->dispatch(1u);
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

					compactCount->use();
					compactCount->setUniform("ACTIVE_LIST_INDEX", 0u);
					compactCount->setUniform("EMITTER_COUNT", 4u);
					compactCount->setUniform("TEMPLATE_COUNT", 4u);
					compactCount->setUniform("TEMPLATE_CAPACITY", 4u);
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, compactTemplateBuffer));
					GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, compactDispatchBuffer));
					compactCount->dispatchIndirect();
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));

					GLuint drawCommandBuffer = createTestBuffer(7, drawCommands);
					compactPrefix->use();
					compactPrefix->setUniform("TEMPLATE_COUNT", 4u);
					compactPrefix->setUniform("TEMPLATE_CAPACITY", 4u);
					compactPrefix->dispatch(1u);
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

					GLuint renderIndexBuffer = createTestBuffer(1, renderIndices);
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, compactScratchBuffer));
					compactScatter->use();
					compactScatter->setUniform("ACTIVE_LIST_INDEX", 0u);
					compactScatter->setUniform("EMITTER_COUNT", 4u);
					compactScatter->setUniform("TEMPLATE_COUNT", 4u);
					compactScatter->setUniform("TEMPLATE_CAPACITY", 4u);
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, compactTemplateBuffer));
					GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, compactDispatchBuffer));
					compactScatter->dispatchIndirect();
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));

					readBuffer(compactCounterBuffer, compactCounters);
					readBuffer(compactScratchBuffer, compactScratch);
					readBuffer(renderIndexBuffer, renderIndices);
					readBuffer(drawCommandBuffer, drawCommands);
					readBuffer(compactDispatchBuffer, compactDispatch);

					std::array<uint32_t, 4> referenceCounts{};
					for (uint32_t particleIndex : compactActiveA)
						++referenceCounts[emitterTemplates[compactParticles[particleIndex].emitterIndex]];
					uint32_t referenceOffset = 0u;
					std::array<bool, 8> seenParticles{};
					std::string compactionFailure;
					for (uint32_t templateIndex = 0; templateIndex < referenceCounts.size(); ++templateIndex)
					{
						if (compactCounters[8u + templateIndex] != referenceCounts[templateIndex] ||
							compactScratch[templateIndex] != referenceOffset ||
							compactScratch[4u + templateIndex] != referenceCounts[templateIndex])
						{
							compactionFailure = "GPU template counts or prefix offsets do not match the CPU reference";
							break;
						}
						auto const& command = drawCommands[templateIndex];
						if (command.count != 4u || command.instanceCount != referenceCounts[templateIndex] ||
							command.first != referenceOffset * 4u || command.baseInstance != templateIndex)
						{
							compactionFailure = "GPU indirect command does not describe its emitter-template range";
							break;
						}
						for (uint32_t ordinal = referenceOffset; ordinal < referenceOffset + referenceCounts[templateIndex]; ++ordinal)
						{
							uint32_t particleIndex = renderIndices[ordinal];
							if (particleIndex >= compactParticles.size() || seenParticles[particleIndex] ||
								emitterTemplates[compactParticles[particleIndex].emitterIndex] != templateIndex)
							{
								compactionFailure = "render index list does not contain one contiguous run per emitter template";
								break;
							}
							seenParticles[particleIndex] = true;
						}
						if (!compactionFailure.empty()) break;
						referenceOffset += referenceCounts[templateIndex];
					}
					if (compactDispatch != std::array<uint32_t, 3>{ 1u, 1u, 1u })
						compactionFailure = "compaction dispatch was not generated from the GPU active count";
					if (!compactionFailure.empty()) return fail(compactionFailure);

					// Feed the count produced above straight into the production spawn
					// kernel. Template zero is already at its budget, so this request must
					// be dropped without touching the free or active counts.
					GLuint compactFreeBuffer = createTestBuffer(1, compactFreeIndices);
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, compactParticleBuffer));
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, compactActiveABuffer));
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, compactActiveBBuffer));
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, compactCounterBuffer));
					GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, compactEmitterBuffer));
					GLuint budgetCommandBuffer = createTestBuffer(7, budgetCommands);
					auto budgetSpawnResource = renderSystem->getResourceManager()->getResource("__mpp_particle_spawn__", true);
					auto* budgetSpawn = dynamic_cast<ComputeProgram*>(budgetSpawnResource.get());
					if (!budgetSpawn) return fail("particle spawn resource is not a compute program");
					budgetSpawn->use();
					budgetSpawn->setUniform("EMITTER_COUNT", 4u);
					budgetSpawn->setUniform("TEMPLATE_COUNT", 4u);
					budgetSpawn->setUniform("SPAWN_COMMAND_OFFSET", 0u);
					budgetSpawn->setUniform("ACTIVE_LIST_INDEX", 0u);
					budgetSpawn->dispatch(1u);
					GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
					readBuffer(compactCounterBuffer, compactCounters);
					if (compactCounters[0] != 8u || compactCounters[1] != 8u || compactCounters[3] != 1u ||
						compactCounters[8] != referenceCounts[0])
						return fail("spawn budget clamp did not consume the compaction template counter");

					(void)compactFreeBuffer;
					(void)budgetCommandBuffer;
					GL_CHECK(glUseProgram(0));
					GL_CHECK(glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0));
					GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));
					GL_CHECK(glDeleteBuffers(GLsizei(compactBuffers.size()), compactBuffers.data()));
				}
				// A live depth image at the particle plane must fade the same particles
				// completely. The otherwise identical authored pass above omitted DEPTH
				// and therefore proves the hard-edged fallback remains drawable.
				auto softGraph = std::make_shared<RenderGraph>();
				GraphImageDesc softDepthDesc;
				softDepthDesc.format = GraphImageFormat::Depth24;
				softDepthDesc.usage = GraphImageUsage::DepthAttachment | GraphImageUsage::Sampled;
				auto softDepth = softGraph->createImage("GpuTestParticleDepth", softDepthDesc);
				auto softDepthPass = softGraph->addPass("ParticleDepth", GraphPassType::Scene);
				softGraph->setPassCallbackFactory(softDepthPass, "MPP.ShadowDepth");
				auto centreClip = particleCamera->getProjectionTransform() * particleCamera->getViewTransform() * glm::vec4(0, 0, 0, 1);
				float const particlePlaneDepth = (centreClip.z / centreClip.w) * 0.5f + 0.5f;
				softDepth = softGraph->writeDepth(softDepthPass, softDepth, GraphLoadOp::Clear, GraphStoreOp::Store, particlePlaneDepth);
				auto softColour = softGraph->createImage("GpuTestSoftParticleColour", authoredColourDesc);
				auto softPass = softGraph->addPass("Particles", GraphPassType::Scene);
				softGraph->setPassCallbackFactory(softPass, "MPP.ParticleScene");
				softGraph->bindSampler(softPass, "DEPTH", softDepth);
				softGraph->setPassParameters(softPass, authoredParameters);
				softColour = softGraph->writeColour(softPass, softColour, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0.0f));
				softGraph->setPassRasterState(softPass, authoredRaster);
				auto softStream = std::make_shared<RenderGraphStream>(renderSystem->getResourceManager());
				softStream->setGraph(softGraph);
				auto softTemplate = renderSystem->getResourceManager()->declareResource("GpuTestSoftParticleGraph", softStream).first;
				softTemplate->load();
				softTemplate->create();
				auto softOptions = authoredParticleOptions;
				softOptions.graphTemplate = softTemplate;
				auto softPipeline = renderSystem->getOrCreateRenderPipeline("GpuTestSoftParticlePipeline", softOptions);
				softPipeline->requestGraphImageCapture();
				softPipeline->render(particleScene, particleCamera, glm::vec2(0.0f));
				if (drewParticles(softPipeline->takeGraphImageCaptures()))
					return fail("soft particles did not fade at the live scene-depth intersection");
				renderSystem->removeRenderPipeline("GpuTestSoftParticlePipeline");

				renderSystem->getParticleSystem().destroyEffect(particleEffect);
				renderSystem->removeRenderPipeline("GpuTestAuthoredParticlePipeline");
			}

			stage = "MRT readback";
			if (renderSystem->getCaps().maxDrawBuffers >= 2 && renderSystem->getCaps().maxColourAttachments >= 2)
			{
				RenderGraph mrt;
				auto left = mrt.createImage("GpuTestMrt0", colour);
				auto right = mrt.createImage("GpuTestMrt1", colour);
				auto pass = mrt.addPass("GpuTestMrt", GraphPassType::Scene);
				left = mrt.writeColour(pass, left, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(0, 0, 1, 1));
				right = mrt.writeColour(pass, right, GraphLoadOp::Clear, GraphStoreOp::Store, glm::vec4(1, 1, 0, 1));
				RenderGraphTargets mrtTargets(renderSystem);
				mrtTargets.allocate(mrt.buildAllocationPlan({ 32, 32 }));
				RenderGraphExecutor mrtExecutor(renderSystem);
				mrtExecutor.setPassCallback(mrt, pass, [](RenderGraphExecutionContext const&) {});
				mrtExecutor.execute(mrt, mrtTargets, renderSystem->getCaps());
				if (!nearColour(readFirstPixel(mrtTargets.get(left)), { 0, 0, 255, 255 })) return fail("MRT location 0 readback failed");
				if (!nearColour(readFirstPixel(mrtTargets.get(right)), { 255, 255, 0, 255 })) return fail("MRT location 1 readback failed");
			}

			stage = "screen-space text overlay";
			RenderTextureOptions textTargetOptions;
			auto textTarget = renderSystem->createRenderTexture(
				"GpuTestTextOverlay",
				renderSystem->getWindowWidth(),
				renderSystem->getWindowHeight(),
				textTargetOptions);
			renderSystem->setRenderTarget(textTarget);
			// Binding a target does not resize the viewport, and the graph stages above
			// leave it at their own fixture sizes. Text is placed in window space, so
			// without this the glyphs land outside the viewport entirely.
			renderSystem->setViewport(0, 0, textTarget->getWidth(), textTarget->getHeight());
			renderSystem->clearScreen(Colour::Black);
			renderSystem->renderText("RenderGraph text overlay", 8, 0, Colour::White);
			GL_CHECK(glFinish());
			bool textVisible = containsVisiblePixel(textTarget);
			if (!textVisible)
			{
				renderSystem->renderToScreen();
				return fail("screen-space text overlay produced no visible pixels");
			}

			renderSystem->clearScreen(Colour::Black);
			renderSystem->renderTextFormatted("[#FF0000FF]D", 8, 0);
			GL_CHECK(glFinish());
			uint8_t opaqueRed = maximumRed(textTarget);

			renderSystem->clearScreen(Colour::Black);
			renderSystem->renderTextFormatted("[#FF000080]D", 8, 0);
			GL_CHECK(glFinish());
			uint8_t translucentRed = maximumRed(textTarget);
			renderSystem->renderToScreen();
			if (opaqueRed < 32 || translucentRed < 8 || translucentRed >= opaqueRed * 3 / 4)
			{
				return fail("formatted text alpha was not applied to glyph coverage");
			}

			stage = "TAA jitter and camera-cut revisions";auto jitter0=taaHaltonJitter(0),jitter1=taaHaltonJitter(1),jitter7=taaHaltonJitter(7);if(glm::distance(jitter0,glm::vec2(0.0f,-1.0f/6.0f))>0.0001f||glm::distance(jitter1,glm::vec2(-0.25f,1.0f/6.0f))>0.0001f||glm::distance(jitter7,glm::vec2(-0.4375f,0.3888889f))>0.0001f||taaHaltonJitter(8)!=jitter0)return fail("eight-sample Halton jitter sequence is incorrect");Camera taaCamera({0,0,5},0,0,0,60,1);auto unjittered=taaCamera.getProjectionTransform();taaCamera.setProjectionJitter({0.01f,-0.02f});auto jittered=taaCamera.getProjectionTransform();if(jittered[2][0]==unjittered[2][0]||jittered[2][1]==unjittered[2][1])return fail("camera projection jitter was not applied");auto cutRevision=taaCamera.getCutRevision();taaCamera.markCut();if(taaCamera.getCutRevision()!=cutRevision+1)return fail("explicit camera-cut revision failed");taaCamera.setProjectionJitter({0,0});

			stage = "transactional named-output processor";
			RenderGraph outputGraph;GraphImageDesc outputDesc;outputDesc.format=GraphImageFormat::Rgba8;outputDesc.usage=GraphImageUsage::ColourAttachment|GraphImageUsage::Sampled;// This fixture drives the output the way an external one works -- the graph
			// renders into the processor's Input image and presents into a separate
			// destination -- so it has to say so. Offscreen outputs are covered below.
			outputDesc.external=true;outputGraph.createImage("Presentation",outputDesc);RenderPipelineOutput output;output.name="Main";output.image="Presentation";output.antiAliasing.msaa=AntiAliasingSamples::X4;output.antiAliasing.ssaa=AntiAliasingSamples::X4;output.antiAliasing.taa=true;output.antiAliasing.fxaa=true;RenderOutputProcessor processor(renderSystem,"GpuTestOutput");processor.rebuild({output},outputGraph,{{"Main",textTarget}},{});auto generation=processor.getGeneration();if(generation==0||processor.getPlans().size()!=1||processor.getPlans().front().rasterSamples!=4||processor.getPlans().front().rasterSize!=glm::uvec2(textTarget->getWidth()*2,textTarget->getHeight()*2)||processor.getPlans().front().physicalImages.size()!=7)return fail("named-output physical plan/history ownership is incomplete");processor.rebuild({output},outputGraph,{{"Main",textTarget}},{});if(processor.getGeneration()!=generation)return fail("unchanged output plan replaced its generation");auto oldInput=processor.getInput("Main");bool rejected=false;try{auto invalid=output;invalid.image="Missing";processor.rebuild({invalid},outputGraph,{{"Main",textTarget}},{});}catch(...){rejected=true;}if(!rejected||processor.getGeneration()!=generation||processor.getInput("Main")!=oldInput)return fail("failed output generation did not retain prior resources");renderSystem->setRenderTarget(oldInput);renderSystem->clearScreen(Colour(1,0,0,0.25f));RenderTextureOptions taaDepthOptions;taaDepthOptions.numAttachments=0;taaDepthOptions.depthAttachment=RenderTextureDepthAttachment::DepthTexture;auto taaDepth=renderSystem->createRenderTexture("GpuTestTaaDepth",oldInput->getWidth(),oldInput->getHeight(),taaDepthOptions);renderSystem->setRenderTarget(taaDepth);float taaDepthValue=0.5f;GL_CHECK(glClearBufferfv(GL_DEPTH,0,&taaDepthValue));TaaFrameContext taaFrame;taaFrame.frameSerial=1;taaFrame.resetHistory=true;processor.present("Main",textTarget,{},taaDepth,&taaFrame);GL_CHECK(glFinish());if(!nearColour(readFirstPixel(textTarget),{255,0,0,64})||!processor.hasValidTaaHistory("Main")||processor.getTaaHistoryResetCount("Main")!=1)return fail("TAA first-frame history/alpha initialization failed");taaFrame.frameSerial=2;taaFrame.resetHistory=false;processor.present("Main",textTarget,{},taaDepth,&taaFrame);if(processor.getTaaHistoryResetCount("Main")!=1)return fail("TAA valid consecutive frame reset history");taaFrame.frameSerial=4;processor.present("Main",textTarget,{},taaDepth,&taaFrame);if(processor.getTaaHistoryResetCount("Main")!=2)return fail("TAA skipped-frame reset failed");taaFrame.frameSerial=5;taaFrame.resetHistory=true;processor.present("Main",textTarget,{},taaDepth,&taaFrame);if(processor.getTaaHistoryResetCount("Main")!=3)return fail("TAA explicit camera-cut reset failed");auto resizedTaaTarget=renderSystem->createRenderTexture("GpuTestTaaResize",31,23,RenderTextureOptions{});processor.rebuild({output},outputGraph,{{"Main",resizedTaaTarget}},{});if(processor.hasValidTaaHistory("Main"))return fail("TAA resize/generation replacement retained history");output.antiAliasing.taa=false;auto smallSsaaTarget=renderSystem->createRenderTexture("GpuTestSsaaOutput",31,23,RenderTextureOptions{});for(auto factor:{AntiAliasingSamples::X2,AntiAliasingSamples::X4,AntiAliasingSamples::X8}){output.antiAliasing.ssaa=factor;processor.rebuild({output},outputGraph,{{"Main",smallSsaaTarget}},{});auto const& ssaaPlan=processor.getPlans().front();if(ssaaPlan.rasterSize!=glm::uvec2(ssaaDimension(31,factor),ssaaDimension(23,factor)))return fail("SSAA physical dimensions are incorrect");renderSystem->setRenderTarget(processor.getInput("Main"));renderSystem->clearScreen(Colour(0.2f,0.4f,0.8f,0.375f));processor.present("Main",smallSsaaTarget);GL_CHECK(glFinish());if(!nearColour(readFirstPixel(smallSsaaTarget),{51,102,204,96}))return fail("SSAA factor Lanczos readback did not preserve colour/alpha");if(factor==AntiAliasingSamples::X4){auto input=dynamic_cast<RenderTexture*>(processor.getInput("Main").get());std::vector<uint8_t> checker(input->getWidth()*input->getHeight()*4);for(size_t y=0;y<input->getHeight();++y)for(size_t x=0;x<input->getWidth();++x){auto value=(uint8_t)(((x+y)&1)?255:0);auto index=(y*input->getWidth()+x)*4;checker[index]=checker[index+1]=checker[index+2]=value;checker[index+3]=255;}GL_CHECK(glBindTexture(GL_TEXTURE_2D,input->getColourAttachmentId(0)));GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D,0,0,0,(GLsizei)input->getWidth(),(GLsizei)input->getHeight(),GL_RGBA,GL_UNSIGNED_BYTE,checker.data()));GL_CHECK(glBindTexture(GL_TEXTURE_2D,0));processor.present("Main",smallSsaaTarget);GL_CHECK(glFinish());auto filtered=readPixels(smallSsaaTarget);auto centre=((smallSsaaTarget->getHeight()/2)*smallSsaaTarget->getWidth()+smallSsaaTarget->getWidth()/2)*4;if(filtered[centre]<100||filtered[centre]>155||filtered[centre+3]<254)return fail("Lanczos checkerboard downsample did not filter/preserve alpha");}}

			stage="offscreen output anti-aliasing chain";
			// An offscreen output presents into the graph's own image, so its input and
			// destination are the same target. present() used to treat that alone as
			// "nothing to do" and return, dropping FXAA and TAA even though the
			// authoring was accepted and their work targets allocated.
			{
				RenderGraph offscreenGraph;GraphImageDesc offscreenDesc;offscreenDesc.format=GraphImageFormat::Rgba8;offscreenDesc.usage=GraphImageUsage::ColourAttachment|GraphImageUsage::Sampled;
				offscreenGraph.createImage("Offscreen",offscreenDesc);
				RenderPipelineOutput offscreenOutput;offscreenOutput.name="Offscreen";offscreenOutput.image="Offscreen";offscreenOutput.antiAliasing.fxaa=true;
				RenderOutputProcessor offscreenProcessor(renderSystem,"GpuTestOffscreenOutput");
				auto offscreenTarget=renderSystem->createRenderTexture("GpuTestOffscreenTarget",16,16,RenderTextureOptions{});
				offscreenProcessor.rebuild({offscreenOutput},offscreenGraph,{{"Offscreen",offscreenTarget}},{});
				renderSystem->setRenderTarget(offscreenTarget);renderSystem->clearScreen(Colour(0.2f,0.4f,0.8f,1.0f));
				RenderPipelineFlowSnapshot offscreenSnapshot;renderSystem->beginRenderFlowCapture(&offscreenSnapshot);
				offscreenProcessor.present("Offscreen",offscreenTarget,offscreenTarget,{},nullptr);
				renderSystem->endRenderFlowCapture();GL_CHECK(glFinish());
				bool ranFxaa=false,ranPresentation=false;
				for(auto const& event:offscreenSnapshot.physicalEvents)
				{
					if(event.kind==RenderFlowEventKind::Fxaa&&event.enabled)ranFxaa=true;
					if(event.kind==RenderFlowEventKind::Presentation&&event.enabled)ranPresentation=true;
				}
				if(!ranFxaa||!ranPresentation)return fail("an offscreen named output skipped its anti-aliasing chain");
				// The chain must also leave the image intact rather than blitting a
				// stale or empty intermediate over it.
				if(!nearColour(readFirstPixel(offscreenTarget),{51,102,204,255}))return fail("offscreen output chain did not preserve its colour");
				// SSAA cannot act on an offscreen output, so the plan must drop it
				// rather than sizing the work targets against a destination that is
				// already the raster-size graph image.
				auto ssaaOffscreen=offscreenOutput;ssaaOffscreen.antiAliasing.ssaa=AntiAliasingSamples::X4;
				offscreenProcessor.rebuild({ssaaOffscreen},offscreenGraph,{{"Offscreen",offscreenTarget}},{});
				auto const& offscreenPlan=offscreenProcessor.getPlans().front();
				if(offscreenPlan.antiAliasing.ssaa!=AntiAliasingSamples::Off||offscreenPlan.rasterSize!=offscreenPlan.logicalSize)
					return fail("SSAA on an offscreen output was planned against a raster-size destination");
			}

			stage="TAA accumulation, neighbourhood clamp, and depth rejection";output.antiAliasing.ssaa=AntiAliasingSamples::Off;output.antiAliasing.taa=true;output.antiAliasing.fxaa=false;auto taaOutput=renderSystem->createRenderTexture("GpuTestTaaOutput",9,9,RenderTextureOptions{});processor.rebuild({output},outputGraph,{{"Main",taaOutput}},{});auto taaInputTarget=processor.getInput("Main");auto taaInput=dynamic_cast<RenderTexture*>(taaInputTarget.get());RenderTextureOptions smallDepthOptions;smallDepthOptions.numAttachments=0;smallDepthOptions.depthAttachment=RenderTextureDepthAttachment::DepthTexture;auto currentDepth=renderSystem->createRenderTexture("GpuTestTaaCurrentDepth",9,9,smallDepthOptions);renderSystem->setRenderTarget(taaInputTarget);renderSystem->clearScreen(Colour(0.5f,0.5f,0.5f,0.5f));renderSystem->setRenderTarget(currentDepth);float halfDepth=0.5f;GL_CHECK(glClearBufferfv(GL_DEPTH,0,&halfDepth));TaaFrameContext accumulationFrame;accumulationFrame.frameSerial=10;accumulationFrame.resetHistory=true;processor.present("Main",taaOutput,{},currentDepth,&accumulationFrame);std::vector<uint8_t> taaChecker(9*9*4);for(size_t pixel=0;pixel<81;++pixel){auto value=(uint8_t)((((pixel%9)+(pixel/9))&1)?255:0);taaChecker[pixel*4]=taaChecker[pixel*4+1]=taaChecker[pixel*4+2]=value;taaChecker[pixel*4+3]=128;}GL_CHECK(glBindTexture(GL_TEXTURE_2D,taaInput->getColourAttachmentId(0)));GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D,0,0,0,9,9,GL_RGBA,GL_UNSIGNED_BYTE,taaChecker.data()));GL_CHECK(glBindTexture(GL_TEXTURE_2D,0));accumulationFrame.frameSerial=11;accumulationFrame.resetHistory=false;processor.present("Main",taaOutput,{},currentDepth,&accumulationFrame);GL_CHECK(glFinish());auto accumulated=readPixels(taaOutput);auto accumulatedCentre=(4*9+4)*4;if(accumulated[accumulatedCentre]<100||accumulated[accumulatedCentre]>130||accumulated[accumulatedCentre+3]<126||accumulated[accumulatedCentre+3]>130)return fail("TAA static-history blend/neighbourhood clamp failed");renderSystem->setRenderTarget(taaInputTarget);renderSystem->clearScreen(Colour(0,0,1,0.25f));renderSystem->setRenderTarget(currentDepth);float changedDepth=0.8f;GL_CHECK(glClearBufferfv(GL_DEPTH,0,&changedDepth));accumulationFrame.frameSerial=12;processor.present("Main",taaOutput,{},currentDepth,&accumulationFrame);GL_CHECK(glFinish());if(!nearColour(readFirstPixel(taaOutput),{0,0,255,64}))return fail("TAA depth-inconsistent history was not rejected");

			stage="fixed high-quality FXAA LDR processing";output.antiAliasing.taa=false;output.antiAliasing.fxaa=true;auto fxaaOutput=renderSystem->createRenderTexture("GpuTestFxaaOutput",16,16,RenderTextureOptions{});processor.rebuild({output},outputGraph,{{"Main",fxaaOutput}},{});auto fxaaInput=dynamic_cast<RenderTexture*>(processor.getInput("Main").get());std::vector<uint8_t> staircase(16*16*4);for(size_t y=0;y<16;++y)for(size_t x=0;x<16;++x){auto index=(y*16+x)*4;auto value=(uint8_t)(x>y?255:0);staircase[index]=staircase[index+1]=staircase[index+2]=value;staircase[index+3]=96;}GL_CHECK(glBindTexture(GL_TEXTURE_2D,fxaaInput->getColourAttachmentId(0)));GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D,0,0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,staircase.data()));GL_CHECK(glBindTexture(GL_TEXTURE_2D,0));processor.present("Main",fxaaOutput);GL_CHECK(glFinish());auto antialiased=readPixels(fxaaOutput);size_t softened=0;for(size_t pixel=0;pixel<256;++pixel){auto red=antialiased[pixel*4];if(red>8&&red<247)++softened;if(std::abs((int)antialiased[pixel*4+3]-96)>1)return fail("FXAA did not preserve alpha");}if(!softened)return fail("FXAA did not soften a staircase edge");


			stage = "pass-scoped folded point-shadow caster clips";
			CameraCulledShadowTestScene foldedScene(renderSystem);
			auto apertureFrame = foldedScene.add3dModel(measuredModelResource);
			apertureFrame->scale({ 0.02f, 8.0f, 8.0f });
			auto sourceOccluder = foldedScene.add3dModel(measuredModelResource);
			sourceOccluder->scale({ 0.2f, 0.6f, 0.6f });
			sourceOccluder->translate({ 1.0f, 0.0f, 0.0f });
			auto destinationOccluder = foldedScene.add3dModel(measuredModelResource);
			destinationOccluder->scale({ 0.2f, 0.6f, 0.6f });
			destinationOccluder->translate({ -1.0f, 0.0f, 0.0f });

			auto foldedOptions = pointQuality;
			foldedOptions.resolution = 32;
			foldedOptions.light.position = { 2.0f, 0.0f, 0.0f };
			foldedOptions.light.range = 10.0f;
			foldedOptions.pointCasterClip.enabled = true;
			foldedOptions.pointCasterClip.retainedWorldPlane = { 1.0f, 0.0f, 0.0f, 0.0f };
			foldedOptions.pointCasterClip.openingCentre = {};
			foldedOptions.pointCasterClip.openingTangent = { 0.0f, 0.0f, 1.0f };
			foldedOptions.pointCasterClip.openingBitangent = { 0.0f, 1.0f, 0.0f };
			foldedOptions.pointCasterClip.openingHalfSize = { 0.75f, 0.75f };
			foldedOptions.pointCasterClip.planeTolerance = 0.02f;

			auto foldedSentinel = renderSystem->createRenderTexture(
				"GpuTestFoldedShadowSentinel", 9, 7, RenderTextureOptions{});
			renderSystem->setRenderTarget(foldedSentinel);
			renderSystem->setViewport(1, 1, 5, 4);
			GraphRasterState foldedSentinelRaster;
			foldedSentinelRaster.explicitState = true;
			foldedSentinelRaster.depthTest = false;
			foldedSentinelRaster.depthWrite = false;
			foldedSentinelRaster.cullMode = GraphCullMode::Back;
			foldedSentinelRaster.blend = true;
			renderSystem->applyRasterState(foldedSentinelRaster, 1, 5, 4);
			auto const expectedFoldedRaster = renderSystem->captureRasterState(1);
			auto foldedStateRestored = [&]
			{
				return renderSystem->mRenderTarget == foldedSentinel &&
					renderSystem->mViewportX == 1 && renderSystem->mViewportY == 1 &&
					renderSystem->mViewportWidth == 5 && renderSystem->mViewportHeight == 4 &&
					renderSystem->captureRasterState(1) == expectedFoldedRaster;
			};

			renderSystem->configureShadowDomain("GpuTestFoldedSource", foldedOptions);
			renderSystem->renderShadowDomain("GpuTestFoldedSource", { apertureFrame });
			auto sourceTarget = renderSystem->getShadowDomainDepthTarget("GpuTestFoldedSource");
			float clearSource = readCubeDepth(sourceTarget, 1, 16, 16);
			float frameSource = readCubeDepth(sourceTarget, 1, 2, 2);
			if (clearSource < 0.99f || frameSource >= 0.99f || !foldedStateRestored())
				return fail("bounded point-shadow opening did not retain its frame or restore render state");
			renderSystem->renderShadowDomain("GpuTestFoldedSource", { apertureFrame, sourceOccluder });
			float blockedSource = readCubeDepth(sourceTarget, 1, 16, 16);
			if (blockedSource >= 0.99f || !foldedStateRestored())
				return fail("source-leg occluder did not shadow the open aperture path");
			renderSystem->renderShadowDomain("GpuTestFoldedSource", { apertureFrame });
			if (readCubeDepth(sourceTarget, 1, 16, 16) < 0.99f)
				return fail("removing the source-leg occluder did not restore only its open path");

			auto destinationOptions = foldedOptions;
			destinationOptions.pointCasterClip.retainedWorldPlane = { -1.0f, 0.0f, 0.0f, 0.0f };
			renderSystem->configureShadowDomain("GpuTestFoldedDestination", destinationOptions);
			renderSystem->renderShadowDomain(
				"GpuTestFoldedDestination", { apertureFrame, destinationOccluder });
			auto destinationTarget = renderSystem->getShadowDomainDepthTarget("GpuTestFoldedDestination");
			if (readCubeDepth(destinationTarget, 1, 16, 16) >= 0.99f ||
				readCubeDepth(sourceTarget, 1, 16, 16) < 0.99f || !foldedStateRestored())
				return fail("destination-leg occlusion contaminated the restored source leg or renderer state");
			renderSystem->renderShadowDomain("GpuTestFoldedDestination", { apertureFrame });
			if (readCubeDepth(destinationTarget, 1, 16, 16) < 0.99f)
				return fail("removing the destination-leg occluder did not restore its open path");
			renderSystem->renderToScreen();
			renderSystem->resetViewport();

			targets.clear();
			if (!releasedTarget.expired()) return fail("cleared graph target remains referenced");
			renderSystem->renderToScreen();
			return true;
		}
		catch (MppException const& exception)
		{
			renderSystem->renderToScreen();
			return fail(stage + ": " + exception.what() + " at " + exception.getFile() + ":" + std::to_string(exception.getLine()));
		}
		catch (std::exception const& exception)
		{
			renderSystem->renderToScreen();
			return fail(stage + ": " + exception.what());
		}
	}
}
