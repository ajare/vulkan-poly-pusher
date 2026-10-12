#include <algorithm>

#include <GL/glew.h>

#include "mpp/PostEffectMaterial.h"
#include "mpp/PostEffectMaterialStream.h"
#include "mpp/Program.h"
#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	PostEffectMaterial::PostEffectMaterial(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "PostEffectMaterial", renderSystem, resourceMgr, resourceStream)
	{
	}

	PostEffectMaterial::~PostEffectMaterial()
	{
		destroy();
	}

	void PostEffectMaterial::createImpl()
	{
		auto* mStr = dynamic_cast<PostEffectMaterialStream*>(getResourceStream().get());
		if (!mStr)
			THROW_MPP("Could not cast to type 'PostEffectMaterialStream'", __LINE__, __FILE__, __func__);

		auto resourceMgr = getResourceManager();

		mUniforms = mStr->getUniforms();
		mSamplerSlots = mStr->getSamplerSlots();

		auto const& progOpts = mStr->getProgramOptions();
		if (progOpts.resourceExists)
		{
			mProgram = progOpts.isChild
				? resourceMgr->getResource(getName() + "/Program")
				: resourceMgr->getResource(progOpts.existingResource);
		}
		else
		{
			THROW_MPP("PostEffectMaterial '" + getName() + "' does not reference a program resource.", __LINE__, __FILE__, __func__);
		}

		acquireDependentResource(mProgram);
		mProgram->load();
		string const programLabel = "PostEffect: " + mProgram->getName();
		GL_CHECK(glObjectLabel(GL_PROGRAM, mProgram->getId(), -1, programLabel.c_str()));

		// Every declared sampler slot must exist on the compiled program as a
		// 2D sampler -- the render graph binds scene/prior-stage/depth images to
		// these slots by name (RenderGraphExecutionContext), so a missing or
		// mistyped slot would otherwise fail silently at draw time instead of load time.
		Program* program = static_cast<Program*>(mProgram.get());
		for (auto const& slot : mSamplerSlots)
		{
			int index = -1;
			for (int i = 0; i < program->getNumSamplers(); ++i)
				if (program->getSamplerName(i) == slot) { index = i; break; }
			if (index < 0)
				THROW_MPP("PostEffectMaterial '" + getName() + "' declares sampler slot '" + slot + "' which is absent from its program.", __LINE__, __FILE__, __func__);
			if (program->getSamplerGlType(slot) != GL_SAMPLER_2D)
				THROW_MPP("PostEffectMaterial '" + getName() + "' sampler slot '" + slot + "' must be a sampler2D.", __LINE__, __FILE__, __func__);
		}

		// Every uniform value carried by the material must be a real, bindable
		// uniform on the program -- otherwise a typo'd parameter name silently
		// does nothing when a chain entry tries to override it at runtime.
		for (auto const& [name, value] : mUniforms.getUniformData())
		{
			MPP_UNUSED(value);
			if (program->getUniformId(name) < 0)
				THROW_MPP("PostEffectMaterial '" + getName() + "' declares uniform '" + name + "' which is absent from its program.", __LINE__, __FILE__, __func__);
		}
	}

	void PostEffectMaterial::destroyImpl()
	{
	}

	void PostEffectMaterial::loadImpl()
	{
		mProgram->load();
	}

	void PostEffectMaterial::unloadImpl()
	{
	}

	ResourcePtr PostEffectMaterial::getProgram()
	{
		return mProgram;
	}

	UniformCollection const& PostEffectMaterial::getUniforms() const
	{
		return mUniforms;
	}

	vector<string> const& PostEffectMaterial::getSamplerSlots() const
	{
		return mSamplerSlots;
	}

	int PostEffectMaterial::getSamplerUnit(string const& samplerSlot) const
	{
		if (find(mSamplerSlots.begin(), mSamplerSlots.end(), samplerSlot) == mSamplerSlots.end())
			return -1;
		return static_cast<Program*>(mProgram.get())->getSamplerUnit(samplerSlot);
	}
}
