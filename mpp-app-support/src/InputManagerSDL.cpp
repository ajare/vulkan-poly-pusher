#include <SDL3/SDL.h>
#include "mpp/app/InputManagerSDL.h"

#define DOUBLECLICKTIME 0.5

InputManagerSDL::InputManagerSDL() :
	mMouseMotion(0),
	mMouseDeltaX(0.0f),
	mMouseDeltaY(0.0f)
{
	// Mouse buffer
	for (int i = 0; i < MOUSE_HISTORY_SIZE; ++i)
		mMouseHistory[i] = 0;

	// Keys
	mInputNameLinks["Escape"] = Key_Escape;

	mInputNameLinks["1"] = Key_1;
	mInputNameLinks["2"] = Key_2;
	mInputNameLinks["3"] = Key_3;
	mInputNameLinks["4"] = Key_4;
	mInputNameLinks["5"] = Key_5;
	mInputNameLinks["6"] = Key_6;
	mInputNameLinks["7"] = Key_7;
	mInputNameLinks["8"] = Key_8;
	mInputNameLinks["9"] = Key_9;
	mInputNameLinks["0"] = Key_0;
	mInputNameLinks["-"] = Key_Minus;
	mInputNameLinks["="] = Key_Equals;

	mInputNameLinks["Backspace"] = Key_Backspace;
	mInputNameLinks["Tab"] = Key_Tab;

	mInputNameLinks["Q"] = Key_Q;
	mInputNameLinks["W"] = Key_W;
	mInputNameLinks["E"] = Key_E;
	mInputNameLinks["R"] = Key_R;
	mInputNameLinks["T"] = Key_T;
	mInputNameLinks["Y"] = Key_Y;
	mInputNameLinks["U"] = Key_U;
	mInputNameLinks["I"] = Key_I;
	mInputNameLinks["O"] = Key_O;
	mInputNameLinks["P"] = Key_P;
	mInputNameLinks["["] = Key_LeftBracket;
	mInputNameLinks["]"] = Key_RightBracket;

	mInputNameLinks["Enter"] = Key_Enter;
	mInputNameLinks["Left Control"] = Key_LeftControl;

	mInputNameLinks["A"] = Key_A;
	mInputNameLinks["S"] = Key_S;
	mInputNameLinks["D"] = Key_D;
	mInputNameLinks["F"] = Key_F;
	mInputNameLinks["G"] = Key_G;
	mInputNameLinks["H"] = Key_H;
	mInputNameLinks["J"] = Key_J;
	mInputNameLinks["K"] = Key_K;
	mInputNameLinks["L"] = Key_L;
	mInputNameLinks[";"] = Key_Semicolon;
	mInputNameLinks["'"] = Key_Apostrophe;
	mInputNameLinks["Tilde"] = Key_Tilde;

	mInputNameLinks["Left Shift"] = Key_LeftShift;
	mInputNameLinks["\\"] = Key_Backslash;
	mInputNameLinks["Z"] = Key_Z;
	mInputNameLinks["X"] = Key_X;
	mInputNameLinks["C"] = Key_C;
	mInputNameLinks["V"] = Key_V;
	mInputNameLinks["B"] = Key_B;
	mInputNameLinks["N"] = Key_N;
	mInputNameLinks["M"] = Key_M;
	mInputNameLinks[","] = Key_Comma;
	mInputNameLinks["."] = Key_Period;
	mInputNameLinks["/"] = Key_Slash;
	mInputNameLinks["Right Shift"] = Key_RightShift;

	mInputNameLinks["Numpad *"] = Key_NumpadMultiply;
	mInputNameLinks["Left Alt"] = Key_LeftAlt;
	mInputNameLinks["Space"] = Key_Space;
	mInputNameLinks["Caps Lock"] = Key_CapsLock;

	mInputNameLinks["F1"] = Key_F1;
	mInputNameLinks["F2"] = Key_F2;
	mInputNameLinks["F3"] = Key_F3;
	mInputNameLinks["F4"] = Key_F4;
	mInputNameLinks["F5"] = Key_F5;
	mInputNameLinks["F6"] = Key_F6;
	mInputNameLinks["F7"] = Key_F7;
	mInputNameLinks["F8"] = Key_F8;
	mInputNameLinks["F9"] = Key_F9;
	mInputNameLinks["F10"] = Key_F10;

	mInputNameLinks["Num Lock"] = Key_NumLock;
	mInputNameLinks["Scroll Lock"] = Key_ScrollLock;

	mInputNameLinks["Numpad 7"] = Key_Numpad_7;
	mInputNameLinks["Numpad 8"] = Key_Numpad_8;
	mInputNameLinks["Numpad 9"] = Key_Numpad_9;
	mInputNameLinks["Numpad -"] = Key_NumpadMinus;
	mInputNameLinks["Numpad 4"] = Key_Numpad_4;
	mInputNameLinks["Numpad 5"] = Key_Numpad_5;
	mInputNameLinks["Numpad 6"] = Key_Numpad_6;
	mInputNameLinks["Numpad +"] = Key_NumpadPlus;
	mInputNameLinks["Numpad 1"] = Key_Numpad_1;
	mInputNameLinks["Numpad 2"] = Key_Numpad_2;
	mInputNameLinks["Numpad 3"] = Key_Numpad_3;
	mInputNameLinks["Numpad 0"] = Key_Numpad_0;
	mInputNameLinks["Numpad ."] = Key_NumpadPeriod;

	mInputNameLinks["F11"] = Key_F11;
	mInputNameLinks["F12"] = Key_F12;

	mInputNameLinks["Numpad Enter"] = Key_NumpadEnter;
	mInputNameLinks["Right Control"] = Key_RightControl;
	mInputNameLinks["Numpad /"] = Key_NumpadDivide;

	mInputNameLinks["PrintScreen"] = Key_PrintScreen;
	mInputNameLinks["Right Alt"] = Key_RightAlt;

	mInputNameLinks["Pause"] = Key_Pause;
	mInputNameLinks["Home"] = Key_Home;
	mInputNameLinks["Up Arrow"] = Key_UpArrow;
	mInputNameLinks["Page Up"] = Key_PageUp;
	mInputNameLinks["Left Arrow"] = Key_LeftArrow;
	mInputNameLinks["Right Arrow"] = Key_RightArrow;
	mInputNameLinks["End"] = Key_End;
	mInputNameLinks["Down Arrow"] = Key_DownArrow;
	mInputNameLinks["Page Down"] = Key_PageDown;
	mInputNameLinks["Insert"] = Key_Insert;
	mInputNameLinks["Delete"] = Key_Delete;

	// Mouse buttons
	mInputNameLinks["Mouse Left Button"] = NUMKEYS + Mouse_Left;
	mInputNameLinks["Mouse Right Button"] = NUMKEYS + Mouse_Right;
	mInputNameLinks["Mouse Middle Button"] = NUMKEYS + Mouse_Middle;
	mInputNameLinks["Mouse Wheel Up"] = NUMKEYS + Mouse_WheelUp;
	mInputNameLinks["Mouse Wheel Down"] = NUMKEYS + Mouse_WheelDown;
	mInputNameLinks["Mouse Button 6"] = NUMKEYS + Mouse_Button4;
	mInputNameLinks["Mouse Button 7"] = NUMKEYS + Mouse_Button5;
	mInputNameLinks["Mouse Button 8"] = NUMKEYS + Mouse_Button6;
	mInputNameLinks["Mouse Left Doubleclick"] = NUMKEYS + Mouse_LeftDouble;
	mInputNameLinks["Mouse Right Doubleclick"] = NUMKEYS + Mouse_RightDouble;
	mInputNameLinks["Mouse Middle Doubleclick"] = NUMKEYS + Mouse_MiddleDouble;

	// Set up keys
	mCurKeyBuffer = new unsigned char[NUMKEYS];
	mPrevKeyBuffer = new unsigned char[NUMKEYS];

	// Set up mouse
	mButtonTranslator = new int[NUMBUTTONS];

	mCurMouseBuffer = new unsigned char[NUMBUTTONS];
	mPrevMouseBuffer = new unsigned char[NUMBUTTONS];

	mMouseDoubleClickCounter = new int[NUMBUTTONS];
	mMouseDoubleClickTimer = new int[NUMBUTTONS];

	// Clear buffers now that we have created them
	clearInputBuffers();

	// Set up translation
	setupKeyTranslation();
	setupButtonTranslation();
}

