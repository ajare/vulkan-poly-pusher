#include "mpp/app/InputManager.h"

using namespace std;

void InputManager::addEvent(const InputEvent& evt)
{
	mEvents.push_back(evt);
}

void InputManager::clearEvents()
{
	mEvents.clear();
}

vector<InputEvent> const& InputManager::getEvents() const
{
	return mEvents;
}
