#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/ComputeProgram.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/GpuDebugScope.h"
#include "mpp/MppException.h"
#include "mpp/ParticleDrawProgram.h"
#include "mpp/RawShaderStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderTexture.h"
#include "mpp/ResourceManager.h"
#include "mpp/ShaderStorageBuffer.h"
#include "mpp/TrailShaders.h"
#include "mpp/TrailSystem.h"
#include "PersistentMappedBuffer.h"

using namespace std;

namespace mpp
{
	namespace
	{
		char const* UpdateProgramName = "__mpp_trail_update__";
		char const* DrawProgramName = "__mpp_trail_draw__";
		constexpr uint32_t PointBinding = 0u;
		constexpr uint32_t StateBinding = 1u;
		constexpr uint32_t ControlBinding = 2u;
		constexpr uint32_t CommandBinding = 3u;
		constexpr uint32_t BlendClassCount = 2u;

		uint32_t nextGeneration(uint32_t generation)
		{
			++generation;
			return generation == 0u ? 1u : generation;
		}

		float normalizedTime(float value)
		{
			return isfinite(value) ? clamp(value, 0.0f, 1.0f) : 0.0f;
		}

		float sampleCurve(ParticleCurve const& curve, float time)
		{
			if (curve.keys.empty()) return curve.defaultValue;
			auto keys = curve.keys;
			stable_sort(keys.begin(), keys.end(), [](auto const& left, auto const& right)
				{ return normalizedTime(left.time) < normalizedTime(right.time); });
			if (time <= normalizedTime(keys.front().time)) return keys.front().value;
			if (time >= normalizedTime(keys.back().time)) return keys.back().value;
			auto right = upper_bound(keys.begin(), keys.end(), time, [](float value, auto const& key)
				{ return value < normalizedTime(key.time); });
			auto const& left = *(right - 1);
			float const leftTime = normalizedTime(left.time);
			float const rightTime = normalizedTime(right->time);
			float const amount = rightTime > leftTime ? (time - leftTime) / (rightTime - leftTime) : 1.0f;
			return left.value + (right->value - left.value) * amount;
		}

		array<float, 3> sampleGradient(ParticleGradient const& gradient, float time)
		{
			if (gradient.keys.empty()) return gradient.defaultColour;
			auto keys = gradient.keys;
			stable_sort(keys.begin(), keys.end(), [](auto const& left, auto const& right)
				{ return normalizedTime(left.time) < normalizedTime(right.time); });
			if (time <= normalizedTime(keys.front().time)) return keys.front().colour;
			if (time >= normalizedTime(keys.back().time)) return keys.back().colour;
			auto right = upper_bound(keys.begin(), keys.end(), time, [](float value, auto const& key)
				{ return value < normalizedTime(key.time); });
			auto const& left = *(right - 1);
			float const leftTime = normalizedTime(left.time);
			float const rightTime = normalizedTime(right->time);
			float const amount = rightTime > leftTime ? (time - leftTime) / (rightTime - leftTime) : 1.0f;
			array<float, 3> result;
			for (size_t channel = 0; channel < result.size(); ++channel)
				result[channel] = left.colour[channel] + (right->colour[channel] - left.colour[channel]) * amount;
			return result;
		}
	}

	TrailSystem::TrailSystem(RenderSystem* renderSystem, ResourceManager* resourceManager)
		: mwRenderSystem(renderSystem), mwResourceManager(resourceManager)
	{
	}

	TrailSystem::~TrailSystem()
	{
		if (mCurveLut) glDeleteTextures(1, &mCurveLut);
		if (mVertexArray) glDeleteVertexArrays(1, &mVertexArray);
		mControlBuffer.reset();
		mIndirectCommands.reset();
		mStates.reset();
		mPoints.reset();
		auto releaseProgram = [this](ResourcePtr& resource)
		{
			if (!resource) return;
			resource->release(mwRenderSystem);
			if (!resource->isReferenced()) resource->destroy();
			resource.reset();
		};
		releaseProgram(mUpdateProgram);
		releaseProgram(mDrawProgram);
	}

