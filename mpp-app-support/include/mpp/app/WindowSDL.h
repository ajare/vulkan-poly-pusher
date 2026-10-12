#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "mpp/app/Window.h"

class WindowSDL : public Window
{
	SDL_Window* mWindow;

	SDL_GLContext mContextGL;

	bool mFullscreen;
	std::string mTitle;

public:

	explicit WindowSDL(std::string title = "MassivePolyPusher");
	~WindowSDL();

	SDL_Window* getWindow();

	void create(int width, int height, bool fullScreen, bool vsync);

	void destroy();

	void setFullscreen(bool fullscreen);

	void setSize(int width, int height);
	
	void show();

	bool processEvents(InputManager* inputMgr);
};