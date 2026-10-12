#include <cstring>
#include <string>
#include <stdexcept>
#include <utility>
#include "mpp/app/WindowSDL.h"

using namespace std;

WindowSDL::WindowSDL(string title) :
	mWindow(nullptr)
	, mContextGL(nullptr)
	, mTitle(std::move(title))
{
}

WindowSDL::~WindowSDL()
{
	destroy();
}

SDL_Window* WindowSDL::getWindow()
{
	return mWindow;
}

void WindowSDL::create(int width, int height, bool fullScreen, bool vsync)
{
	mWidth = width;
	mHeight = height;
	mFullscreen = fullScreen;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	// A resizable OpenGL window exposes the native resize and maximise controls.
	// SDL3 has no position arguments; an undefined position is the default and a
	// fullscreen window ignores its position anyway.
	SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	if (fullScreen) windowFlags |= SDL_WINDOW_FULLSCREEN;
	mWindow = SDL_CreateWindow(mTitle.c_str(), width, height, windowFlags);

	if (!mWindow)
	{
		string err = SDL_GetError();
		throw runtime_error("Could not create SDL window: " + err);
	}

	// An SDL3 fullscreen window is borderless-desktop unless it is given an
	// explicit mode. Pin the closest mode to the requested size so fullscreen
	// still changes video mode the way it did under SDL2.
	SDL_DisplayMode fullscreenMode;
	if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(mWindow), width, height, 0.0f, false, &fullscreenMode))
	{
		SDL_SetWindowFullscreenMode(mWindow, &fullscreenMode);
	}

#ifdef _DEBUG
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

	mContextGL = SDL_GL_CreateContext(mWindow);
	if (!SDL_GL_MakeCurrent(mWindow, mContextGL))
	{
		throw runtime_error("Could not set the OpenGL context.");
	}

	// This window presents through OpenGL, so vsync is the GL swap interval.
	SDL_GL_SetSwapInterval(vsync ? 1 : 0);

	// SDL3 leaves text input disabled until it is asked for.
	SDL_StartTextInput(mWindow);

//	SDL_HideCursor();
}

void WindowSDL::destroy()
{
	if (mContextGL) { SDL_GL_DestroyContext(mContextGL); mContextGL = nullptr; }
	if (mWindow) { SDL_DestroyWindow(mWindow); mWindow = nullptr; }
}
	
void WindowSDL::setFullscreen(bool fullscreen)
{
	SDL_SetWindowFullscreen(mWindow, fullscreen);

	mFullscreen = fullscreen;
}

void WindowSDL::setSize(int width, int height)
{
	// if we're fullscreen, then go to windowed first, to stop other windows in background being resized.
	if (mFullscreen)
	{
		setFullscreen(false);
		SDL_SetWindowSize(mWindow, width, height);
		setFullscreen(true);
	}
	else
	{
		SDL_SetWindowSize(mWindow, width, height);
	}
}

void WindowSDL::show()
{
	SDL_GL_SwapWindow(mWindow);
}

bool WindowSDL::processEvents(InputManager* inputMgr)
{
	inputMgr->clearEvents();
	bool running = true;

	SDL_Event evt;
	while (SDL_PollEvent(&evt))
	{
		InputEvent ie{};

		switch (evt.type)
		{
		case SDL_EVENT_KEY_DOWN:
			ie.type = IET_KeyPressed;
			// ImGui's native/legacy key index must be a scancode, not the
			// SDLK_* value (arrow keys are far outside that index range).
			ie.code = evt.key.scancode;
			ie.key = evt.key.key;
			ie.mod = evt.key.mod;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_KEY_UP:
			ie.type = IET_KeyReleased;
			ie.code = evt.key.scancode;
			ie.key = evt.key.key;
			ie.mod = evt.key.mod;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			ie.type = IET_ButtonPressed;
			ie.b = evt.button.button;
			ie.code = evt.button.which;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			ie.type = IET_ButtonReleased;
			ie.b = evt.button.button;
			ie.code = evt.button.which;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			ie.type = IET_MouseWheel;
			ie.x = -evt.wheel.x;
			ie.y = evt.wheel.y;
			ie.code = evt.wheel.which;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_MOUSE_MOTION:
			ie.type = IET_MouseMotion;
			ie.x = evt.motion.x;
			ie.y = evt.motion.y;
			ie.dx = evt.motion.xrel;
			ie.dy = evt.motion.yrel;
			ie.code = evt.motion.which;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_TEXT_INPUT:
			ie.type = IET_TextInput;
			std::strncpy(ie.s, evt.text.text, sizeof(ie.s) - 1);
			ie.s[sizeof(ie.s) - 1] = '\0';
			inputMgr->addEvent(ie);
			break;

		// SDL3 promotes each window event to its own top-level event type.
		case SDL_EVENT_WINDOW_RESIZED:
			mWidth = evt.window.data1;
			mHeight = evt.window.data2;
			break;

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			ie.type = IET_WindowEnter;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			ie.type = IET_WindowExit;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			ie.type = IET_FocusGained;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			ie.type = IET_FocusLost;
			inputMgr->addEvent(ie);
			break;

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			running = false;
			break;

		case SDL_EVENT_QUIT:
			running = false;
			break;
		}
	}
	return running;
}
