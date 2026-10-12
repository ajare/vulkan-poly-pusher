#include <cstring>
#include <string>
#include <stdexcept>
#include <utility>
#include "mpp/app/WindowSDL.h"
#include "WindowBridge.h"

using namespace std;

WindowSDL::WindowSDL(string title) :
	mWindow(nullptr)
	, mContextGL(nullptr)
	, mFullscreen(false)
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
    if (mWindow) throw runtime_error("WindowSDL::create requires destroy before recreation");
    if (width <= 0 || height <= 0) throw runtime_error("Window dimensions must be positive");
	mWidth = width;
	mHeight = height;
	mFullscreen = fullScreen;
    SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if (fullScreen) windowFlags |= SDL_WINDOW_FULLSCREEN;
	mWindow = SDL_CreateWindow(mTitle.c_str(), width, height, windowFlags);

	if (!mWindow)
	{
		string err = SDL_GetError();
		throw runtime_error("Could not create SDL window: " + err);
	}

	try {
        mpp::detail::registerWindow(mWindow, vsync);
    } catch (...) {
        SDL_DestroyWindow(mWindow); mWindow = nullptr;
        throw;
    }

	// SDL3 leaves text input disabled until it is asked for.
	SDL_StartTextInput(mWindow);

//	SDL_HideCursor();
}

void WindowSDL::destroy()
{
	mpp::detail::unregisterWindow(mWindow);
	if (mWindow) { SDL_DestroyWindow(mWindow); mWindow = nullptr; }
}
	
void WindowSDL::setFullscreen(bool fullscreen)
{
	if (!SDL_SetWindowFullscreen(mWindow, fullscreen)) throw runtime_error(SDL_GetError());

	mFullscreen = fullscreen;
}

void WindowSDL::setSize(int width, int height)
{
	// if we're fullscreen, then go to windowed first, to stop other windows in background being resized.
	if (mFullscreen)
	{
		setFullscreen(false);
		if (!SDL_SetWindowSize(mWindow, width, height)) throw runtime_error(SDL_GetError());
		setFullscreen(true);
	}
	else
	{
		if (!SDL_SetWindowSize(mWindow, width, height)) throw runtime_error(SDL_GetError());
	}
}

void WindowSDL::show()
{
	mpp::detail::presentWindow(mWindow);
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
