#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <sstream>
#include <tuple>

#include "mpp/RenderGraphExecutor.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphScenePass.h"
#include "mpp/RenderGraphTemplate.h"
#include "mpp/Caps.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/GpuDebugScope.h"
#include "mpp/MppException.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderTexture.h"

using namespace std;

namespace mpp
{
	namespace
	{
		class GraphFramebufferTarget final : public RenderTarget
		{
			GLuint mFramebuffer{ 0 };
			vector<GLenum> mDrawBuffers;
			vector<RenderTexture*> mMipTargets;
			// Cached views must keep every attached texture object alive. OpenGL keeps
			// a deleted attachment alive internally, but the engine object also owns
			// mip generation and dimensions used when the view is activated.
			vector<RenderTargetPtr> mAttachments;

			static RenderTexture* requireRenderTexture(RenderTargetPtr const& target)
			{
				auto texture = dynamic_cast<RenderTexture*>(target.get());
				if (!texture)
				{
					THROW_MPP("A multi-attachment render graph pass requires RenderTexture-backed outputs.", __LINE__, __FILE__, __func__);
				}
				return texture;
			}

			void deactivate() override
			{
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
				for (auto target : mMipTargets) target->generateMipMaps();
			}

			void activate() override
			{
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer));
				if (!mDrawBuffers.empty()) GL_CHECK(glDrawBuffers((GLsizei)mDrawBuffers.size(), mDrawBuffers.data()));
				else GL_CHECK(glDrawBuffer(GL_NONE));
			}