	void TrailSystem::initialise()
	{
		if (mInitialised) return;
		mInitialised = true;
		if (!mwRenderSystem || !mwResourceManager) return;
		auto const& caps = mwRenderSystem->getCaps();
		if (!caps.supportsCompute || !caps.supportsMultiDrawIndirect || caps.maxShaderStorageBufferBindings < 4u) return;
		try
		{
			mWorkGroupSize = max(1u, min<uint32_t>(64u,
				min(caps.maxComputeWorkGroupSize[0], caps.maxComputeWorkGroupInvocations)));
			auto updateStream = make_shared<ComputeProgramStream>(mwResourceManager);
			updateStream->setSource(RawShaderStage::Compute, TrailUpdateComputeShader);
			updateStream->setDefine("MPP_TRAIL_WORK_GROUP_SIZE", to_string(mWorkGroupSize));
			updateStream->setDefine("MPP_TRAIL_MAX_POINTS", to_string(MaxPointCount));
			updateStream->setDefine("MPP_TRAIL_MAX_TRAILS", to_string(MaxTrailCount));
			mUpdateProgram = mwResourceManager->declareResource(UpdateProgramName, updateStream).first;
			mUpdateProgram->acquire(mwRenderSystem);
			mUpdateProgram->load();

			auto drawStream = make_shared<ParticleDrawProgramStream>(mwResourceManager);
			drawStream->setSource(RawShaderStage::Vertex, TrailDrawVertexShader);
			drawStream->setSource(RawShaderStage::Fragment, TrailDrawFragmentShader);
			drawStream->setDefine("MPP_TRAIL_MAX_POINTS", to_string(MaxPointCount));
			mDrawProgram = mwResourceManager->declareResource(DrawProgramName, drawStream).first;
			mDrawProgram->acquire(mwRenderSystem);
			mDrawProgram->load();
			GL_CHECK(glGenVertexArrays(1, &mVertexArray));
			if (!mVertexArray) THROW_MPP("Could not create the trail ribbon vertex array.", __LINE__, __FILE__, __func__);
			mAvailable = true;
		}
		catch (exception const& error)
		{
			mwRenderSystem->warnMessage(string("The trail primitive could not be initialised and is disabled. ") + error.what());
			mAvailable = false;
		}
	}

	void TrailSystem::validate(TrailSpecification const& specification)
	{
		if (specification.maximumPointCount < 2u || specification.maximumPointCount > MaxPointCount)
			throw invalid_argument("TrailSpecification maximumPointCount must be in [2, 256].");
		if (!isfinite(specification.pointLifetime) || specification.pointLifetime <= 0.0f ||
			!isfinite(specification.minimumPointDistance) || specification.minimumPointDistance < 0.0f ||
			!isfinite(specification.width) || specification.width < 0.0f || !isfinite(specification.uvScale) ||
			!isfinite(specification.emissiveIntensity) || specification.emissiveIntensity < 0.0f ||
			!isfinite(specification.softFadeDistance) || specification.softFadeDistance < 0.0f)
			throw invalid_argument("TrailSpecification requires finite non-negative dimensions and a positive point lifetime.");
		if (specification.blendClass != ParticleBlendClass::Additive && specification.blendClass != ParticleBlendClass::Alpha)
			throw invalid_argument("TrailSpecification supports additive or alpha blend classes.");
		if (!all_of(specification.tintAndAlpha.begin(), specification.tintAndAlpha.end(), [](float value) { return isfinite(value); }))
			throw invalid_argument("TrailSpecification tintAndAlpha values must be finite.");
	}

	vector<float> TrailSystem::bakeCurveRows(TrailSpecification const& specification)
	{
		vector<float> result(size_t(CurveSampleCount) * 2u * 4u, 1.0f);
		for (uint32_t sample = 0u; sample < CurveSampleCount; ++sample)
		{
			float const time = float(sample) / float(CurveSampleCount - 1u);
			result[size_t(sample) * 4u] = sampleCurve(specification.widthOverLife, time);
			auto const colour = sampleGradient(specification.colourOverLife, time);
			size_t const colourOffset = (size_t(CurveSampleCount) + sample) * 4u;
			copy(colour.begin(), colour.end(), result.begin() + colourOffset);
		}
		return result;
	}

