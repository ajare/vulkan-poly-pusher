#pragma once

#include <vector>

#include "mpp/Resource.h"
#include "mpp/UniformCollection.h"

namespace mpp
{
	// A fullscreen post-effect's shader/parameter resource. Deliberately not a
	// Material subclass -- Material (see mpp/Material.h) is the shared base for
	// *surface* materials (ShadingModel, double-sidedness, transparency), none
	// of which apply to a 2D fullscreen quad. PostEffectMaterial wraps a Program
	// (its compiled shader) plus the sampler slots and default uniform values a
	// FullscreenEffectPass binds when executing the effect as one entry in a
	// PbrPipelineDocument's post-effect chain.
	class _MPPAPI PostEffectMaterial final : public Resource
	{
		ResourcePtr mProgram;
		UniformCollection mUniforms;
		std::vector<std::string> mSamplerSlots;

	protected:
		void createImpl() override;
		void destroyImpl() override;
		void loadImpl() override;
		void unloadImpl() override;

	public:
		PostEffectMaterial(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);
		~PostEffectMaterial() override;

		ResourcePtr getProgram();
		UniformCollection const& getUniforms() const;
		std::vector<std::string> const& getSamplerSlots() const;
		// Index into the underlying Program's sampler table for a declared slot
		// name, or -1 if the slot isn't declared/bound. Mirrors
		// Program::getSamplerUnit's role for surface materials.
		int getSamplerUnit(std::string const& samplerSlot) const;
	};
}
