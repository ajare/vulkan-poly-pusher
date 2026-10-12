#pragma once

#include <string>

#include "mpp/Config.h"

namespace mpp
{
	// RAII GPU event scope consumed by RenderDoc, Nsight and KHR_debug tooling.
	// Backend details live in the library so including this header does not expose
	// the OpenGL loader to clients.
	class _MPPAPI GpuDebugScope
	{
		enum class Backend { None, Khr, Ext };
		Backend mBackend{ Backend::None };

	public:
		explicit GpuDebugScope(std::string const& label);
		~GpuDebugScope();

		GpuDebugScope(GpuDebugScope const&) = delete;
		GpuDebugScope& operator=(GpuDebugScope const&) = delete;
	};

	_MPPAPI void insertGpuDebugMarker(std::string const& label);
}
