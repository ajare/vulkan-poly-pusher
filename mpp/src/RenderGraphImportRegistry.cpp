#include "mpp/MppException.h"
#include "mpp/RenderGraphImportRegistry.h"

namespace mpp
{
	void RenderGraphImportRegistry::registerImport(std::string const& name, RenderTargetPtr target)
	{
		if (name.empty() || !target)
		{
			THROW_MPP("Render graph import requires a name and target.", __LINE__, __FILE__, __func__);
		}
		mImports[name] = target;
	}

	void RenderGraphImportRegistry::unregisterImport(std::string const& name)
	{
		mImports.erase(name);
	}

	RenderTargetPtr RenderGraphImportRegistry::findImport(std::string const& name) const
	{
		auto const found = mImports.find(name);
		return found == mImports.end() ? nullptr : found->second;
	}
}
