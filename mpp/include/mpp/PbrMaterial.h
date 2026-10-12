#pragma once

#include <vector>
#include "mpp/Material.h"
#include "mpp/PbrMaterialSpecification.h"
#include "mpp/PbrMaterialFeatures.h"
#include "mpp/UniformCollection.h"

namespace mpp
{
	_MPPAPI ShadowCasterContract makePbrShadowCasterContract(PbrMaterialSpecification::PbrSurface const& surface);

	class _MPPAPI PbrMaterial final : public Material
	{
		ResourcePtr mProgram;
		UniformCollection mUniforms;
		PbrMaterialSpecification::PbrSurface mPbrSurface;
		PbrMaterialFeatures mFeatures{ 0 };
		std::string mFeatureSummary{ "Uninitialised" };
		std::vector<ResourcePtr> mTextures;

	protected:
		void createImpl() override;
		void destroyImpl() override;
		void loadImpl() override;
		void unloadImpl() override;

	public:
		PbrMaterial(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);
		~PbrMaterial() override;
		ResourcePtr getProgram() override;
		int getNumTextures() const override;
		void setTexture(int index, ResourcePtr texture) override;
		ResourcePtr getTexture(int index) const override;
		void setUniforms() override;
		ShadingModel getShadingModel() const override { return ShadingModel::Pbr; }
		bool isTransparent() const override { return mPbrSurface.alphaMode == PbrMaterialSpecification::PbrAlphaMode::Blend; }
		ShadowCasterContract getShadowCasterContract() const override;
		bool isDoubleSided() const override { return mPbrSurface.doubleSided; }
		PbrMaterialSpecification::PbrSurface const& getSurface() const;
		PbrMaterialFeatures getFeatures() const;
		std::string const& getFeatureSummary() const;
		void validateInstanceUniforms(UniformCollection const& uniforms) const override;
	};
}
