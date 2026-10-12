#include "mpp/PostEffectMaterialStream.h"

using namespace std;

namespace mpp
{
	PostEffectMaterialStream::PostEffectMaterialStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "PostEffectMaterial")
	{
	}

	string const& PostEffectMaterialStream::getName() const { return mName; }
	PostEffectMaterialSpecification::ProgramOptions const& PostEffectMaterialStream::getProgramOptions() const { return mSpecification.program; }
	mesh::MeshSpecification const& PostEffectMaterialStream::getMeshSpecification() { return mSpecification.program.spec; }
	UniformCollection const& PostEffectMaterialStream::getUniforms() const { return mSpecification.uniforms; }
	vector<string> const& PostEffectMaterialStream::getSamplerSlots() const { return mSpecification.samplerSlots; }
}
