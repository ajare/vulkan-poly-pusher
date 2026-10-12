#include "mpp/app/Window.h"

int Window::getWidth() const
{
	return mWidth;
}

int Window::getHeight() const
{
	return mHeight;
}

void Window::recreate(int width, int height, bool fullScreen, bool vsync)
{
	destroy();
	create(width, height, fullScreen, vsync);
}