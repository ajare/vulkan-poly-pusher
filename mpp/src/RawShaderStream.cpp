#include "mpp/RawShaderStream.h"

using namespace std;

namespace mpp
{
	RawShaderStream::RawShaderStream(ResourceManager* resourceMgr, string const& type)
		: ResourceStream(resourceMgr, type)
	{
	}

	void RawShaderStream::setSource(RawShaderStage stage, string source)
	{
		mSources[stage] = move(source);
	}

	string const& RawShaderStream::getSource(RawShaderStage stage) const
	{
		static string const empty;
		auto const found = mSources.find(stage);
		return found == mSources.end() ? empty : found->second;
	}

	bool RawShaderStream::hasSource(RawShaderStage stage) const
	{
		auto const found = mSources.find(stage);
		return found != mSources.end() && !found->second.empty();
	}

	void RawShaderStream::setDefine(string const& name, string const& value)
	{
		mDefines[name] = value;
	}

	map<string, string> const& RawShaderStream::getDefines() const
	{
		return mDefines;
	}

	ComputeProgramStream::ComputeProgramStream(ResourceManager* resourceMgr)
		: RawShaderStream(resourceMgr, "ComputeProgram")
	{
	}

	ParticleDrawProgramStream::ParticleDrawProgramStream(ResourceManager* resourceMgr)
		: RawShaderStream(resourceMgr, "ParticleDrawProgram")
	{
	}
}
