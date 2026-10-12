#pragma once

class Timer
{
public:

	virtual ~Timer() = default;

	virtual void reset() = 0;

	virtual float getDeltaTime() const = 0;

	virtual float getTotalTime() const = 0;
};