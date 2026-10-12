#pragma once

#include <cstddef>

#include "mpp/RawShaderProgram.h"

namespace mpp
{
	// A raw-GLSL compute kernel. Per ADR 0006 the particle simulation kernel is a
	// single program that branches at runtime, so specialisation here is limited
	// to build-time constants such as the work group size.
	class _MPPAPI ComputeProgram : public RawShaderProgram
	{
	protected:

		void loadImpl() override;

	public:

		ComputeProgram(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		~ComputeProgram();

		// Group counts, not invocation counts. Validated against
		// Caps::maxComputeWorkGroupCount; the program must already be in use().
		void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1);

		// Reads three group counts from the currently bound
		// GL_DISPATCH_INDIRECT_BUFFER. GPU-authored counts avoid a readback when the
		// amount of work is itself GPU-owned.
		void dispatchIndirect(size_t byteOffset = 0);
	};
}
