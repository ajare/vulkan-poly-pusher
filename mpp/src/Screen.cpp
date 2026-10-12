#include <cassert>

#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/Screen.h"
#include "mpp/GLErrorCheck.h"

namespace mpp
{
	/*
	 * Constructor.
	 *
	 */
	Screen::Screen(size_t width, size_t height)
		: RenderTarget(width, height)
	{
	}

	/*
	 * Set the screen as the active RenderTarget.
	 *
	 */
	void Screen::activate()
	{
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	}

	/*
	 * Unset the screen from being the active RenderTarget.
	 *
	 */
	void Screen::deactivate()
	{
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	}
}

