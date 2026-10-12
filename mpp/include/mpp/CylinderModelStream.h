#pragma once

#include <initializer_list>

#include "mpp/PrimitiveModelStream.h"

namespace mpp
{
	class _MPPAPI CylinderModelStream : public PrimitiveModelStream
	{
		int mResolution;

	public:

		CylinderModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, std::string const& material, float length, float radius1, float radius2, int res);
	};
}
