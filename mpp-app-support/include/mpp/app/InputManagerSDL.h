#pragma once

#include <map>
#include <string>
#include "mpp/app/InputManager.h"

class InputManagerSDL : public InputManager
{
	static const int MOUSE_HISTORY_SIZE = 10;

private:

	std::map<int, int> mKeyTranslator;
	int* mButtonTranslator;

	unsigned char* mCurKeyBuffer;
	unsigned char* mPrevKeyBuffer;

	unsigned char* mCurMouseBuffer;
	unsigned char* mPrevMouseBuffer;
	int* mMouseDoubleClickCounter;
	int* mMouseDoubleClickTimer;

	int mMouseMotion;
	float mMouseDeltaX;
	float mMouseDeltaY;
	int mMouseHistory[MOUSE_HISTORY_SIZE];

	// Text names for keys, for config screens, etc
	std::map<std::string, int> mInputNameLinks;

private:

	int translateKeyCode(int rawKey);

	int translateMouseCode(int rawButton);

	void clearInputBuffers();

	void setupKeyTranslation();

	void setupButtonTranslation();

public:

	InputManagerSDL();

	~InputManagerSDL();

	void update();
	
	bool keyDown(int key);

	bool keyPressed(int key);

	bool keyReleased(int key);

	bool buttonDown(int button);

	bool buttonPressed(int button);

	bool buttonReleased(int button);

	bool buttonDoubleClick(int button);

	bool wheelUp();

	bool wheelDown();

	void getMousePosition(int* x, int* y);

	void getMouseMotion(float* motion);

	void getMouseDelta(float* x, float* y);

};