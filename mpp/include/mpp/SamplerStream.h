#pragma once

#include "mpp/ResourceStream.h"
#include "mpp/SamplerParams.h"

namespace mpp
{
	class _MPPAPI SamplerStream : public ResourceStream
	{
		friend class ResourceStreamSerializer;

	protected:
		SamplerParams mParams;
		void loadImpl();

	public:
		SamplerStream(ResourceManager* resourceMgr);
		virtual ~SamplerStream();
		SamplerParams const& getParams() const;
	};
}
