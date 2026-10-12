#pragma once

#include "mpp/app/InputManager.h"

class Window
{
protected:

	int mWidth, mHeight;

public:

	virtual ~Window() = default;

	int getWidth() const;

	int getHeight() const;

	void recreate(int width, int height, bool fullScreen, bool vsync);

	virtual void create(int width, int height, bool fullScreen, bool vsync) = 0;

	virtual void destroy() = 0;

	virtual void setFullscreen(bool fullscreen) = 0;

	virtual void setSize(int width, int height) = 0;

	virtual void show() = 0;

	// Returns false when the native window requests application shutdown.
	virtual bool processEvents(InputManager* inputMgr) = 0;
};