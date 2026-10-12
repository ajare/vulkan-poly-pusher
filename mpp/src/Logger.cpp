#include <iomanip>
#include <time.h>

#include "mpp/Logger.h"

using namespace std;

namespace mpp
{

	/*
	 * Destructor.
	 *
	 */
	Logger::~Logger()
	{
		if (mLog.is_open())
			mLog.close();
	}

	/*
	 * Initialise logger.
	 *
	 */
	bool Logger::initialise(string const& fileName, Level level)
	{
		mFileName = fileName;

		mLog.open(fileName.c_str());

		if (!mLog.is_open())
		{
			// No point adding an error message!
			return false;
		}

		setLevel(level);

		return true;
	}

	void Logger::setLevel(Level level)
	{
		mLevel = level;
	}


	void Logger::_message(string const& suffix)
	{
		struct tm *pTime;
		time_t ctTime; time(&ctTime);

#pragma warning(suppress: 4996)
		pTime = localtime(&ctTime);

		mLog << setw(2) << setfill('0') << pTime->tm_hour
			<< ":" << setw(2) << setfill('0') << pTime->tm_min
			<< ":" << setw(2) << setfill('0') << pTime->tm_sec
			<< " " << suffix << endl;
		
		mLog.flush();
	}

	void Logger::debug(string const& msg)
	{
		if (!mLog.is_open())
		{
			return;
		}

		if (mLevel > Level::Debug)
		{
			return;
		}

		_message("[DEBUG] " + msg);
	}

	void Logger::info(string const& msg)
	{
		if (!mLog.is_open())
		{
			return;
		}

		if (mLevel > Level::Info)
		{
			return;
		}

		_message("[INFO ] " + msg);
	}

	void Logger::warn(string const& msg)
	{
		if (!mLog.is_open())
		{
			return;
		}

		if (mLevel > Level::Warning)
		{
			return;
		}

		_message("[WARN ] " + msg);
	}

	void Logger::error(string const& msg)
	{
		if (!mLog.is_open())
		{
			return;
		}

		if (mLevel > Level::Error)
		{
			return;
		}

		_message("[ERROR] " + msg);
	}

}