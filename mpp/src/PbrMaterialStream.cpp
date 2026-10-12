#include "mpp/PbrMaterialStream.h"

using namespace std;

namespace mpp
{
	PbrMaterialStream::PbrMaterialStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "PbrMaterial")
	{
	}

	string const& PbrMaterialStream::getName() const { return mName; }
	PbrMaterialSpecification::ProgramOptions const& PbrMaterialStream::getProgramOptions() const { return mSpecification.program; }
	mesh::MeshSpecification const& PbrMaterialStream::getMeshSpecification() { return mSpecification.program.spec; }
	UniformCollection const& PbrMaterialStream::getUniforms() const { return mSpecification.uniforms; }
	PbrMaterialSpecification::PbrSurface const& PbrMaterialStream::getPbrSurface() const { return mSpecification.pbr; }
	bool PbrMaterialStream::usesLegacyFullContract() const { return mSpecification.legacyFullContract; }
	vector<PbrMaterialSpecification::TextureOptions> const& PbrMaterialStream::getTextures() const { return mSpecification.textures; }
}