InputManagerSDL::~InputManagerSDL()
{
	delete[] mButtonTranslator;

	delete[] mCurKeyBuffer;
	delete[] mPrevKeyBuffer;

	delete[] mCurMouseBuffer;
	delete[] mPrevMouseBuffer;

	delete[] mMouseDoubleClickCounter;
	delete[] mMouseDoubleClickTimer;
}

void InputManagerSDL::clearInputBuffers()
{
	memset(mCurKeyBuffer, 0, NUMKEYS * sizeof(unsigned char));
	memset(mPrevKeyBuffer, 0, NUMKEYS * sizeof(unsigned char));

	memset(mCurMouseBuffer, 0, NUMBUTTONS * sizeof(unsigned char));
	memset(mPrevMouseBuffer, 0, NUMBUTTONS * sizeof(unsigned char));

	memset(mMouseDoubleClickCounter, 0, NUMBUTTONS * sizeof(int));
	memset(mMouseDoubleClickTimer, 0, NUMBUTTONS * sizeof(int));
}

void InputManagerSDL::setupKeyTranslation()
{
	mKeyTranslator[SDLK_ESCAPE] = Key_Escape;
	mKeyTranslator[SDLK_1] = Key_1;
	mKeyTranslator[SDLK_2] = Key_2;
	mKeyTranslator[SDLK_3] = Key_3;
	mKeyTranslator[SDLK_4] = Key_4;
	mKeyTranslator[SDLK_5] = Key_5;
	mKeyTranslator[SDLK_6] = Key_6;
	mKeyTranslator[SDLK_7] = Key_7;
	mKeyTranslator[SDLK_8] = Key_8;
	mKeyTranslator[SDLK_9] = Key_9;
	mKeyTranslator[SDLK_0] = Key_0;
	mKeyTranslator[SDLK_MINUS] = Key_Minus;
	mKeyTranslator[SDLK_EQUALS] = Key_Equals;
	mKeyTranslator[SDLK_BACKSPACE] = Key_Backspace;
	mKeyTranslator[SDLK_TAB] = Key_Tab;
	mKeyTranslator[SDLK_A] = Key_A;
	mKeyTranslator[SDLK_B] = Key_B;
	mKeyTranslator[SDLK_C] = Key_C;
	mKeyTranslator[SDLK_D] = Key_D;
	mKeyTranslator[SDLK_E] = Key_E;
	mKeyTranslator[SDLK_F] = Key_F;
	mKeyTranslator[SDLK_G] = Key_G;
	mKeyTranslator[SDLK_H] = Key_H;
	mKeyTranslator[SDLK_I] = Key_I;
	mKeyTranslator[SDLK_J] = Key_J;
	mKeyTranslator[SDLK_K] = Key_K;
	mKeyTranslator[SDLK_L] = Key_L;
	mKeyTranslator[SDLK_M] = Key_M;
	mKeyTranslator[SDLK_N] = Key_N;
	mKeyTranslator[SDLK_O] = Key_O;
	mKeyTranslator[SDLK_P] = Key_P;
	mKeyTranslator[SDLK_Q] = Key_Q;
	mKeyTranslator[SDLK_R] = Key_R;
	mKeyTranslator[SDLK_S] = Key_S;
	mKeyTranslator[SDLK_T] = Key_T;
	mKeyTranslator[SDLK_U] = Key_U;
	mKeyTranslator[SDLK_V] = Key_V;
	mKeyTranslator[SDLK_W] = Key_W;
	mKeyTranslator[SDLK_X] = Key_X;
	mKeyTranslator[SDLK_Y] = Key_Y;
	mKeyTranslator[SDLK_Z] = Key_Z;

	mKeyTranslator[SDLK_LEFTBRACKET] = Key_LeftBracket;
	mKeyTranslator[SDLK_RIGHTBRACKET] = Key_RightBracket;
	mKeyTranslator[SDLK_RETURN] = Key_Enter;
	mKeyTranslator[SDLK_LCTRL] = Key_LeftControl;
	mKeyTranslator[SDLK_RCTRL] = Key_RightControl;
	mKeyTranslator[SDLK_SEMICOLON] = Key_Semicolon;
	mKeyTranslator[SDLK_AT] = Key_Apostrophe;
	mKeyTranslator[SDLK_GRAVE] = Key_Tilde;
	mKeyTranslator[SDLK_LSHIFT] = Key_LeftShift;
	mKeyTranslator[SDLK_RSHIFT] = Key_RightShift;
	mKeyTranslator[SDLK_BACKSLASH] = Key_Backslash;
	mKeyTranslator[SDLK_COMMA] = Key_Comma;
	mKeyTranslator[SDLK_PERIOD] = Key_Period;
	mKeyTranslator[SDLK_SLASH] = Key_Slash;
	mKeyTranslator[SDLK_LALT] = Key_LeftAlt;
	mKeyTranslator[SDLK_RALT] = Key_RightAlt;
	mKeyTranslator[SDLK_SPACE] = Key_Space;
	mKeyTranslator[SDLK_CAPSLOCK] = Key_CapsLock;
	mKeyTranslator[SDLK_NUMLOCKCLEAR] = Key_NumLock;
	mKeyTranslator[SDLK_SCROLLLOCK] = Key_ScrollLock;
	mKeyTranslator[SDLK_F1] = Key_F1;
	mKeyTranslator[SDLK_F2] = Key_F2;
	mKeyTranslator[SDLK_F3] = Key_F3;
	mKeyTranslator[SDLK_F4] = Key_F4;
	mKeyTranslator[SDLK_F5] = Key_F5;
	mKeyTranslator[SDLK_F6] = Key_F6;
	mKeyTranslator[SDLK_F7] = Key_F7;
	mKeyTranslator[SDLK_F8] = Key_F8;
	mKeyTranslator[SDLK_F9] = Key_F9;
	mKeyTranslator[SDLK_F10] = Key_F10;
	mKeyTranslator[SDLK_F11] = Key_F11;
	mKeyTranslator[SDLK_F12] = Key_F12;
	mKeyTranslator[SDLK_KP_MULTIPLY] = Key_NumpadMultiply;
	mKeyTranslator[SDLK_KP_MINUS] = Key_NumpadMinus;
	mKeyTranslator[SDLK_KP_PLUS] = Key_NumpadPlus;
	mKeyTranslator[SDLK_KP_DIVIDE] = Key_NumpadDivide;
	mKeyTranslator[SDLK_KP_0] = Key_Numpad_0;
	mKeyTranslator[SDLK_KP_1] = Key_Numpad_1;
	mKeyTranslator[SDLK_KP_2] = Key_Numpad_2;
	mKeyTranslator[SDLK_KP_3] = Key_Numpad_3;
	mKeyTranslator[SDLK_KP_4] = Key_Numpad_4;
	mKeyTranslator[SDLK_KP_5] = Key_Numpad_5;
	mKeyTranslator[SDLK_KP_6] = Key_Numpad_6;
	mKeyTranslator[SDLK_KP_7] = Key_Numpad_7;
	mKeyTranslator[SDLK_KP_8] = Key_Numpad_8;
	mKeyTranslator[SDLK_KP_9] = Key_Numpad_9;
	mKeyTranslator[SDLK_KP_PERIOD] = Key_NumpadPeriod;
	mKeyTranslator[SDLK_KP_ENTER] = Key_NumpadEnter;
	mKeyTranslator[SDLK_PRINTSCREEN] = Key_PrintScreen;
	mKeyTranslator[SDLK_PAUSE] = Key_Pause;
	mKeyTranslator[SDLK_HOME] = Key_Home;
	mKeyTranslator[SDLK_END] = Key_End;
	mKeyTranslator[SDLK_UP] = Key_UpArrow;
	mKeyTranslator[SDLK_DOWN] = Key_DownArrow;
	mKeyTranslator[SDLK_LEFT] = Key_LeftArrow;
	mKeyTranslator[SDLK_RIGHT] = Key_RightArrow;
	mKeyTranslator[SDLK_PAGEUP] = Key_PageUp;
	mKeyTranslator[SDLK_PAGEDOWN] = Key_PageDown;
	mKeyTranslator[SDLK_INSERT] = Key_Insert;
	mKeyTranslator[SDLK_DELETE] = Key_Delete;
}

