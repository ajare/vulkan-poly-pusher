#pragma once

#include <initializer_list>

#include "mpp/PrimitiveModelStream.h"

namespace mpp
{
	class _MPPAPI GridModelStream : public PrimitiveModelStream
	{
	public:

		GridModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, std::string const& material, double width, double depth, size_t dimX, size_t dimZ, float textureRepeatU = 1.0f, float textureRepeatV = 1.0f);
	};
}