		public:
			GraphFramebufferTarget(string const& name, vector<RenderTargetPtr> const& colours, vector<uint32_t> const& colourMips, vector<uint32_t> const& colourFaces, RenderTargetPtr const& depth, uint32_t depthMip, uint32_t depthFace)
				: RenderTarget(colours.empty() ? max<size_t>(1, depth->getWidth() >> depthMip) : max<size_t>(1, colours.front()->getWidth() >> colourMips.front()), colours.empty() ? max<size_t>(1, depth->getHeight() >> depthMip) : max<size_t>(1, colours.front()->getHeight() >> colourMips.front()))
				, mAttachments(colours)
			{
				if (depth) mAttachments.push_back(depth);
				GL_CHECK(glGenFramebuffers(1, &mFramebuffer));
				struct PendingFramebuffer
				{
					GLuint* id;
					bool committed{ false };
					~PendingFramebuffer() { if (!committed && *id != 0) { glDeleteFramebuffers(1, id); *id = 0; } }
				} pending{ &mFramebuffer };
				// glGenFramebuffers reserves a name; binding it creates the object.
				// Labeling a never-bound name is GL_INVALID_VALUE on strict drivers.
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer));
				GL_CHECK(glObjectLabel(GL_FRAMEBUFFER, mFramebuffer, -1, ("RenderGraphPass: " + name).c_str()));
				for (size_t index = 0; index < colours.size(); ++index)
				{
					auto texture = requireRenderTexture(colours[index]);
					if (colourMips[index] == 0) mMipTargets.push_back(texture);
					if (max<size_t>(1, texture->getWidth() >> colourMips[index]) != mWidth || max<size_t>(1, texture->getHeight() >> colourMips[index]) != mHeight)
					{
						THROW_MPP("Render graph pass attachment dimensions do not match.", __LINE__, __FILE__, __func__);
					}
					GLenum attachment = (GLenum)(GL_COLOR_ATTACHMENT0 + index);
					auto target = colourFaces[index] == GraphNoCubeFace ? texture->getAttachmentTextureTarget() : GL_TEXTURE_CUBE_MAP_POSITIVE_X + colourFaces[index];
					GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, target, texture->getColourAttachmentId(0), (GLint)colourMips[index]));
					mDrawBuffers.push_back(attachment);
				}
				if (depth)
				{
					auto texture = requireRenderTexture(depth);
					if (depthMip == 0) mMipTargets.push_back(texture);
					if (max<size_t>(1, texture->getWidth() >> depthMip) != mWidth || max<size_t>(1, texture->getHeight() >> depthMip) != mHeight)
					{
						THROW_MPP("Render graph depth attachment dimensions do not match colour attachments.", __LINE__, __FILE__, __func__);
					}
					auto target = depthFace == GraphNoCubeFace ? texture->getAttachmentTextureTarget() : GL_TEXTURE_CUBE_MAP_POSITIVE_X + depthFace;
					GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, texture->hasStencilBuffer() ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, target, texture->getDepthTextureId(), (GLint)depthMip));
				}
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					THROW_MPP("Render graph pass framebuffer is incomplete.", __LINE__, __FILE__, __func__);
				}
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
				pending.committed = true;
			}

			~GraphFramebufferTarget() override
			{
				if (mFramebuffer != 0) glDeleteFramebuffers(1, &mFramebuffer);
			}
		};

		void clearPassOutputs(RenderSystem& renderSystem, GraphPassInfo const& pass)
		{
			auto const savedState = renderSystem.captureRasterState(pass.colourOutputs.size());
			renderSystem.forceRenderWriteMasks(true, {});
			for (size_t index = 0; index < pass.colourOutputs.size(); ++index)
			{
				auto const& output = pass.colourOutputs[index];
				if (output.load == GraphLoadOp::Clear)
				{
					GL_CHECK(glClearBufferfv(GL_COLOR, (GLint)index, &output.clearColour.x));
				}
			}
			for (auto const& output : pass.depthOutputs)
			{
				if (output.load == GraphLoadOp::Clear)
				{
					GL_CHECK(glClearBufferfv(GL_DEPTH, 0, &output.clearDepth));
				}
			}
			renderSystem.applyRasterState(savedState, savedState.colourWriteMasks.size(), 0, 0);
		}

		class GraphRasterStateScope
		{
			RenderSystem* mRenderSystem{ nullptr };
			GraphRasterState mSavedState;
			size_t mColourOutputs{ 0 }, mWidth{ 0 }, mHeight{ 0 };

		public:
			GraphRasterStateScope(RenderSystem* renderSystem, GraphRasterState const& state, size_t colourOutputs, size_t width, size_t height)
				: mRenderSystem(state.explicitState ? renderSystem : nullptr), mColourOutputs(colourOutputs), mWidth(width), mHeight(height)
			{
				if (!mRenderSystem) return;
				mSavedState = mRenderSystem->captureRasterState(colourOutputs);
				mRenderSystem->applyRasterState(state, colourOutputs, width, height);
			}
			~GraphRasterStateScope()
			{
				if (!mRenderSystem) return;
				mRenderSystem->applyRasterState(mSavedState, mColourOutputs, mWidth, mHeight);
				mRenderSystem->debugVerifyRasterStateCache();
			}
		};

		optional<GraphImageCapture> captureStoredOutput(RenderGraph const& graph, GraphPassInfo const& pass,
			RenderGraphTargets const& targets, GraphImageHandle image, uint32_t mipLevel, uint32_t cubeFace, bool depth)
		{
			auto texture = dynamic_cast<RenderTexture*>(targets.get(image).get());
			if (!texture) return nullopt; // Presentation/default-framebuffer output.
			auto const info = graph.getImageInfo(image);
			bool const cube = info.desc.shape == GraphImageShape::CubeMap;
			if (cube && cubeFace == GraphNoCubeFace) return nullopt;
			GraphImageCapture capture;
			capture.passName = pass.name;
			capture.imageName = info.name;
			capture.width = (uint32_t)max<size_t>(1, texture->getWidth() >> mipLevel);
			capture.height = (uint32_t)max<size_t>(1, texture->getHeight() >> mipLevel);
			capture.depth = depth;
			capture.cubeFace = cubeFace;
			GLint previousPackAlignment = 0;
			GL_CHECK(glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment));
			GL_CHECK(glPixelStorei(GL_PACK_ALIGNMENT, 1));
			struct PackAlignmentRestore
			{
				GLint value;
				~PackAlignmentRestore() { glPixelStorei(GL_PACK_ALIGNMENT, value); }
			} packAlignmentRestore{ previousPackAlignment };
			auto const bindTarget = cube ? (GLenum)GL_TEXTURE_CUBE_MAP : (GLenum)GL_TEXTURE_2D;
			auto const bindingQuery = cube ? (GLenum)GL_TEXTURE_BINDING_CUBE_MAP : (GLenum)GL_TEXTURE_BINDING_2D;
			auto const imageTarget = cube ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeFace) : (GLenum)GL_TEXTURE_2D;
			GLint previousTexture = 0;
			GL_CHECK(glGetIntegerv(bindingQuery, &previousTexture));
			struct TextureBindingRestore
			{
				GLenum target;
				GLuint texture;
				~TextureBindingRestore() { glBindTexture(target, texture); }
			} textureBindingRestore{ bindTarget, (GLuint)previousTexture };
			if (depth)
			{
				auto const id = texture->getDepthTextureId();
				if (!id) return nullopt;
				vector<float> values((size_t)capture.width * capture.height);
				GL_CHECK(glBindTexture(bindTarget, id));
				GL_CHECK(glGetTexImage(imageTarget, (GLint)mipLevel, GL_DEPTH_COMPONENT, GL_FLOAT, values.data()));
				GL_CHECK(glBindTexture(bindTarget, (GLuint)previousTexture));
				capture.pixels.resize(values.size() * 3);
				for (size_t index = 0; index < values.size(); ++index)
				{
					auto const value = (uint8_t)round(clamp(1.0f - values[index], 0.0f, 1.0f) * 255.0f);
					capture.pixels[index * 3] = capture.pixels[index * 3 + 1] = capture.pixels[index * 3 + 2] = value;
				}
			}
			else
			{
				auto const id = texture->getColourAttachmentId(0);
				if (!id) return nullopt;
				auto const pixelCount = (size_t)capture.width * capture.height;
				capture.pixels.resize(pixelCount * 3);
				GL_CHECK(glBindTexture(bindTarget, id));
				auto const format = info.desc.format;
				bool const redOnly = format == GraphImageFormat::R8 || format == GraphImageFormat::R16f || format == GraphImageFormat::R32f;
				bool const redGreen = format == GraphImageFormat::Rg8 || format == GraphImageFormat::Rg16f || format == GraphImageFormat::Rg32f;
				bool const hdr = format == GraphImageFormat::Rgba16f || format == GraphImageFormat::Rgba32f || format == GraphImageFormat::R11g11b10f;
				if (redOnly || redGreen)
				{
					auto const channels = redOnly ? 1u : 2u;
					vector<float> values(pixelCount * channels);
					GL_CHECK(glGetTexImage(imageTarget, (GLint)mipLevel, redOnly ? GL_RED : GL_RG, GL_FLOAT, values.data()));
					bool const signedValues = info.name.find("Normal") != string::npos || info.name.find("normal") != string::npos;
					for (size_t index = 0; index < pixelCount; ++index)
					{
						auto encode = [&](float value)
						{
							if (signedValues) value = value * 0.5f + 0.5f;
							return (uint8_t)round(clamp(value, 0.0f, 1.0f) * 255.0f);
						};
						auto const red = encode(values[index * channels]);
						auto const green = redOnly ? red : encode(values[index * channels + 1]);
						capture.pixels[index * 3] = red;
						capture.pixels[index * 3 + 1] = green;
						capture.pixels[index * 3 + 2] = redOnly ? red : 0;
					}
				}
				else if (hdr)
				{
					vector<float> values(pixelCount * 3);
					GL_CHECK(glGetTexImage(imageTarget, (GLint)mipLevel, GL_RGB, GL_FLOAT, values.data()));
					for (size_t index = 0; index < values.size(); ++index)
					{
						auto const value = max(values[index], 0.0f);
						capture.pixels[index] = (uint8_t)round((value / (1.0f + value)) * 255.0f);
					}
				}
				else
				{
					GL_CHECK(glGetTexImage(imageTarget, (GLint)mipLevel, GL_RGB, GL_UNSIGNED_BYTE, capture.pixels.data()));
				}
				GL_CHECK(glBindTexture(bindTarget, (GLuint)previousTexture));
			}
			auto const rowSize = (size_t)capture.width * 3;
			for (size_t y = 0; y < capture.height / 2; ++y)
			{
				auto top = capture.pixels.begin() + y * rowSize;
				auto bottom = capture.pixels.begin() + (capture.height - y - 1) * rowSize;
				swap_ranges(top, top + rowSize, bottom);
			}
			return capture;
		}

		void discardDontCareOutputs(GraphPassInfo const& pass, RenderGraphExecutionContext const& context, bool defaultFramebuffer)
		{
			if (!GLEW_VERSION_4_3 && !GLEW_ARB_invalidate_subdata) return;
			// The default framebuffer names its buffers GL_COLOR/GL_DEPTH/GL_STENCIL,
			// not GL_COLOR_ATTACHMENT0 -- passing an attachment enum there is
			// GL_INVALID_ENUM, which is what authoring store="dontCare" on a
			// presentation output used to produce.
			vector<GLenum> attachments;
			for (size_t index = 0; index < pass.colourOutputs.size(); ++index)
			{
				if (pass.colourOutputs[index].store != GraphStoreOp::DontCare) continue;
				if (!defaultFramebuffer) { attachments.push_back((GLenum)(GL_COLOR_ATTACHMENT0 + index)); continue; }
				// The default framebuffer has exactly one colour buffer however many
				// outputs the pass declares, so name it once.
				if (find(attachments.begin(), attachments.end(), (GLenum)GL_COLOR) == attachments.end()) attachments.push_back(GL_COLOR);
			}
			for (auto const& output : pass.depthOutputs)
			{
				if (output.store != GraphStoreOp::DontCare) continue;
				if (defaultFramebuffer) { attachments.push_back(GL_DEPTH); continue; }
				// GraphFramebufferTarget attaches a packed depth-stencil format as
				// GL_DEPTH_STENCIL_ATTACHMENT, so invalidating GL_DEPTH_ATTACHMENT
				// would name something that is not attached and leave the stencil
				// aspect stored regardless.
				auto const* texture = dynamic_cast<RenderTexture const*>(context.getImage(output.image).get());
				attachments.push_back(texture && texture->hasStencilBuffer() ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT);
			}
			if (!attachments.empty()) GL_CHECK(glInvalidateFramebuffer(GL_FRAMEBUFFER, (GLsizei)attachments.size(), attachments.data()));
		}
	}

	struct RenderGraphExecutor::FramebufferViewCache
	{
		struct Attachment
		{
			uint32_t textureTarget{ 0 };
			uint32_t textureId{ 0 };
			uint32_t mipLevel{ 0 };
			uint32_t cubeFace{ GraphNoCubeFace };
			uint32_t aspect{ 0 }; // 0 colour, 1 depth, 2 packed depth/stencil.
			uint64_t width{ 0 };
			uint64_t height{ 0 };
			bool operator <(Attachment const& other) const
			{
				return tie(textureTarget, textureId, mipLevel, cubeFace, aspect, width, height) <
					tie(other.textureTarget, other.textureId, other.mipLevel, other.cubeFace, other.aspect, other.width, other.height);
			}
		};
		struct Key
		{
			vector<Attachment> drawBuffers;
			optional<Attachment> depth;
			bool operator <(Key const& other) const
			{
				return tie(drawBuffers, depth) < tie(other.drawBuffers, other.depth);
			}
		};

		RenderGraphTargets const* targets{ nullptr };
		uint64_t targetGeneration{ 0 };
		map<Key, RenderTargetPtr> views;
	};

	RenderGraphExecutionContext::RenderGraphExecutionContext(RenderGraphTargets const* targets, UniformCollection const* parameters, RenderGraphFrameContext const* frame, GraphPassInfo const* pass)
		: mTargets(targets)
		, mParameters(parameters)
		, mFrame(frame)
		, mPass(pass)
	{
	}

	RenderTargetPtr RenderGraphExecutionContext::getImage(GraphImageHandle image) const
	{
		return mTargets ? mTargets->get(image) : nullptr;
	}

	UniformCollection const& RenderGraphExecutionContext::getParameters() const
	{
		if (!mParameters) THROW_MPP("Render graph pass has no parameter collection.", __LINE__, __FILE__, __func__);
		return *mParameters;
	}

	RenderGraphFrameContext const& RenderGraphExecutionContext::getFrame() const
	{
		if (!mFrame) THROW_MPP("Render graph execution has no frame context.", __LINE__, __FILE__, __func__);
		return *mFrame;
	}

	GraphPassInfo const& RenderGraphExecutionContext::getPass() const
	{
		if (!mPass) THROW_MPP("Render graph execution has no pass metadata.", __LINE__, __FILE__, __func__);
		return *mPass;
	}

	RenderGraphExecutor::RenderGraphExecutor(RenderSystem* renderSystem)
		: mRenderSystem(renderSystem)
	{
		if (!mRenderSystem)
		{
			THROW_MPP("Render graph executor requires a RenderSystem.", __LINE__, __FILE__, __func__);
		}
		mGpuTimingSupported = GLEW_VERSION_3_3 || GLEW_ARB_timer_query;
	}

	RenderGraphExecutor::~RenderGraphExecutor()
	{
		clearGpuTimings();
	}

	void RenderGraphExecutor::clearGpuTimings()
	{
		for (auto const& frame : mPendingGpuTimings) for (auto const& query : frame)
		{
			GLuint ids[] = { query.begin, query.end };
			glDeleteQueries(2, ids);
		}
		mPendingGpuTimings.clear();
		mGpuTimings.clear();
	}

	void RenderGraphExecutor::collectGpuTimings()
	{
		while (!mPendingGpuTimings.empty())
		{
			auto const& frame = mPendingGpuTimings.front();
			bool available = true;
			for (auto const& query : frame)
			{
				GLint ready = GL_FALSE;
				GL_CHECK(glGetQueryObjectiv(query.end, GL_QUERY_RESULT_AVAILABLE, &ready));
				if (ready == GL_FALSE) { available = false; break; }
			}
			if (!available) break;
			for (auto const& query : frame)
			{
				GLuint64 begin = 0, end = 0;
				GL_CHECK(glGetQueryObjectui64v(query.begin, GL_QUERY_RESULT, &begin));
				GL_CHECK(glGetQueryObjectui64v(query.end, GL_QUERY_RESULT, &end));
				mGpuTimings[query.name] = { query.name, end >= begin ? double(end - begin) / 1000000.0 : 0.0 };
				GLuint ids[] = { query.begin, query.end };
				GL_CHECK(glDeleteQueries(2, ids));
			}
			mPendingGpuTimings.pop_front();
		}
	}


	void RenderGraphExecutor::setPassCallback(string const& passName, function<void(RenderGraphExecutionContext const&)> callback)
	{
		if (passName.empty() || !callback)
		{
			THROW_MPP("Render graph pass callback requires a pass name and function.", __LINE__, __FILE__, __func__);
		}
		mCallbacks[passName] = move(callback);
	}

	void RenderGraphExecutor::setPassCallback(RenderGraph const& graph, GraphPassHandle pass, function<void(RenderGraphExecutionContext const&)> callback)
	{
		setPassCallback(graph.getPassInfo(pass).name, move(callback));
	}

	void RenderGraphExecutor::setPassFactoryRegistry(RenderGraphPassFactoryRegistry const* registry)
	{
		mFactoryRegistry = registry;
	}

	void RenderGraphExecutor::setFrameContext(RenderGraphFrameContext const* frameContext)
	{
		mFrameContext = frameContext;
	}

	void RenderGraphExecutor::setPassParameterOverrides(string const& passName, UniformCollection const& parameters)
	{
		if (passName.empty()) THROW_MPP("Render graph pass parameter overrides require a pass name.", __LINE__, __FILE__, __func__);
		mParameterOverrides[passName] = parameters;
	}

	void RenderGraphExecutor::setPassParameterOverrides(RenderGraph const& graph, GraphPassHandle pass, UniformCollection const& parameters)
	{
		setPassParameterOverrides(graph.getPassInfo(pass).name, parameters);
	}

	void RenderGraphExecutor::clearPassCallbacks()	{
		mCallbacks.clear();
		mScenePasses.clear();
	}

	vector<GraphPassExecutionStats> const& RenderGraphExecutor::getLastExecutionStats() const
	{
		return mLastExecutionStats;
	}

	vector<GraphPassHandle> const& RenderGraphExecutor::getLastExecutionOrder() const
	{
		return mLastExecutionOrder;
	}

	string const& RenderGraphExecutor::getLastFailedPassName() const
	{
		return mLastFailedPassName;
	}

	GraphFramebufferCacheStats RenderGraphExecutor::getFramebufferCacheStats() const
	{
		auto result = mFramebufferCacheStats;
		result.entries = mFramebufferViews ? mFramebufferViews->views.size() : 0;
		return result;
	}

	void RenderGraphExecutor::requestImageCapture()
	{
		mCaptureNextExecution = true;
	}

	vector<GraphImageCapture> RenderGraphExecutor::takeImageCaptures()
	{
		auto captures = move(mLastImageCaptures);
		mLastImageCaptures.clear();
		return captures;
	}

	void RenderGraphExecutor::synchronizeFramebufferViews(RenderGraphTargets const& targets)
	{
		if (!mFramebufferViews) return;
		auto& cache = *mFramebufferViews;
		if (cache.targets == &targets && cache.targetGeneration == targets.getGeneration()) return;
		if (!cache.views.empty()) ++mFramebufferCacheStats.invalidations;
		cache.views.clear();
		cache.targets = &targets;
		cache.targetGeneration = targets.getGeneration();
	}

	RenderTargetPtr RenderGraphExecutor::getFramebufferView(string const& name, RenderGraphTargets const& targets,
		vector<RenderTargetPtr> const& colours, vector<uint32_t> const& colourMips, vector<uint32_t> const& colourFaces,
		RenderTargetPtr const& depth, uint32_t depthMip, uint32_t depthFace)
	{
		if (!mFramebufferViews) mFramebufferViews = make_unique<FramebufferViewCache>();
		synchronizeFramebufferViews(targets);
		auto& cache = *mFramebufferViews;

		auto attachmentKey = [](RenderTargetPtr const& target, uint32_t mipLevel, uint32_t cubeFace, bool depthAttachment)
		{
			auto texture = dynamic_cast<RenderTexture*>(target.get());
			if (!texture)
				THROW_MPP("A cached render graph framebuffer requires RenderTexture-backed outputs.", __LINE__, __FILE__, __func__);
			FramebufferViewCache::Attachment key;
			key.textureTarget = texture->getAttachmentTextureTarget();
			key.textureId = depthAttachment ? texture->getDepthTextureId() : texture->getColourAttachmentId(0);
			key.mipLevel = mipLevel;
			key.cubeFace = cubeFace;
			key.aspect = depthAttachment ? (texture->hasStencilBuffer() ? 2u : 1u) : 0u;
			key.width = max<size_t>(1, texture->getWidth() >> mipLevel);
			key.height = max<size_t>(1, texture->getHeight() >> mipLevel);
			return key;
		};

		FramebufferViewCache::Key key;
		key.drawBuffers.reserve(colours.size());
		for (size_t index = 0; index < colours.size(); ++index)
			key.drawBuffers.push_back(attachmentKey(colours[index], colourMips[index], colourFaces[index], false));
		if (depth) key.depth = attachmentKey(depth, depthMip, depthFace, true);
		auto const found = cache.views.find(key);
		if (found != cache.views.end())
		{
			++mFramebufferCacheStats.hits;
			return found->second;
		}
		++mFramebufferCacheStats.misses;
		auto view = make_shared<GraphFramebufferTarget>(name, colours, colourMips, colourFaces, depth, depthMip, depthFace);
		cache.views.emplace(move(key), view);
		return view;
	}

	void RenderGraphExecutor::execute(RenderGraph const& graph, RenderGraphTargets const& targets, Caps const& caps)
	{
		auto compiled = graph.compile(caps);
		if (!compiled.valid)
		{
			ostringstream message;
			message << "Cannot execute invalid render graph:";
			for (auto const& diagnostic : compiled.diagnostics) message << "\n- " << diagnostic;
			THROW_MPP(message.str(), __LINE__, __FILE__, __func__);
		}
		synchronizeFramebufferViews(targets);
		mLastExecutionStats.clear();
		mLastFailedPassName.clear();
		bool const captureThisExecution = mCaptureNextExecution;
		mCaptureNextExecution = false;
		if (captureThisExecution) mLastImageCaptures.clear();
		if (mGpuTimingSupported) collectGpuTimings();
		vector<GpuTimingQuery> frameGpuQueries;
		auto cleanupQueries = [&](void*)
		{
			for (auto const& query : frameGpuQueries)
			{
				GLuint ids[] = { query.begin, query.end };
				glDeleteQueries(2, ids);
			}
		};
		unique_ptr<void, decltype(cleanupQueries)> queryCleanup((void*)1, cleanupQueries);
		bool const recordGpuTimings = mGpuTimingSupported && mPendingGpuTimings.size() < 8;
		for (auto const passHandle : compiled.passOrder)
		{
			auto const pass = graph.getPassInfo(passHandle);
			auto const statsBefore = mRenderSystem->getCurrentRenderInfo();
			auto const passStart = chrono::steady_clock::now();
			GpuDebugScope passScope(renderFlowPassRenderDocLabel(passHandle, pass.name, pass.type));
			auto const explicitCallback = mCallbacks.find(pass.name);
			RenderGraphPassCallback callback = explicitCallback == mCallbacks.end() ? RenderGraphPassCallback() : explicitCallback->second;
			RenderGraphScenePass* scenePass = nullptr;
			if (!callback && mFactoryRegistry && !pass.callbackFactory.empty())
			{
				callback = mFactoryRegistry->findFactory(pass.callbackFactory);
				if (!callback)
				{
					auto found = mScenePasses.find(pass.name);
					if (found == mScenePasses.end())
					{
						auto created = mFactoryRegistry->createScenePass(pass.callbackFactory);
						if (created) found = mScenePasses.emplace(pass.name, std::move(created)).first;
					}
					if (found != mScenePasses.end()) scenePass = found->second.get();
				}
			}
			auto override = mParameterOverrides.find(pass.name);
			RenderGraphExecutionContext context(&targets, override == mParameterOverrides.end() ? &pass.parameters : &override->second, mFrameContext, &pass);
			bool const declarativeFullscreen = !callback && !scenePass && mExecutingTemplate && pass.type == GraphPassType::Fullscreen && !pass.programResource.empty();
			if (!callback && !scenePass && !declarativeFullscreen)
			{
				THROW_MPP("No callback registered for render graph pass '" + pass.name + "'" +
					(pass.callbackFactory.empty() ? "." : " (factory '" + pass.callbackFactory + "')."), __LINE__, __FILE__, __func__);
			}
			vector<RenderTargetPtr> colours;
			vector<uint32_t> colourMips, colourFaces;
			for (auto const& output : pass.colourOutputs)
			{
				auto target = targets.getWriteTarget(output.image);
				if (!target) THROW_MPP("Render graph colour output has no allocated or imported target.", __LINE__, __FILE__, __func__);
				colours.push_back(target);
				colourMips.push_back(output.mipLevel);
				colourFaces.push_back(output.cubeFace);
			}
			RenderTargetPtr depth;
			uint32_t depthMip = 0, depthFace = GraphNoCubeFace;
			if (!pass.depthOutputs.empty())
			{
				depth = targets.getWriteTarget(pass.depthOutputs.front().image);
				depthMip = pass.depthOutputs.front().mipLevel;
				depthFace = pass.depthOutputs.front().cubeFace;
				if (!depth) THROW_MPP("Render graph depth output has no allocated or imported target.", __LINE__, __FILE__, __func__);
			}

			if (colours.empty() && !depth)
			{
				THROW_MPP("Render graph executor supports graphics passes with at least one output.", __LINE__, __FILE__, __func__);
			}
			RenderTargetPtr passTarget;
			if (colours.size() == 1 && colourMips.front() == 0 && colourFaces.front() == GraphNoCubeFace && !depth)
			{
				passTarget = colours.front();
			}
			else
			{
				passTarget = getFramebufferView(pass.name, targets, colours, colourMips, colourFaces, depth, depthMip, depthFace);
			}
			map<RenderTexture*, uint32_t> mipViews;
			for (auto const& binding : pass.samplerBindings)
			{
				if (binding.mipLevel == UINT32_MAX) continue;
				auto texture = dynamic_cast<RenderTexture*>(targets.get(binding.image).get());
				if (!texture) THROW_MPP("Explicit graph mip views require RenderTexture inputs.", __LINE__, __FILE__, __func__);
				auto existing = mipViews.find(texture);
				if (existing != mipViews.end() && existing->second != binding.mipLevel)
					THROW_MPP("One graph pass cannot bind different mip views of the same texture without texture-view support.", __LINE__, __FILE__, __func__);
				mipViews[texture] = binding.mipLevel;
			}
			mRenderSystem->pushRenderTarget(passTarget);
			mRenderSystem->setExpectedGraphColourOutputs(pass.colourOutputs.size());
			bool const imagePass = pass.type == GraphPassType::Fullscreen || pass.type == GraphPassType::Present;
			GraphRasterState imagePassSavedState;
			if (imagePass)
			{
				// Fullscreen programs use the renderer's identity 2D transform. Graph
				// execution enters from a 3D scene projection, so preserve it and
				// install the same transform used by the manual post-effect path.
				imagePassSavedState = mRenderSystem->captureRasterState(pass.colourOutputs.size());
				mRenderSystem->pushProjectionMatrix();
				mRenderSystem->pushCameraMatrix();
				mRenderSystem->pushModelMatrix();
				mRenderSystem->setProjection2dOrthographic();
				mRenderSystem->resetTransform();
				// The shared fullscreen quad is authored at window dimensions. Graph
				// targets commonly match an editor viewport instead, so scale the quad
				// to this pass target. Without this, every post-process samples only a
				// clipped UV subregion and ping-pong blur visibly drifts each pass.
				mRenderSystem->scaleTransform2d(glm::vec2(
					(float)passTarget->getWidth() / (float)mRenderSystem->getWindowWidth(),
					(float)passTarget->getHeight() / (float)mRenderSystem->getWindowHeight()));
				auto imageState = imagePassSavedState;
				imageState.depthTest = false;
				imageState.depthWrite = false;
				imageState.cullMode = GraphCullMode::None;
				imageState.scissor = false;
				mRenderSystem->applyRasterState(imageState, pass.colourOutputs.size(), passTarget->getWidth(), passTarget->getHeight());
			}
			auto restoreImagePassState = [&]()
			{
				if (!imagePass) return;
				mRenderSystem->applyRasterState(imagePassSavedState, pass.colourOutputs.size(), passTarget->getWidth(), passTarget->getHeight());
				mRenderSystem->popModelMatrix();
				mRenderSystem->popCameraMatrix();
				mRenderSystem->popProjectionMatrix();
			};
			GpuTimingQuery gpuQuery;
			bool gpuQueryStarted = false;
			mRenderSystem->beginRenderFlowPass(passHandle, pass.name);
			try
			{
				if (recordGpuTimings)
				{
					gpuQuery.pass = passHandle;
					gpuQuery.name = pass.name;
					GLuint ids[2]{};
					GL_CHECK(glGenQueries(2, ids));
					gpuQuery.begin = ids[0]; gpuQuery.end = ids[1]; gpuQueryStarted = true;
					GL_CHECK(glQueryCounter(gpuQuery.begin, GL_TIMESTAMP));
				}
				for (auto const& view : mipViews) view.first->applyMipView(view.second);
				mRenderSystem->setViewport(0, 0, passTarget->getWidth(), passTarget->getHeight());
				{
					GpuDebugScope loadScope("Load/Clear Attachments");
					clearPassOutputs(*mRenderSystem, pass);
				}
				{
					GpuDebugScope executeScope("Execute: " + pass.name);
					GraphRasterStateScope rasterState(mRenderSystem, pass.rasterState, pass.colourOutputs.size(), passTarget->getWidth(), passTarget->getHeight());
					if (callback) callback(context);
					else if (scenePass) scenePass->execute(context);
					else
					{
						vector<pair<string, Texture*>> samplers;
						for (auto const& binding : pass.samplerBindings)
							samplers.push_back({ binding.sampler, dynamic_cast<Texture*>(context.getImage(binding.image).get()) });
						mRenderSystem->renderGraphFullscreen(mExecutingTemplate->getProgram(passHandle), samplers, context.getParameters());
					}
				}
				{
					GpuDebugScope storeScope("Store/Resolve Attachments");
					discardDontCareOutputs(pass, context, passTarget == mRenderSystem->getScreenRenderTarget());
					for (auto const& output : pass.colourOutputs) if (output.store == GraphStoreOp::Store && targets.resolve(output.image, false) && mRenderSystem->isRenderFlowCaptureActive()){try{auto info=graph.getImageInfo(output.image);auto source=targets.getWriteTarget(output.image),destination=targets.get(output.image);RenderFlowResourceDesc sourceDesc{info.name+".v"+to_string(output.image.version)+".msaa",{(uint32_t)source->getWidth(),(uint32_t)source->getHeight()},info.desc.format,dynamic_cast<RenderTexture*>(source.get())->getSamples()};RenderFlowResourceDesc destinationDesc{info.name+".v"+to_string(output.image.version)+".resolved",{(uint32_t)destination->getWidth(),(uint32_t)destination->getHeight()},info.desc.format,1};mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::MsaaResolve,info.name+".v"+to_string(output.image.version),output.image,true,{}, {},false,{std::move(sourceDesc)},{std::move(destinationDesc)});}catch(...){mRenderSystem->failRenderFlowCapture();}}
					for (auto const& output : pass.depthOutputs) if (output.store == GraphStoreOp::Store && targets.resolve(output.image, true) && mRenderSystem->isRenderFlowCaptureActive()){try{auto info=graph.getImageInfo(output.image);auto source=targets.getWriteTarget(output.image),destination=targets.get(output.image);RenderFlowResourceDesc sourceDesc{info.name+".v"+to_string(output.image.version)+".msaa",{(uint32_t)source->getWidth(),(uint32_t)source->getHeight()},info.desc.format,dynamic_cast<RenderTexture*>(source.get())->getSamples()};RenderFlowResourceDesc destinationDesc{info.name+".v"+to_string(output.image.version)+".resolved",{(uint32_t)destination->getWidth(),(uint32_t)destination->getHeight()},info.desc.format,1};mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::MsaaResolve,info.name+".v"+to_string(output.image.version),output.image,true,{}, {},true,{std::move(sourceDesc)},{std::move(destinationDesc)});}catch(...){mRenderSystem->failRenderFlowCapture();}}
					if (captureThisExecution)
					{
						for (auto const& output : pass.colourOutputs)
							if (output.store == GraphStoreOp::Store)
								if (auto capture = captureStoredOutput(graph, pass, targets, output.image, output.mipLevel, output.cubeFace, false))
									mLastImageCaptures.push_back(move(*capture));
						for (auto const& output : pass.depthOutputs)
							if (output.store == GraphStoreOp::Store)
								if (auto capture = captureStoredOutput(graph, pass, targets, output.image, output.mipLevel, output.cubeFace, true))
									mLastImageCaptures.push_back(move(*capture));
					}
				}
				for (auto const& view : mipViews) view.first->restoreMipView();
				restoreImagePassState();
				mRenderSystem->setExpectedGraphColourOutputs(0);
				mRenderSystem->popRenderTarget();
				if (gpuQueryStarted)
				{
					GL_CHECK(glQueryCounter(gpuQuery.end, GL_TIMESTAMP));
					frameGpuQueries.push_back(gpuQuery);
					gpuQueryStarted = false;
				}
				auto const statsAfter = mRenderSystem->getCurrentRenderInfo();
				GraphPassExecutionStats stats;
				stats.pass = passHandle;
				stats.name = pass.name;
				stats.cpuMilliseconds = chrono::duration<double, milli>(chrono::steady_clock::now() - passStart).count();
				stats.gpuTimingSupported = mGpuTimingSupported;
				// Already name-keyed, so the defensive name comparison this carried
				// alongside an index key has become the lookup itself.
				auto gpuTiming = mGpuTimings.find(pass.name);
				if (gpuTiming != mGpuTimings.end()) { stats.gpuMilliseconds = gpuTiming->second.milliseconds; stats.gpuTimingAvailable = true; }
				stats.primitivesSubmitted = static_cast<uint64_t>(max(0, statsAfter.primitivesRendered - statsBefore.primitivesRendered));
				stats.trianglesSubmitted = static_cast<uint64_t>(max(0, statsAfter.trianglesRendered - statsBefore.trianglesRendered));
				stats.fullscreenQuads = static_cast<uint64_t>(max(0, statsAfter.fullscreenQuads - statsBefore.fullscreenQuads));
				stats.colourOutputCount = static_cast<uint32_t>(pass.colourOutputs.size());
				stats.depthOutputCount = static_cast<uint32_t>(pass.depthOutputs.size());
				stats.storedDepthOutputCount = static_cast<uint32_t>(std::count_if(pass.depthOutputs.begin(), pass.depthOutputs.end(),
					[](auto const& output) { return output.store == GraphStoreOp::Store; }));
				stats.samplerBindingCount = static_cast<uint32_t>(pass.samplerBindings.size());
				for (size_t outputIndex = 0; outputIndex < pass.colourOutputs.size(); ++outputIndex)
				{
					auto const& output = pass.colourOutputs[outputIndex];
					if (auto texture = dynamic_cast<RenderTexture*>(targets.get(output.image).get()))
					{
						stats.maxColourOutputMipLevels = std::max(stats.maxColourOutputMipLevels, texture->getMipLevels());
						if (outputIndex == 0)
						{
							stats.primaryColourOutputName = graph.getImageInfo(output.image).name;
							stats.primaryColourOutputWidth = static_cast<uint32_t>(texture->getWidth());
							stats.primaryColourOutputHeight = static_cast<uint32_t>(texture->getHeight());
						}
					}
				}
				mLastExecutionStats.push_back(move(stats));
				mRenderSystem->endRenderFlowPass(passHandle, pass.name);
			}
			catch (...)
			{
				mLastFailedPassName = pass.name;
				if (gpuQueryStarted)
				{
					glQueryCounter(gpuQuery.end, GL_TIMESTAMP);
					GLuint ids[] = { gpuQuery.begin, gpuQuery.end };
					glDeleteQueries(2, ids);
					gpuQueryStarted = false;
				}
				for (auto const& view : mipViews) view.first->restoreMipView();
				restoreImagePassState();
				mRenderSystem->setExpectedGraphColourOutputs(0);
				mRenderSystem->popRenderTarget();
				mRenderSystem->abortRenderFlowPass();
				throw;
			}
		}
		if (!frameGpuQueries.empty()) mPendingGpuTimings.push_back(move(frameGpuQueries));
		mLastExecutionOrder = compiled.passOrder;
		queryCleanup.release();
	}

	void RenderGraphExecutor::execute(RenderGraphTemplate const& graphTemplate, RenderGraphTargets const& targets, Caps const& caps)
	{
		if (!graphTemplate.getGraph()) THROW_MPP("Cannot execute an unloaded RenderGraphTemplate.", __LINE__, __FILE__, __func__);
		mExecutingTemplate = &graphTemplate;
		try
		{
			execute(*graphTemplate.getGraph(), targets, caps);
			mExecutingTemplate = nullptr;
		}
		catch (...)
		{
			mExecutingTemplate = nullptr;
			throw;
		}
	}
}
