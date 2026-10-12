#pragma once

#include "mpp/RenderGraphStream.h"
#include "mpp/resource-parsers/Config.h"
#include "mpp/resource-parsers/FileStream.h"

namespace mpp
{
	namespace resource_parsers
	{
		// Resource-stream adapter for the nested RenderGraph XML topology format.
		class _MPPRESOURCEPARSERSAPI FileRenderGraphStream : public RenderGraphStream, public FileStream
		{
		protected:
			void loadImpl() override;

		public:
			FileRenderGraphStream(ResourceManager* resourceMgr, std::string const& filepath);
		};
	}
}
