#pragma once

#include "mpp/RawShaderProgram.h"

namespace mpp
{
	// A raw-GLSL vertex + fragment program with no vertex attributes: billboards
	// are attribute-less instanced quads built from gl_VertexID, with particle
	// data fetched from a shader storage buffer. Per ADR 0006 the draw is the
	// permuted half of the system, so #define specialisation belongs here.
	class _MPPAPI ParticleDrawProgram : public RawShaderProgram
	{
	protected:

		void loadImpl() override;

	public:

		ParticleDrawProgram(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		~ParticleDrawProgram();
	};
}
