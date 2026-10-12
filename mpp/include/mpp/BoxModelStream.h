#pragma once

#include <initializer_list>

#include "mpp/PrimitiveModelStream.h"

namespace mpp
{
	class _MPPAPI BoxModelStream : public PrimitiveModelStream
	{
	public:

		BoxModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, std::string const& material, float width, float height, float depth);
	};
}
