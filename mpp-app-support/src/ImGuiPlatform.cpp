#include <GL/glew.h>

#include <mpp/ProgrammaticTextureStream.h>

#include <stdexcept>

#include <SDL3/SDL.h>

#include "imgui/imgui.h"

#include "mpp/app/ImGuiPlatform.h"

using namespace std;

static ImGuiBackendData* getBackendData()
{
	return ImGui::GetCurrentContext() ? (ImGuiBackendData*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static void setClipboardText(ImGuiContext* context, char const* text)
{
	SDL_SetClipboardText(text);
}

static char const* getClipboardText(ImGuiContext* context)
{
	auto bd = getBackendData();

	if (bd->clipboardTextData)
	{
		SDL_free(bd->clipboardTextData);
	}

	bd->clipboardTextData = SDL_GetClipboardText();
	return bd->clipboardTextData;
}

static void platformSetImeData(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
	if (data->WantVisible)
	{
		SDL_Rect r;
		r.x = (int)data->InputPos.x;
		r.y = (int)data->InputPos.y;
		r.w = 1;
		r.h = (int)data->InputLineHeight;
		// SDL3 scopes the IME area to a window and takes a cursor offset within it.
		SDL_SetTextInputArea(SDL_GetKeyboardFocus(), &r, 0);
	}
}

void imGuiSetup(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, ImGuiBackendData* bd, bool enableDocking, string const& mergedIconFontFilename)
{
	if (ImGui::GetCurrentContext() != nullptr)
	{
		return;
	}

	ImGui::CreateContext();

	// Create backend data
	ImGuiIO& io = ImGui::GetIO();

	io.BackendPlatformUserData = bd;
	io.BackendPlatformName = "SDL3";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

	// Load mouse cursors
	bd->mouseCursors[ImGuiMouseCursor_Arrow] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
	bd->mouseCursors[ImGuiMouseCursor_TextInput] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
	bd->mouseCursors[ImGuiMouseCursor_ResizeAll] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
	bd->mouseCursors[ImGuiMouseCursor_ResizeNS] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
	bd->mouseCursors[ImGuiMouseCursor_ResizeEW] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
	bd->mouseCursors[ImGuiMouseCursor_ResizeNESW] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
	bd->mouseCursors[ImGuiMouseCursor_ResizeNWSE] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
	bd->mouseCursors[ImGuiMouseCursor_Hand] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
	bd->mouseCursors[ImGuiMouseCursor_Wait] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
	bd->mouseCursors[ImGuiMouseCursor_Progress] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_PROGRESS);
	bd->mouseCursors[ImGuiMouseCursor_NotAllowed] = (SDL_Cursor*)SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);

	bd->mouseLastCursor = nullptr;

	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

	platform_io.Platform_SetClipboardTextFn = setClipboardText;
	platform_io.Platform_GetClipboardTextFn = getClipboardText;
	platform_io.Platform_ClipboardUserData = nullptr;
	platform_io.Platform_SetImeDataFn = platformSetImeData;

	// Configure ImGui
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	if (enableDocking)
	{
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}

	// TODO: Set optional io.ConfigFlags values, e.g. 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard' to enable keyboard controls.
	// TODO: Fill optional fields of the io structure later.
	// TODO: Load TTF/OTF fonts if you don't want to use the default font.

	if (!mergedIconFontFilename.empty())
	{
		io.Fonts->AddFontDefault();
		ImFontConfig iconConfig;
		iconConfig.MergeMode = true;
		iconConfig.PixelSnapH = true;
		static ImWchar const iconRanges[] = { 0xe005, 0xf8ff, 0 };
		// The docking snapshot's default font uses an implicit reference size;
		// merged sources must also use an implicit size (0) rather than 13px.
		if (!io.Fonts->AddFontFromFileTTF(mergedIconFontFilename.c_str(), 0.0f, &iconConfig, iconRanges))
		{
			throw std::runtime_error("Could not load merged ImGui icon font '" + mergedIconFontFilename + "'.");
		}
	}

	// Build and load the texture atlas into a texture.
	// This should be lazily created for when we re-enter the state!

	// At this point you've got the texture data and you need to upload that to your graphic system:
	// After we have created the texture, store its pointer/identifier (_in whichever format your engine uses_) in 'io.Fonts->TexID'.
	// This will be passed back to your via the renderer. Basically ImTextureID == void*. Read FAQ for details about ImTextureID.
	// 

	auto fontRes = resourceMgr->getResource("__ImGui_Font__", true);
	if (!fontRes)
	{
		int fontWidth, fontHeight;
		unsigned char* fontData{ nullptr };

		io.Fonts->GetTexDataAsRGBA32(&fontData, &fontWidth, &fontHeight);

		auto fontTextureStr = new mpp::ProgrammaticTextureStream(resourceMgr);

		fontTextureStr->setTarget(mpp::TextureTarget::Texture2D);
		fontTextureStr->setData([fontData, fontWidth, fontHeight](string const&)
		{
			mpp::TextureData data;

			data.width = fontWidth;
			data.height = fontHeight;
			data.bitsPerPixel = 32;
			data.dataType = GL_UNSIGNED_BYTE;
			data.pixelFormat = GL_RGBA;

			size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

			data.data = new uint8_t[dataSize];
			memcpy(data.data, fontData, dataSize);

			return data;
		});

		fontTextureStr->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);

		fontRes = resourceMgr->declareResource("__ImGui_Font__", mpp::ResourceStreamPtr(fontTextureStr)).first;
		fontRes->load();
	}

	io.Fonts->SetTexID((ImTextureID)(intptr_t)fontRes->getId());

	io.DisplaySize.x = (float)renderSystem->getWindowWidth();
	io.DisplaySize.y = (float)renderSystem->getWindowHeight();

	ImGui::StyleColorsDark();
}

