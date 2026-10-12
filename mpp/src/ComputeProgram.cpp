#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/ComputeProgram.h"
#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"
#include "mpp/RenderSystem.h"

using namespace std;

namespace mpp
{
	ComputeProgram::ComputeProgram(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: RawShaderProgram(name, "ComputeProgram", renderSystem, resourceMgr, resourceStream)
	{
	}

	ComputeProgram::~ComputeProgram()
	{
		destroy();
	}

	void ComputeProgram::loadImpl()
	{
		if (!getRenderSystem()->getCaps().supportsCompute)
		{
			THROW_MPP("Compute program '" + getName() + "' cannot be loaded: this context reports no compute shader support.", __LINE__, __FILE__, __func__);
		}
		link({ RawShaderStage::Compute });
	}

	void ComputeProgram::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
	{
		auto const& caps = getRenderSystem()->getCaps();
		uint32_t const requested[3]{ groupsX, groupsY, groupsZ };
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			if (requested[axis] > caps.maxComputeWorkGroupCount[axis])
			{
				THROW_MPP("Compute program '" + getName() + "' requested " + to_string(requested[axis]) +
					" work groups on axis " + to_string(axis) + ", exceeding the GPU maximum of " +
					to_string(caps.maxComputeWorkGroupCount[axis]) + ".", __LINE__, __FILE__, __func__);
			}
		}

		GL_CHECK(glDispatchCompute(groupsX, groupsY, groupsZ));
	}

	void ComputeProgram::dispatchIndirect(size_t byteOffset)
	{
		if (byteOffset % sizeof(uint32_t) != 0)
		{
			THROW_MPP("Compute program '" + getName() + "' received a misaligned indirect dispatch offset.",
				__LINE__, __FILE__, __func__);
		}
		GL_CHECK(glDispatchComputeIndirect(static_cast<GLintptr>(byteOffset)));
	}
}
