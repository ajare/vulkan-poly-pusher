#include <memory>

#include "mpp/resource-parsers/FileRenderGraphStream.h"
#include "mpp/resource-parsers/RenderGraphParser.h"

namespace mpp
{
	namespace resource_parsers
	{
		FileRenderGraphStream::FileRenderGraphStream(ResourceManager* resourceMgr, std::string const& filepath)
			: RenderGraphStream(resourceMgr)
			, FileStream(filepath)
		{
		}

		void FileRenderGraphStream::loadImpl()
		{
			setGraph(std::make_shared<RenderGraph>(RenderGraphParser::fromFile(getFilepath())));
		}
	}
}
