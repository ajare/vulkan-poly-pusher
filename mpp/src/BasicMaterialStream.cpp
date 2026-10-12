#include "mpp/BasicMaterialStream.h"

using namespace std;

namespace mpp
{
	BasicMaterialStream::BasicMaterialStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "BasicMaterial")
	{
	}

	string const& BasicMaterialStream::getName() const { return mName; }
	BasicMaterialSpecification::ProgramOptions const& BasicMaterialStream::getProgramOptions() const { return mSpecification.program; }
	mesh::MeshSpecification const& BasicMaterialStream::getMeshSpecification() { return mSpecification.program.spec; }
	UniformCollection const& BasicMaterialStream::getUniforms() const { return mSpecification.uniforms; }
	ShadowCasterContract const& BasicMaterialStream::getShadowCasterContract() const { return mSpecification.shadowCaster; }
	vector<BasicMaterialSpecification::TextureOptions> const& BasicMaterialStream::getTextures() const { return mSpecification.textures; }
}