void imGuiShutdown(ImGuiBackendData* bd)
{
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	if (bd->clipboardTextData)
	{
		SDL_free(bd->clipboardTextData);
	}

	for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
	{
		SDL_DestroyCursor((SDL_Cursor*)bd->mouseCursors[cursor_n]);
	}

	io.BackendPlatformName = nullptr;
	io.BackendPlatformUserData = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);
}


static void updateMouseData(SDL_Window* window, ImGuiBackendData* bd)
{
	ImGuiIO& io = ImGui::GetIO();

	const bool is_app_focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0; // SDL 2.0.3 and non-windowed systems: single-viewport only

	if (is_app_focused)
	{
		// (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when io.ConfigNavMoveSetMousePos is enabled by user)
		if (io.WantSetMousePos)
		{
			SDL_WarpMouseInWindow(window, io.MousePos.x, io.MousePos.y);
		}
	}
}

static void updateMouseCursor(ImGuiBackendData* bd)
{
	ImGuiIO& io = ImGui::GetIO();
	
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
	{
		return;
	}

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	if (io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None)
	{
		// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
		SDL_HideCursor();
	}
	else
	{
		// Show OS mouse cursor
		auto expected_cursor_v = bd->mouseCursors[imgui_cursor] ? bd->mouseCursors[imgui_cursor] : bd->mouseCursors[ImGuiMouseCursor_Arrow];
		auto expected_cursor = (SDL_Cursor*)expected_cursor_v;

		if (bd->mouseLastCursor != expected_cursor)
		{
			SDL_SetCursor(expected_cursor); // SDL function doesn't have an early out (see #6113)
			bd->mouseLastCursor = expected_cursor;
		}
		
		SDL_ShowCursor();
	}
}