	TrailSystem::Slot* TrailSystem::find(TrailHandle handle)
	{
		if (!handle || handle.index >= mSlots.size()) return nullptr;
		auto& slot = mSlots[handle.index];
		return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
	}

	TrailSystem::Slot const* TrailSystem::find(TrailHandle handle) const
	{
		if (!handle || handle.index >= mSlots.size()) return nullptr;
		auto const& slot = mSlots[handle.index];
		return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
	}

	TrailHandle TrailSystem::createTrail(TrailSpecification const& specification, glm::vec3 const& position)
	{
		validate(specification);
		if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))
			throw invalid_argument("TrailSystem::createTrail requires a finite position.");
		uint32_t index;
		if (!mFreeIndices.empty())
		{
			index = mFreeIndices.back();
			mFreeIndices.pop_back();
		}
		else
		{
			if (mSlots.size() >= MaxTrailCount) THROW_MPP("The trail capacity was exceeded.", __LINE__, __FILE__, __func__);
			index = uint32_t(mSlots.size());
			mSlots.emplace_back();
			mControls.emplace_back();
		}
		auto& slot = mSlots[index];
		slot.occupied = true;
		slot.stopping = false;
		slot.specification = specification;
		slot.historyGeneration = nextGeneration(slot.historyGeneration);
		auto& control = mControls[index];
		control = {};
		control.positionEnabled = { position.x, position.y, position.z, 1.0f };
		control.lifetimeDistanceUvWidth = { specification.pointLifetime, specification.minimumPointDistance,
			specification.uvScale, specification.width };
		control.tintAndAlpha = specification.tintAndAlpha;
		control.appearance = { specification.emissiveIntensity, specification.softFadeDistance, float(index * 2u), 0.0f };
		control.modes = { 1u, uint32_t(specification.blendClass), slot.historyGeneration, specification.maximumPointCount };
		mDirtyCurveSlots.push_back(index);
		return { index, slot.generation };
	}

	void TrailSystem::reclaim(uint32_t index)
	{
		if (index >= mSlots.size() || !mSlots[index].occupied) return;
		auto& slot = mSlots[index];
		slot.occupied = false;
		slot.stopping = false;
		slot.generation = nextGeneration(slot.generation);
		slot.historyGeneration = nextGeneration(slot.historyGeneration);
		mControls[index] = {};
		mControls[index].modes[2] = slot.historyGeneration;
		mFreeIndices.push_back(index);
	}

	void TrailSystem::destroyTrail(TrailHandle trail)
	{
		if (!find(trail)) return;
		reclaim(trail.index);
	}

	void TrailSystem::setTrailPosition(TrailHandle trail, glm::vec3 const& position)
	{
		if (!find(trail)) return;
		if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))
			throw invalid_argument("TrailSystem::setTrailPosition requires a finite position.");
		mControls[trail.index].positionEnabled[0] = position.x;
		mControls[trail.index].positionEnabled[1] = position.y;
		mControls[trail.index].positionEnabled[2] = position.z;
	}

	void TrailSystem::stopTrail(TrailHandle trail)
	{
		auto* slot = find(trail);
		if (!slot || slot->stopping) return;
		slot->stopping = true;
		slot->stopSeconds = mSimulationSeconds;
		mControls[trail.index].positionEnabled[3] = 0.0f;
	}

	void TrailSystem::startTrail(TrailHandle trail)
	{
		auto* slot = find(trail);
		if (!slot) return;
		if (slot->stopping)
		{
			slot->stopping = false;
			slot->historyGeneration = nextGeneration(slot->historyGeneration);
			mControls[trail.index].modes[2] = slot->historyGeneration;
		}
		mControls[trail.index].positionEnabled[3] = 1.0f;
	}

	void TrailSystem::clearTrail(TrailHandle trail)
	{
		auto* slot = find(trail);
		if (!slot) return;
		slot->historyGeneration = nextGeneration(slot->historyGeneration);
		mControls[trail.index].modes[2] = slot->historyGeneration;
	}

	size_t TrailSystem::getLiveTrailCount() const
	{
		return count_if(mSlots.begin(), mSlots.end(), [](auto const& slot) { return slot.occupied; });
	}

	void TrailSystem::ensureBuffersAllocated()
	{
		if (mBuffersAllocated) return;
		size_t const pointBytes = size_t(MaxTrailCount) * MaxPointCount * sizeof(TrailPointRecord);
		size_t const stateBytes = size_t(MaxTrailCount) * sizeof(TrailState);
		size_t const controlBytes = size_t(MaxTrailCount) * sizeof(TrailControlData);
		size_t const commandBytes = size_t(BlendClassCount) * MaxTrailCount * sizeof(ParticleDrawArraysIndirectCommand);
		size_t const largest = max({ pointBytes, stateBytes, controlBytes, commandBytes });
		if (largest > mwRenderSystem->getCaps().maxShaderStorageBlockSize)
			THROW_MPP("Trail history buffers exceed the GPU's maximum shader storage block size.", __LINE__, __FILE__, __func__);

		mPoints = make_unique<ShaderStorageBuffer>();
		mPoints->create(pointBytes, nullptr, "Trail position history points");
		mStates = make_unique<ShaderStorageBuffer>();
		mStates->create(stateBytes, nullptr, "Trail position history ring states");
		mIndirectCommands = make_unique<ShaderStorageBuffer>();
		mIndirectCommands->create(commandBytes, nullptr, "Trail ribbon indirect draw commands");
		GLint storageAlignment = 1;
		GL_CHECK(glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storageAlignment));
		mControlBuffer = make_unique<detail::PersistentMappedBuffer>();
		mControlBuffer->create(GL_SHADER_STORAGE_BUFFER, controlBytes, max(1, storageAlignment),
			mwRenderSystem->getCaps().streamingGeometry, mControls.data(), mControls.size() * sizeof(TrailControlData),
			"Trail CPU controls");

		GL_CHECK(glGenTextures(1, &mCurveLut));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, mCurveLut));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, CurveSampleCount, MaxTrailCount * 2u,
			0, GL_RGBA, GL_FLOAT, nullptr));
		GL_CHECK(glObjectLabel(GL_TEXTURE, mCurveLut, -1, "Trail width and colour RGBA16F LUT"));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		mBuffersAllocated = true;
	}

	void TrailSystem::uploadDirtyCurves()
	{
		if (mDirtyCurveSlots.empty()) return;
		sort(mDirtyCurveSlots.begin(), mDirtyCurveSlots.end());
		mDirtyCurveSlots.erase(unique(mDirtyCurveSlots.begin(), mDirtyCurveSlots.end()), mDirtyCurveSlots.end());
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, mCurveLut));
		for (uint32_t index : mDirtyCurveSlots)
		{
			if (index >= mSlots.size() || !mSlots[index].occupied) continue;
			auto rows = bakeCurveRows(mSlots[index].specification);
			GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, GLint(index * 2u), CurveSampleCount, 2u,
				GL_RGBA, GL_FLOAT, rows.data()));
		}
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		mDirtyCurveSlots.clear();
	}

	void TrailSystem::reclaimStoppedTrails()
	{
		for (uint32_t index = 0u; index < mSlots.size(); ++index)
		{
			auto const& slot = mSlots[index];
			if (slot.occupied && slot.stopping &&
				mSimulationSeconds - slot.stopSeconds >= slot.specification.pointLifetime)
				reclaim(index);
		}
	}

	void TrailSystem::simulate(float deltaSeconds)
	{
		deltaSeconds = clampParticleDeltaSeconds(deltaSeconds);
		mSimulationSeconds += deltaSeconds;
		reclaimStoppedTrails();
		if (!mAvailable || (mControls.empty() && !mBuffersAllocated)) return;
		if (!mBuffersAllocated)
		{
			if (getLiveTrailCount() == 0u) return;
			ensureBuffersAllocated();
		}
		uploadDirtyCurves();
		mControlBuffer->upload(mControls.data(), mControls.size() * sizeof(TrailControlData), 0,
			mControls.size() * sizeof(TrailControlData));
		mPoints->bindStorage(PointBinding);
		mStates->bindStorage(StateBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, ControlBinding, mControlBuffer->getBuffer(),
			GLintptr(mControlBuffer->getActiveOffset()), GLsizeiptr(mControls.size() * sizeof(TrailControlData))));
		mIndirectCommands->bindStorage(CommandBinding);
		auto* program = static_cast<ComputeProgram*>(mUpdateProgram.get());
		program->use();
		program->setUniform("TRAIL_COUNT", uint32_t(mControls.size()));
		program->setUniform("DELTA_SECONDS", deltaSeconds);
		program->dispatch((uint32_t(mControls.size()) + mWorkGroupSize - 1u) / mWorkGroupSize);
		GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT));
		mControlBuffer->markUsed();
	}

	void TrailSystem::simulate()
	{
		initialise();
		auto const now = chrono::steady_clock::now();
		float deltaSeconds = 0.0f;
		if (mHasLastSimulationTime)
			deltaSeconds = chrono::duration<float>(now - mLastSimulationTime).count();
		mLastSimulationTime = now;
		mHasLastSimulationTime = true;
		if (mAvailable)
		{
			GpuDebugScope scope("Trails: update position histories");
			simulate(deltaSeconds);
		}
		else
		{
			mSimulationSeconds += clampParticleDeltaSeconds(deltaSeconds);
			reclaimStoppedTrails();
		}
	}

	void TrailSystem::render(ParticleBlendClass blendClass, ResourcePtr const& sceneDepth)
	{
		render(blendClass, dynamic_cast<RenderTexture*>(sceneDepth.get()));
	}

	void TrailSystem::render(ParticleBlendClass blendClass, RenderTexture* sceneDepth)
	{
		initialise();
		if (!mAvailable || !mBuffersAllocated || mControls.empty()) return;
		if (blendClass != ParticleBlendClass::Additive && blendClass != ParticleBlendClass::Alpha) return;
		GpuDebugScope scope("Trails: draw camera-facing ribbons");
		mwRenderSystem->setDepthWriteState(false, true);
		auto* program = static_cast<ParticleDrawProgram*>(mDrawProgram.get());
		program->use();
		program->setUniform("SCENE_DEPTH", int32_t(0));
		program->setUniform("TRAIL_CURVE_LUT", int32_t(1));
		program->setUniform("HAS_SCENE_DEPTH", int32_t(sceneDepth ? 1 : 0));
		if (sceneDepth) sceneDepth->bindDepth(0u);
		GL_CHECK(glActiveTexture(GL_TEXTURE1));
		GL_CHECK(glBindSampler(1u, 0u));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, mCurveLut));
		mPoints->bindStorage(PointBinding);
		mStates->bindStorage(StateBinding);
		GL_CHECK(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, ControlBinding, mControlBuffer->getBuffer(),
			GLintptr(mControlBuffer->getActiveOffset()), GLsizeiptr(mControls.size() * sizeof(TrailControlData))));
		mIndirectCommands->bindDrawIndirect();
		GL_CHECK(glBindVertexArray(mVertexArray));
		size_t const firstCommand = size_t(uint32_t(blendClass)) * MaxTrailCount;
		auto const offset = reinterpret_cast<void const*>(firstCommand * sizeof(ParticleDrawArraysIndirectCommand));
		GL_CHECK(glMultiDrawArraysIndirect(GL_TRIANGLE_STRIP, offset, GLsizei(mControls.size()),
			sizeof(ParticleDrawArraysIndirectCommand)));
		GL_CHECK(glBindVertexArray(0));
		GL_CHECK(glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0));
		GL_CHECK(glActiveTexture(GL_TEXTURE1));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		if (sceneDepth)
		{
			GL_CHECK(glActiveTexture(GL_TEXTURE0));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
		}
		mControlBuffer->markUsed();
	}
}
