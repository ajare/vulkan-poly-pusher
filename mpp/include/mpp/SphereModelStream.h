#pragma once

#include <initializer_list>
#include <map>

#include "mpp/PrimitiveModelStream.h"

namespace mpp
{
	class _MPPAPI SphereModelStream : public PrimitiveModelStream
	{
		double mRadius;
		
		int mResolution;

	private:

		void subdivide(std::vector<double>& positions);

		uint32_t getMidpointIndex(std::map<uint64_t, uint32_t>& midpointIndices, std::vector<double>& positions, uint32_t i0, uint32_t i1);

		void renormalise(std::vector<double>& positions);

		void getUvCoord(double nx, double ny, double nz, double* u, double* v);

	public:

		SphereModelStream(ResourceManager* resourceMgr, mesh::MeshSpecification const& meshSpec, std::string const& material, double radius, int res);
	};
}
