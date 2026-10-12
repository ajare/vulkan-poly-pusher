#include <mutex>
#include <fstream>
#include <map>
#include <iomanip>
#include <time.h>

#include "utils/FileSystem.h"

#include "mpp/StaticLogger.h"
#include "mpp/MppException.h"

#define LOG_FILENAME "mpp-resources.log"

namespace mpp
{
	using namespace std;

	static bool gsInitialised = false;
	static map<string, bool> gsEnabled;

	void _initialise_static_logger(string const& file)
	{
		// Delete file if it exists
		utils::FileSystem::deleteFile(file);
	}

	void _static_log_message(string const& file, string const& message)
	{
		std::ofstream logger;
		
		logger.open(file, ofstream::out | ofstream::app);

		if (!logger.is_open())
		{
			THROW_MPP("Could not open " + file + " for logging.", __LINE__, __FILE__, __func__);
		}

		struct tm *pTime;
		time_t ctTime; time(&ctTime);

#pragma warning(suppress: 4996)
		pTime = localtime(&ctTime);

		logger << setw(2) << setfill('0') << pTime->tm_hour
			<< ":" << setw(2) << setfill('0') << pTime->tm_min
			<< ":" << setw(2) << setfill('0') << pTime->tm_sec
			<< " " << message << "\n";

		logger.close();
	}

	void enable_static_log(string const& file, bool enabled)
	{
		gsEnabled[file] = enabled;
	}

	bool is_static_log_enabled(string const& file)
	{
		auto it = gsEnabled.find(file);
		if (it == gsEnabled.end())
		{
			return false;
		}

		return it->second;
	}

	static mutex gMutex;

	void static_log_message(string const& file, string const& message)
	{
		const lock_guard<std::mutex> lock(gMutex);

		if (is_static_log_enabled(file))
		{
			if (!gsInitialised)
			{
				_initialise_static_logger(file);
				gsInitialised = true;
			}

			_static_log_message(file, message);
		}
	}
}