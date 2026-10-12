#include <SDL3/SDL.h>
#include "mpp/app/TimerSDL.h"

TimerSDL::TimerSDL() :
	mStartTime(0.0f),
	mRunningTime(0.0f)
{
}

void TimerSDL::reset()
{
	// SDL3 returns 64-bit milliseconds.
	mStartTime = (float)(SDL_GetTicks() / 1000.0);
	mRunningTime = 0.0f;
}

float TimerSDL::getDeltaTime() const
{
	float newTime = (float)(SDL_GetTicks() / 1000.0);
	float frameTime = newTime - mStartTime;

	mStartTime = newTime;
	mRunningTime += frameTime;

	return frameTime;
}

float TimerSDL::getTotalTime() const
{
	return mRunningTime;
}
