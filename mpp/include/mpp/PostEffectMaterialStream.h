#pragma once

#include <string>
#include <vector>

#include "mpp/ResourceStream.h"
#include "mpp/UniformCollection.h"
#include "mpp/PostEffectMaterialSpecification.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{
	class _MPPAPI PostEffectMaterialStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		std::string mName;
		PostEffectMaterialSpecification mSpecification;

	public:
		explicit PostEffectMaterialStream(ResourceManager* resourceMgr);
		std::string const& getName() const;
		PostEffectMaterialSpecification::ProgramOptions const& getProgramOptions() const;
		mesh::MeshSpecification const& getMeshSpecification();
		UniformCollection const& getUniforms() const;
		std::vector<std::string> const& getSamplerSlots() const;
	};
}
