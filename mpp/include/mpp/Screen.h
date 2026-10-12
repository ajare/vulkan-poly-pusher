#pragma once

#include "mpp/Config.h"
#include "mpp/RenderTarget.h"

namespace mpp
{
	class _MPPAPI Screen : public RenderTarget
	{
		void deactivate();

		void activate();

	public:

		Screen(size_t width, size_t height);
	};
}
