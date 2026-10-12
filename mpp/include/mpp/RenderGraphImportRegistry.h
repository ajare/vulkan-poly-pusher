#pragma once

#include <map>
#include <string>

#include "mpp/Config.h"
#include "mpp/RenderTarget.h"

namespace mpp
{
	// Application-owned render targets addressed by immutable XML import names.
	class _MPPAPI RenderGraphImportRegistry
	{
		std::map<std::string, RenderTargetPtr> mImports;

	public:
		void registerImport(std::string const& name, RenderTargetPtr target);
		void unregisterImport(std::string const& name);
		RenderTargetPtr findImport(std::string const& name) const;
	};
}
