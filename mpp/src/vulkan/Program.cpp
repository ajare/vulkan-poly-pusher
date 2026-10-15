// Native Vulkan implementation of the frozen Program public API (#95).
// CPU-side behaviour (stream parsing, reflection metadata, sort IDs) is
// retained from the reference; GPU realization compiles the authored Vulkan
// shader variant (#93) into a descriptor set layout, pipeline layout and
// graphics pipeline derived from reflection and the authored mesh layout
// (#94) — no reconstructed GL state machine. Uniform "locations" are offsets
// into the generated `_mpp_Uniforms` block's native per-frame-context
// storage, never GL interoperability promises. Drawing lands in #96.
#include <algorithm>
#include <format>
#include <stdexcept>

#include "mpp/CompiledProgram.h"
#include "mpp/Program.h"
#include "mpp/ProgramStream.h"
#include "mpp/RenderSystem.h"
#include "mpp/MppException.h"

#include "WindowBridge.h"
#include "PipelineStore.h"
#include "UniformBlockWrite.h"
#include "LegacyGpuValues.h"

using namespace std;

namespace mpp
{
	namespace
	{
		detail::PipelineStore& pipelines()
		{
			return detail::associatedWindow()->pipelines();
		}

		// Pipeline liveness is a diagnostic; after renderer/window teardown the
		// device (and every native pipeline with it) is already gone.
		bool livePipeline(uint32_t id)
		{
			if (!id) return false;
			try { return pipelines().isLive(id); }
			catch (...) { return false; }
		}