void imGuiNewFrame(SDL_Window* window, ImGuiBackendData* bd)
{
	ImGuiIO& io = ImGui::GetIO();

	int w, h;
	int display_w, display_h;

	SDL_GetWindowSize(window, &w, &h);

	if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
	{
		w = h = 0;
	}
		
	SDL_GetWindowSizeInPixels(window, &display_w, &display_h);

	io.DisplaySize = ImVec2((float)w, (float)h);

	if (w > 0 && h > 0)
	{
		io.DisplayFramebufferScale = ImVec2((float)display_w / w, (float)display_h / h);
	}

	// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
	// (Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. Happens in VMs and Emscripten, see #6189, #6114, #3644)
	static Uint64 frequency = SDL_GetPerformanceFrequency();
	Uint64 current_time = SDL_GetPerformanceCounter();
	if (current_time <= bd->time)
	{
		current_time = bd->time + 1;
	}

	io.DeltaTime = bd->time > 0 ? (float)((double)(current_time - bd->time) / frequency) : (float)(1.0f / 60.0f);
	bd->time = current_time;

	if (bd->mouseLastLeaveFrame && bd->mouseLastLeaveFrame >= ImGui::GetFrameCount() && bd->mouseButtonsDown == 0)
	{
		bd->mouseLastLeaveFrame = 0;
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}

	updateMouseData(window, bd);
	updateMouseCursor(bd);
}

