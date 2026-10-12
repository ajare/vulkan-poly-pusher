#pragma once

#include <memory>

#include "mpp/Config.h"

namespace mpp
{
	class RenderSystem;

	class _MPPAPI RenderTarget
	{
		friend class RenderSystem;

	protected:

		size_t mWidth, mHeight;

	private:

		virtual void deactivate() = 0;

		virtual void activate() = 0;
		
	public:

		RenderTarget(size_t width, size_t height);

		virtual ~RenderTarget() = default;

		virtual size_t getWidth() const;

		virtual size_t getHeight() const;

		// Resize targets which own their storage. Screen targets are recreated by
		// RenderSystem; other target types may return false when not resizable.
		virtual bool resize(size_t width, size_t height);
	};

	typedef std::shared_ptr<RenderTarget> RenderTargetPtr;
}