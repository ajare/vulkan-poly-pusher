#pragma once

#include <vector>
#include "mpp/app/Input.h"

class InputManager
{
protected:

	// List of input events to parse.
	std::vector<InputEvent> mEvents;

public:
	
	virtual ~InputManager() = default;

	void addEvent(InputEvent const& evt);

	void clearEvents();

	std::vector<InputEvent> const& getEvents() const;

	virtual void update() = 0;
	
	virtual bool keyDown(int key) = 0;

	virtual bool keyPressed(int key) = 0;

	virtual bool keyReleased(int key) = 0;

	virtual bool buttonDown(int button) = 0;

	virtual bool buttonPressed(int button) = 0;

	virtual bool buttonReleased(int button) = 0;

	virtual bool buttonDoubleClick(int button) = 0;

	virtual bool wheelUp() = 0;

	virtual bool wheelDown() = 0;

	virtual void getMousePosition(int* x, int* y) = 0;

	virtual void getMouseMotion(float* motion) = 0;

	virtual void getMouseDelta(float* x, float* y) = 0;
};