static ImGuiKey keyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode)
{
	switch (keycode)
	{
	case SDLK_TAB: return ImGuiKey_Tab;
	case SDLK_LEFT: return ImGuiKey_LeftArrow;
	case SDLK_RIGHT: return ImGuiKey_RightArrow;
	case SDLK_UP: return ImGuiKey_UpArrow;
	case SDLK_DOWN: return ImGuiKey_DownArrow;
	case SDLK_PAGEUP: return ImGuiKey_PageUp;
	case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
	case SDLK_HOME: return ImGuiKey_Home;
	case SDLK_END: return ImGuiKey_End;
	case SDLK_INSERT: return ImGuiKey_Insert;
	case SDLK_DELETE: return ImGuiKey_Delete;
	case SDLK_BACKSPACE: return ImGuiKey_Backspace;
	case SDLK_SPACE: return ImGuiKey_Space;
	case SDLK_RETURN: return ImGuiKey_Enter;
	case SDLK_ESCAPE: return ImGuiKey_Escape;
		//case SDLK_QUOTE: return ImGuiKey_Apostrophe;
	case SDLK_COMMA: return ImGuiKey_Comma;
		//case SDLK_MINUS: return ImGuiKey_Minus;
	case SDLK_PERIOD: return ImGuiKey_Period;
		//case SDLK_SLASH: return ImGuiKey_Slash;
	case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
		//case SDLK_EQUALS: return ImGuiKey_Equal;
		//case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
		//case SDLK_BACKSLASH: return ImGuiKey_Backslash;
		//case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
		//case SDLK_GRAVE: return ImGuiKey_GraveAccent;
	case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
	case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
	case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
	case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
	case SDLK_PAUSE: return ImGuiKey_Pause;
	case SDLK_KP_0: return ImGuiKey_Keypad0;
	case SDLK_KP_1: return ImGuiKey_Keypad1;
	case SDLK_KP_2: return ImGuiKey_Keypad2;
	case SDLK_KP_3: return ImGuiKey_Keypad3;
	case SDLK_KP_4: return ImGuiKey_Keypad4;
	case SDLK_KP_5: return ImGuiKey_Keypad5;
	case SDLK_KP_6: return ImGuiKey_Keypad6;
	case SDLK_KP_7: return ImGuiKey_Keypad7;
	case SDLK_KP_8: return ImGuiKey_Keypad8;
	case SDLK_KP_9: return ImGuiKey_Keypad9;
	case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
	case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
	case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
	case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
	case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
	case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
	case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
	case SDLK_LSHIFT: return ImGuiKey_LeftShift;
	case SDLK_LALT: return ImGuiKey_LeftAlt;
	case SDLK_LGUI: return ImGuiKey_LeftSuper;
	case SDLK_RCTRL: return ImGuiKey_RightCtrl;
	case SDLK_RSHIFT: return ImGuiKey_RightShift;
	case SDLK_RALT: return ImGuiKey_RightAlt;
	case SDLK_RGUI: return ImGuiKey_RightSuper;
	case SDLK_APPLICATION: return ImGuiKey_Menu;
	case SDLK_0: return ImGuiKey_0;
	case SDLK_1: return ImGuiKey_1;
	case SDLK_2: return ImGuiKey_2;
	case SDLK_3: return ImGuiKey_3;
	case SDLK_4: return ImGuiKey_4;
	case SDLK_5: return ImGuiKey_5;
	case SDLK_6: return ImGuiKey_6;
	case SDLK_7: return ImGuiKey_7;
	case SDLK_8: return ImGuiKey_8;
	case SDLK_9: return ImGuiKey_9;
	case SDLK_A: return ImGuiKey_A;
	case SDLK_B: return ImGuiKey_B;
	case SDLK_C: return ImGuiKey_C;
	case SDLK_D: return ImGuiKey_D;
	case SDLK_E: return ImGuiKey_E;
	case SDLK_F: return ImGuiKey_F;
	case SDLK_G: return ImGuiKey_G;
	case SDLK_H: return ImGuiKey_H;
	case SDLK_I: return ImGuiKey_I;
	case SDLK_J: return ImGuiKey_J;
	case SDLK_K: return ImGuiKey_K;
	case SDLK_L: return ImGuiKey_L;
	case SDLK_M: return ImGuiKey_M;
	case SDLK_N: return ImGuiKey_N;
	case SDLK_O: return ImGuiKey_O;
	case SDLK_P: return ImGuiKey_P;
	case SDLK_Q: return ImGuiKey_Q;
	case SDLK_R: return ImGuiKey_R;
	case SDLK_S: return ImGuiKey_S;
	case SDLK_T: return ImGuiKey_T;
	case SDLK_U: return ImGuiKey_U;
	case SDLK_V: return ImGuiKey_V;
	case SDLK_W: return ImGuiKey_W;
	case SDLK_X: return ImGuiKey_X;
	case SDLK_Y: return ImGuiKey_Y;
	case SDLK_Z: return ImGuiKey_Z;
	case SDLK_F1: return ImGuiKey_F1;
	case SDLK_F2: return ImGuiKey_F2;
	case SDLK_F3: return ImGuiKey_F3;
	case SDLK_F4: return ImGuiKey_F4;
	case SDLK_F5: return ImGuiKey_F5;
	case SDLK_F6: return ImGuiKey_F6;
	case SDLK_F7: return ImGuiKey_F7;
	case SDLK_F8: return ImGuiKey_F8;
	case SDLK_F9: return ImGuiKey_F9;
	case SDLK_F10: return ImGuiKey_F10;
	case SDLK_F11: return ImGuiKey_F11;
	case SDLK_F12: return ImGuiKey_F12;
	case SDLK_F13: return ImGuiKey_F13;
	case SDLK_F14: return ImGuiKey_F14;
	case SDLK_F15: return ImGuiKey_F15;
	case SDLK_F16: return ImGuiKey_F16;
	case SDLK_F17: return ImGuiKey_F17;
	case SDLK_F18: return ImGuiKey_F18;
	case SDLK_F19: return ImGuiKey_F19;
	case SDLK_F20: return ImGuiKey_F20;
	case SDLK_F21: return ImGuiKey_F21;
	case SDLK_F22: return ImGuiKey_F22;
	case SDLK_F23: return ImGuiKey_F23;
	case SDLK_F24: return ImGuiKey_F24;
	case SDLK_AC_BACK: return ImGuiKey_AppBack;
	case SDLK_AC_FORWARD: return ImGuiKey_AppForward;
	default: break;
	}

	// Fallback to scancode
	switch (scancode)
	{
	case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
	case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
	case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
	case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
	case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
	case SDL_SCANCODE_NONUSBACKSLASH: return ImGuiKey_Oem102;
	case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
	case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
	case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
	case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
	case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
	case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
	default: break;
	}
	return ImGuiKey_None;
}

