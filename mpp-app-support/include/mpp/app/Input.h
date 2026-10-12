#pragma once

// Generic key definitions
enum KeyDefinition
{
	Key_Escape = 0,
	Key_1,
	Key_2,
	Key_3,
	Key_4,
	Key_5,
	Key_6,
	Key_7,
	Key_8,
	Key_9,
	Key_0,
	Key_Minus,
	Key_Equals,
	Key_Backspace,
	Key_Tab,
	Key_A,
	Key_B,
	Key_C,
	Key_D,
	Key_E,
	Key_F,
	Key_G,
	Key_H,
	Key_I,
	Key_J,
	Key_K,
	Key_L,
	Key_M,
	Key_N,
	Key_O,
	Key_P,
	Key_Q,
	Key_R,
	Key_S,
	Key_T,
	Key_U,
	Key_V,
	Key_W,
	Key_X,
	Key_Y,
	Key_Z,
	Key_LeftBracket,
	Key_RightBracket,
	Key_Enter,
	Key_LeftControl,
	Key_RightControl,
	Key_Semicolon,
	Key_Apostrophe,
	Key_Tilde,
	Key_LeftShift,
	Key_RightShift,
	Key_Backslash,
	Key_Comma,
	Key_Period,
	Key_Slash,
	Key_LeftAlt,
	Key_RightAlt,
	Key_Space,
	Key_CapsLock,
	Key_NumLock,
	Key_ScrollLock,
	Key_F1,
	Key_F2,
	Key_F3,
	Key_F4,
	Key_F5,
	Key_F6,
	Key_F7,
	Key_F8,
	Key_F9,
	Key_F10,
	Key_F11,
	Key_F12,
	Key_NumpadMultiply,
	Key_NumpadMinus,
	Key_NumpadPlus,
	Key_NumpadDivide,
	Key_Numpad_0,
	Key_Numpad_1,
	Key_Numpad_2,
	Key_Numpad_3,
	Key_Numpad_4,
	Key_Numpad_5,
	Key_Numpad_6,
	Key_Numpad_7,
	Key_Numpad_8,
	Key_Numpad_9,
	Key_NumpadPeriod,
	Key_NumpadEnter,
	Key_PrintScreen,
	Key_Pause,
	Key_Home,
	Key_End,
	Key_UpArrow,
	Key_DownArrow,
	Key_LeftArrow,
	Key_RightArrow,
	Key_PageUp,
	Key_PageDown,
	Key_Insert,
	Key_Delete,
	NUMKEYS
};

enum MouseDefinition
{
	Mouse_Left = 1,
	Mouse_Right,
	Mouse_Middle,
	Mouse_WheelUp,
	Mouse_WheelDown,
	Mouse_Button4,
	Mouse_Button5,
	Mouse_Button6,
	Mouse_Blank, // Because mouse definitions start at 1, need this to 
				 // make NUMBUTTONS correct
	NUMBUTTONS,
	Mouse_LeftDouble,
	Mouse_RightDouble,
	Mouse_MiddleDouble
};

enum InputEventType
{
	IET_KeyPressed = 0,
	IET_KeyReleased,
	IET_ButtonPressed,
	IET_ButtonReleased,
	IET_MouseWheel,
	IET_MouseMotion,
	IET_TextInput,
	IET_WindowEnter,
	IET_WindowExit,
	IET_FocusGained,
	IET_FocusLost
};

struct InputEvent
{
	int type;
	unsigned int code;
	unsigned int key;
	unsigned int mod;
	int b;
	char s[32];
	float x, y, z;
	float dx, dy;
};
