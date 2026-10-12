#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/RenderTarget.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{

	/*
	 * Constructor.
	 *
	 */
	RenderTarget::RenderTarget(size_t width, size_t height)
		: mWidth(width)
		, mHeight(height)
	{
	}

	/*
	 * Get render target width.
	 *
	 */
	size_t RenderTarget::getWidth() const
	{
		return mWidth;
	}

	/*
	 * Get render target height.
	 *
	 */
	size_t RenderTarget::getHeight() const
	{
		return mHeight;
	}

	bool RenderTarget::resize(size_t width, size_t height)
	{
		return false;
	}
}