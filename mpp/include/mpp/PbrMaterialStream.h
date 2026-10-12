#pragma once

#include <string>
#include <vector>

#include "mpp/ResourceStream.h"
#include "mpp/UniformCollection.h"
#include "mpp/PbrMaterialSpecification.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	class _MPPAPI PbrMaterialStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		std::string mName;
		PbrMaterialSpecification mSpecification;

	public:
		explicit PbrMaterialStream(ResourceManager* resourceMgr);
		std::string const& getName() const;
		PbrMaterialSpecification::ProgramOptions const& getProgramOptions() const;
		mesh::MeshSpecification const& getMeshSpecification();
		UniformCollection const& getUniforms() const;
		PbrMaterialSpecification::PbrSurface const& getPbrSurface() const;
		bool usesLegacyFullContract() const;
		std::vector<PbrMaterialSpecification::TextureOptions> const& getTextures() const;
	};
}
