#pragma once

#include "mpp/SamplerStream.h"

namespace mpp
{
	class _MPPAPI ProgrammaticSamplerStream : public SamplerStream
	{
	public:

		explicit ProgrammaticSamplerStream(ResourceManager* resourceMgr);

		void setFiltering(SamplerParams::MinFilter minFilter, SamplerParams::MagFilter magFilter);

		void setWrapping(SamplerParams::Wrapping wrapping);

		void setLodMinLevel(float level);

		void setLodMaxLevel(float level);

		void setLodBias(float bias);

		void setMaxAnisotropy(float maxAnisotropy);
	};
}