void InputManagerSDL::setupButtonTranslation()
{
	mButtonTranslator[SDL_BUTTON_LEFT] = Mouse_Left;
	mButtonTranslator[SDL_BUTTON_RIGHT] = Mouse_Right;
	mButtonTranslator[SDL_BUTTON_MIDDLE] = Mouse_Middle;
}

int InputManagerSDL::translateKeyCode(int rawKey)
{
	return mKeyTranslator[rawKey];
}

int InputManagerSDL::translateMouseCode(int rawButton)
{
	return mButtonTranslator[rawButton];
}

void InputManagerSDL::update()
{
	// Clear mouse movement
	mMouseMotion = 0;
	mMouseDeltaX = 0.0f;
	mMouseDeltaY = 0.0f;

	// Clear mouse wheel
	mCurMouseBuffer[Mouse_WheelDown] = 0;
	mCurMouseBuffer[Mouse_WheelUp] = 0;
	
	memcpy(mPrevKeyBuffer, mCurKeyBuffer, NUMKEYS * sizeof(unsigned char));
	memcpy(mPrevMouseBuffer, mCurMouseBuffer, NUMBUTTONS * sizeof(unsigned char));

	// Reset any double-clicks
	for (int i = 0; i < NUMBUTTONS; ++i)
	{
		mMouseDoubleClickTimer[i]--;
		
		if (mMouseDoubleClickCounter[i] == 4 || mMouseDoubleClickTimer[i] == 0)
		{
			mMouseDoubleClickCounter[i] = 0;
			mMouseDoubleClickTimer[i] = 0;
		}
	}

	// Now parse all events
	for (size_t i = 0; i < mEvents.size(); ++i)
	{
		int tCode;
		switch (mEvents[i].type)
		{
		case IET_KeyPressed:
			tCode = translateKeyCode(mEvents[i].key);
			mCurKeyBuffer[tCode] = 1;
			break;

		case IET_KeyReleased:
			tCode = translateKeyCode(mEvents[i].key);
			mCurKeyBuffer[tCode] = 0;
			break;

		case IET_ButtonPressed:
			tCode = translateMouseCode(mEvents[i].b);
			mCurMouseBuffer[tCode] = 1;

			if (mMouseDoubleClickCounter[tCode] == 0)
			{
				mMouseDoubleClickCounter[tCode] = 1;
				mMouseDoubleClickTimer[tCode] = (int)DOUBLECLICKTIME;
			}
			else if (mMouseDoubleClickCounter[tCode] == 2)
			{
				mMouseDoubleClickCounter[tCode] = 3;
			}

			break;

		case IET_ButtonReleased:
			tCode = translateMouseCode(mEvents[i].b);
			mCurMouseBuffer[tCode] = 0;

			if (mMouseDoubleClickCounter[tCode] == 1 ||
				mMouseDoubleClickCounter[tCode] == 3)
			{
				mMouseDoubleClickCounter[tCode] ++;
			}
			break;

		case IET_MouseWheel:
			if (mEvents[i].y > 0.0f)
				mCurMouseBuffer[Mouse_WheelUp] = 1;
			else if (mEvents[i].y < 0.0f)
				mCurMouseBuffer[Mouse_WheelDown] = 1;
			break;

		case IET_MouseMotion:
			mMouseMotion = mEvents[i].code;
			mMouseDeltaX += mEvents[i].dx;
			mMouseDeltaY += mEvents[i].dy;
			break;

		default:
			break;
		}
	}

	// Now clear events
	mEvents.clear();

	// Fill in mouse buffer
	memcpy(mMouseHistory, mMouseHistory + 1, sizeof(int) * (MOUSE_HISTORY_SIZE - 1));
	mMouseHistory[0] = mMouseMotion;
}