static void updateKeyModifiers(SDL_Keymod sdl_key_mods)
{
	ImGuiIO& io = ImGui::GetIO();

	io.AddKeyEvent(ImGuiMod_Ctrl, (sdl_key_mods & SDL_KMOD_CTRL) != 0);
	io.AddKeyEvent(ImGuiMod_Shift, (sdl_key_mods & SDL_KMOD_SHIFT) != 0);
	io.AddKeyEvent(ImGuiMod_Alt, (sdl_key_mods & SDL_KMOD_ALT) != 0);
	io.AddKeyEvent(ImGuiMod_Super, (sdl_key_mods & SDL_KMOD_GUI) != 0);
}

void imGuiHandleInput(InputManager* inputMgr, ImGuiBackendData* bd)
{
	ImGuiIO& io = ImGui::GetIO();

	auto const& events = inputMgr->getEvents();

	for (auto const& event : events)
	{
		switch (event.type)
		{
		case InputEventType::IET_KeyPressed:
		{
			updateKeyModifiers((SDL_Keymod)event.mod);
			ImGuiKey key = keyEventToImGuiKey(event.key, (SDL_Scancode)event.code);
			if (key != ImGuiKey_None)
			{
				io.AddKeyEvent(key, true);
				io.SetKeyEventNativeData(key, event.key, event.code, event.code);
			}
			break;
		}

		case InputEventType::IET_KeyReleased:
		{
			updateKeyModifiers((SDL_Keymod)event.mod);
			ImGuiKey key = keyEventToImGuiKey(event.key, (SDL_Scancode)event.code);
			if (key != ImGuiKey_None)
			{
				io.AddKeyEvent(key, false);
				io.SetKeyEventNativeData(key, event.key, event.code, event.code);
			}
			break;
		}

		case InputEventType::IET_TextInput:
		{
			io.AddInputCharactersUTF8(event.s);
			break;
		}

		case InputEventType::IET_ButtonPressed:
		{
			int mouse_button = -1;
			if (event.b == SDL_BUTTON_LEFT) { mouse_button = 0; }
			if (event.b == SDL_BUTTON_RIGHT) { mouse_button = 1; }
			if (event.b == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
			if (event.b == SDL_BUTTON_X1) { mouse_button = 3; }
			if (event.b == SDL_BUTTON_X2) { mouse_button = 4; }
			if (mouse_button == -1)
				break;

			io.AddMouseSourceEvent(event.code == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
			io.AddMouseButtonEvent(mouse_button, true);
			bd->mouseButtonsDown |= (1 << mouse_button);
			break;
		}

		case InputEventType::IET_ButtonReleased:
		{
			int mouse_button = -1;
			if (event.b == SDL_BUTTON_LEFT) { mouse_button = 0; }
			if (event.b == SDL_BUTTON_RIGHT) { mouse_button = 1; }
			if (event.b == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
			if (event.b == SDL_BUTTON_X1) { mouse_button = 3; }
			if (event.b == SDL_BUTTON_X2) { mouse_button = 4; }
			if (mouse_button == -1)
				break;

			io.AddMouseSourceEvent(event.code == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
			io.AddMouseButtonEvent(mouse_button, false);
			bd->mouseButtonsDown &= ~(1 << mouse_button);
			break;
		}
		case InputEventType::IET_MouseWheel:
		{
			io.AddMouseSourceEvent(event.code == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
			io.AddMouseWheelEvent(event.x, event.y);
			break;
		}
		case InputEventType::IET_MouseMotion:
		{
			ImVec2 mouse_pos((float)event.x, (float)event.y);
			io.AddMouseSourceEvent(event.code == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
			io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
			break;
		}
		case InputEventType::IET_WindowEnter:
		{
			bd->mouseLastLeaveFrame = 0;
			break;
		}
		case InputEventType::IET_WindowExit:
		{
			bd->mouseLastLeaveFrame = ImGui::GetFrameCount() + 1;
			break;
		}
		case InputEventType::IET_FocusGained:
		{
			io.AddFocusEvent(true);
			break;
		}
		case InputEventType::IET_FocusLost:
		{
			io.AddFocusEvent(false);
			break;
		}
		}
	}
}