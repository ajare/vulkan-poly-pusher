#pragma once

#include <string>
#include <vector>

#include "mpp/ResourceStream.h"
#include "mpp/UniformCollection.h"
#include "mpp/BasicMaterialSpecification.h"

namespace mpp
{
	class _MPPAPI BasicMaterialStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		std::string mName;
		BasicMaterialSpecification mSpecification;

	public:
		explicit BasicMaterialStream(ResourceManager* resourceMgr);
		std::string const& getName() const;
		BasicMaterialSpecification::ProgramOptions const& getProgramOptions() const;
		mesh::MeshSpecification const& getMeshSpecification();
		UniformCollection const& getUniforms() const;
		ShadowCasterContract const& getShadowCasterContract() const;
		std::vector<BasicMaterialSpecification::TextureOptions> const& getTextures() const;
	};
}