bool InputManagerSDL::keyDown(int key)
{
	return (mCurKeyBuffer[key] == 1);
}

bool InputManagerSDL::keyPressed(int key)
{
	return ((mCurKeyBuffer[key] == 1) && (mPrevKeyBuffer[key] == 0));
}

bool InputManagerSDL::keyReleased (int key)
{
	return ((mCurKeyBuffer[key] == 0) && (mPrevKeyBuffer[key] == 1));
}

bool InputManagerSDL::buttonDown(int button)
{
	return (mCurMouseBuffer[button] == 1);
}

bool InputManagerSDL::buttonPressed(int button)
{
	return ((mCurMouseBuffer[button] == 1) && (mPrevMouseBuffer[button] == 0));
}

bool InputManagerSDL::buttonReleased(int button)
{
	return ((mCurMouseBuffer[button] == 0) && (mPrevMouseBuffer[button] == 1));
}

bool InputManagerSDL::buttonDoubleClick(int button)
{
	return (mMouseDoubleClickCounter[button] == 4);
}

bool InputManagerSDL::wheelUp()
{
	return (mCurMouseBuffer[Mouse_WheelUp] == 1);
}

bool InputManagerSDL::wheelDown()
{
	return (mCurMouseBuffer[Mouse_WheelDown] == 1);
}

void InputManagerSDL::getMousePosition(int* x, int* y)
{
	// SDL3 reports the mouse position in floats.
	float fx, fy;
	SDL_GetMouseState(&fx, &fy);
	*x = (int)fx;
	*y = (int)fy;
}

void InputManagerSDL::getMouseDelta(float* x, float* y)
{
	*x = mMouseDeltaX;
	*y = mMouseDeltaY;
}

void InputManagerSDL::getMouseMotion(float* motion)
{
	// average buffer
	float modifier = 0.5f;
	float weight = 0.25f;

	*motion = 0.0f;
	for (int i = 0; i < MOUSE_HISTORY_SIZE; ++i)
	{
		*motion += mMouseHistory[i] * weight;
		weight *= modifier;
	}
}

