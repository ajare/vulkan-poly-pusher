#pragma once

#include <fstream>
#include <string>

#include "mpp/Config.h"

namespace mpp
{
	class _MPPAPI Logger
	{
	public:

		enum class Level
		{
			Debug,
			Info,
			Warning,
			Error
		};

	private:

		std::ofstream mLog;

		std::string mFileName;

		Level mLevel{ Level::Info };

	private:

		void _message(std::string const& suffix);

	public:

		~Logger();

		bool initialise(std::string const& fileName, Level level);

		void setLevel(Level level);

		void debug(std::string const& msg);

		void info(std::string const& msg);

		void warn(std::string const& msg);

		void error(std::string const& msg);
	};

}