		// Retained numeric uniform-type vocabulary for the public
		// getUniformGlType contract; unknown/unsupported shapes report 0.
		uint32_t uniformGlType(ShaderScalar scalar, uint32_t components, uint32_t columns)
		{
			if (columns)
			{
				if (scalar != ShaderScalar::Float) return 0;
				if (columns == 2 && components == 2) return GL_FLOAT_MAT2;
				if (columns == 3 && components == 3) return GL_FLOAT_MAT3;
				if (columns == 4 && components == 4) return GL_FLOAT_MAT4;
				return 0;
			}
			switch (scalar)
			{
			case ShaderScalar::Float:
				return components == 1 ? GL_FLOAT : components == 2 ? GL_FLOAT_VEC2 :
					components == 3 ? GL_FLOAT_VEC3 : GL_FLOAT_VEC4;
			case ShaderScalar::SignedInt:
				return components == 1 ? GL_INT : components == 2 ? GL_INT_VEC2 :
					components == 3 ? GL_INT_VEC3 : GL_INT_VEC4;
			case ShaderScalar::UnsignedInt:
				return components == 1 ? GL_UNSIGNED_INT : components == 2 ? GL_UNSIGNED_INT_VEC2 :
					components == 3 ? GL_UNSIGNED_INT_VEC3 : GL_UNSIGNED_INT_VEC4;
			case ShaderScalar::Boolean:
				return components == 1 ? GL_BOOL : 0;
			}
			return 0;
		}
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
	 * Create the data required for the program from the resource stream.
	 * Retained CPU behaviour; mesh-particle instancing injection belongs to
	 * the undelivered compute path and is never applied here.
	 *
	 */
	void Program::createImpl()
	{
		ProgramStream* pStr = dynamic_cast<ProgramStream*>(getResourceStream().get());
		if (!pStr)
		{
			THROW_MPP("Could not cast to type 'ProgramStream'.", __LINE__, __FILE__, __func__);
		}

		mVertexSource = pStr->getVertexSource();
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
	 * Compile the authored Vulkan shader variant and realize the native
	 * pipeline, descriptor layouts and per-frame-context uniform storage.
	 * Reflection metadata is published only after every operation succeeds.
	 * Recompilation after a runtime source edit flows through the same path:
	 * compileProgram returns fresh SPIR-V and the pipeline is rebuilt.
	 *
	 */
	void Program::loadImpl()
	{
		ProgramStream* pStr = dynamic_cast<ProgramStream*>(getResourceStream().get());
		if (!pStr)
		{
			THROW_MPP("Could not cast to type 'ProgramStream'.", __LINE__, __FILE__, __func__);
		}

		resetReflectionState();

		// Runtime compilation through the retained program-stream seam; errors
		// already name the logical program, stage and offending source line.
		auto compiled = compileProgram(*pStr, getName());

		// Retained CPU reflection metadata, sourced from SPIR-V reflection
		// instead of GL program queries.
		vector<TextureInfo> reflectedTextures;
		for (auto const& texture : pStr->getTextures())
		{
			TextureInfo ti;
			ti.samplerName = texture;
			ti.markedUpName = MPP_PROGRAM_MARKUP_TEXTURE(texture);
			ti.uniformId = -1;
			for (auto const* stage : { &compiled->vertex, &compiled->fragment })
				for (auto const& binding : stage->bindings)
					if (binding.kind == ShaderBinding::Kind::CombinedImageSampler && binding.name == ti.markedUpName)
						ti.uniformId = (int)binding.binding;
			reflectedTextures.push_back(ti);
		}

		vector<VariableInfo> reflectedVertexAttributes;
		for (auto const& inAttrib : pStr->getInAttributes())
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

		map<string, int> reflectedUniformIds;
		map<string, uint32_t> reflectedUniformTypes;
		int viewPosId = -1, modelMatrixId = -1, modelCameraProjectionMatrixId = -1;
		int normalMatrixId = -1, halfWindowSizeId = -1, pointSizeId = -1;

		// The generated `_mpp_Uniforms` block is a sorted union across stages
		// with identical member offsets (#93); either stage's reflection works.
		std::vector<UniformMember> const* uniformMembers = nullptr;
		for (auto const* stage : { &compiled->vertex, &compiled->fragment })
			for (auto const& binding : stage->bindings)
				if (binding.kind == ShaderBinding::Kind::UniformBlock && binding.set == 0 && binding.binding == 0)
					uniformMembers = &binding.members;
		if (uniformMembers)
		{
			for (auto const& member : *uniformMembers)
			{
				auto const base = detail::uniformMemberBase(member.name);
				// Mirror the GL active-uniform convention: arrays are exposed
				// with a "[0]" suffix, scalars and vectors plain.
				string key(base);
				if (!member.arrayDimensions.empty()) key += "[0]";
				reflectedUniformIds[key] = (int)member.offset;
				reflectedUniformTypes[key] = uniformGlType(member.scalar, member.components, member.columns);

				if (base == MPP_PROGRAM_VIEWPOS_NAME) viewPosId = (int)member.offset;
				else if (base == MPP_PROGRAM_MMATRIX_NAME) modelMatrixId = (int)member.offset;
				else if (base == MPP_PROGRAM_MCPMATRIX_NAME) modelCameraProjectionMatrixId = (int)member.offset;
				else if (base == MPP_PROGRAM_NORMALMATRIX_NAME) normalMatrixId = (int)member.offset;
				else if (base == MPP_PROGRAM_HALFWINDOWSIZE_NAME) halfWindowSizeId = (int)member.offset;
				else if (base == MPP_PROGRAM_POINTSIZE_NAME) pointSizeId = (int)member.offset;
			}
		}

		bool fragmentOutputLocationsKnown = true;
		uint64_t fragmentOutputLocationMask = 0;
		for (auto const& output : compiled->fragment.outputs)
			if (output.location < 64) fragmentOutputLocationMask |= uint64_t{ 1 } << output.location;

		// Native realization: shader modules, descriptor set/pipeline layouts
		// from reflection, vertex input from the authored mesh layout, and
		// per-frame-context uniform buffers behind descriptor sets. Throws
		// actionable diagnostics naming this program and the offending
		// binding/attribute; no partial native state survives a failure.
		auto id = pipelines().realizeProgram(compiled, pStr->getMeshSpecification(), getName());

		mMeshSpecification = pStr->getMeshSpecification();
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
		setId(id);

		if (getRenderSystem()) getRenderSystem()->infoMessage("Created program '" + getName() + "'.");
	}

	/*
	 * Retire the native pipeline; destruction completes once in-flight
	 * submissions finish.
	 *
	 */
	void Program::unloadImpl()
	{
		resetReflectionState();
		++mFragmentOutputRevision;

		if (getId() != 0)
		{
			try { pipelines().releaseProgram(getId()); }
			catch (...) { /* renderer/window already torn down */ }
			setId(0);
		}
	}

	/*
	 * Get the native uniform identifier for the marked-up name, or -1. The
	 * identifier is the member's byte offset in the generated uniform block —
	 * a block-relative engine value, not a GL location promise.
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
	 * Get identifier for model matrix, if using one, or -1.
	 *
	 */
	int Program::getModelMatrixId() const
	{
		return mMMatrixId;
	}

	/*
	 * Get identifier for model-camera-projection matrix, if using one, or -1.
	 *
	 */
	int Program::getModelCameraProjectionMatrixId() const
	{
		return mMcpMatrixId;
	}

	/*
	 * Get identifier for normal matrix, if using one, or -1.
	 *
	 */
	int Program::getNormalMatrixId() const
	{
		return mNormalMatrixId;
	}

	/*
	 * Get identifier for half window size uniform, if using it, or -1.
	 *
	 */
	int Program::getHalfWindowSizeId() const
	{
		return mHalfWindowSizeId;
	}

	/*
	 * Get identifier for point size uniform, if using it, or -1.
	 *
	 */
	int Program::getPointSizeId() const
	{
		return mPointSizeId;
	}

	void Program::setSortId(uint32_t sortId)
	{
		mSortId = sortId;
	}

	uint32_t Program::getSortId() const
	{
		return mSortId;
	}

	int Program::getNumSamplers() const
	{
		return (int)mTextures.size();
	}

	string const& Program::getSamplerName(int index) const
	{
		return mTextures[index].samplerName;
	}

	/*
	 * Sampler positions index the program's reflected sampler list; the
	 * compiler assigns each its descriptor binding in the same order.
	 *
	 */
	int Program::getSamplerUnit(string const& name) const
	{
		for (size_t index = 0; index < mTextures.size(); ++index)
			if (mTextures[index].samplerName == name) return (int)index;
		return -1;
	}

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
	 * Select this program's realized pipeline as input for subsequent
	 * renderer operations (the retained MPP-level purpose of bind()). There
	 * is no global device state; the drawing slice (#96) records the
	 * selection into command buffers.
	 *
	 */
	void Program::bind()
	{
		if (getId() == 0)
		{
			THROW_MPP("Program '" + getName() + "' is not loaded; bind() selects a realized pipeline for subsequent renderer operations.", __LINE__, __FILE__, __func__);
		}
		pipelines().select(getId());
	}

	/*
	 * A realized program owns one graphics pipeline (its layout, descriptor
	 * layout, shader modules and uniform storage are internal to it).
	 *
	 */
	int Program::getIdCount() const
	{
		return 1;
	}

	int Program::getLiveIdCount() const
	{
		return livePipeline(getId()) ? 1 : 0;
	}
}
