#pragma once

#include <vector>

#include "mpp/Material.h"
#include "mpp/UniformCollection.h"

namespace mpp
{
	class _MPPAPI BasicMaterial : public Material
	{
		ResourcePtr mProgram;
		UniformCollection mUniforms;
		ShadowCasterContract mShadowCaster;
		std::vector<ResourcePtr> mTextures;

	protected:
		void createImpl() override;
		void destroyImpl() override;
		void loadImpl() override;
		void unloadImpl() override;

	public:
		BasicMaterial(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);
		~BasicMaterial() override;

		ResourcePtr getProgram() override;
		int getNumTextures() const override;
		void setTexture(int index, ResourcePtr texture) override;
		ResourcePtr getTexture(int index) const override;
		void setUniforms() override;
		ShadingModel getShadingModel() const override { return ShadingModel::Basic; }
		bool isTransparent() const override { return false; }
		ShadowCasterContract getShadowCasterContract() const override { return mShadowCaster; }
		void validateInstanceUniforms(UniformCollection const&) const override {}
	};
}
