#include "mpp/ParticleDrawProgram.h"

using namespace std;

namespace mpp
{
	ParticleDrawProgram::ParticleDrawProgram(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: RawShaderProgram(name, "ParticleDrawProgram", renderSystem, resourceMgr, resourceStream)
	{
	}

	ParticleDrawProgram::~ParticleDrawProgram()
	{
		destroy();
	}

	void ParticleDrawProgram::loadImpl()
	{
		link({ RawShaderStage::Vertex, RawShaderStage::Fragment });
	}
}
