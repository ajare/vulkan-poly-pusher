#pragma once

#include <string>
#include <stdexcept>

#include "Config.h"

namespace mpp
{
	namespace resource_parsers
	{

		class MppResourceParsersException : public std::runtime_error
		{
			int mLine{ 0 };

			std::string mFile;

			std::string mFunction;

		public:

			explicit MppResourceParsersException(std::string const& msg)
				: std::runtime_error(msg)
			{
			}

			MppResourceParsersException(std::string const& msg, int line, std::string const& file, std::string const& function)
				: std::runtime_error(msg)
				, mLine(line)
				, mFile(file)
				, mFunction(function)
			{
			}

			int getLine() const
			{
				return mLine;
			}

			std::string const& getFile() const
			{
				return mFile;
			}

			std::string const& getFunction() const
			{
				return mFunction;
			}
		};

		class MppResourceParsersIoException : public MppResourceParsersException
		{
		public:

			explicit MppResourceParsersIoException(std::string const& msg)
				: MppResourceParsersException(msg)
			{
			}

			MppResourceParsersIoException(std::string const& msg, int line, std::string const& file, std::string const& function)
				: MppResourceParsersException(msg, line, file, function)
			{
			}
		};


		class MppResourceParsersNotImplementedException : public MppResourceParsersException
		{
		public:

			MppResourceParsersNotImplementedException(std::string const& item, int line, std::string const& file, std::string const& function)
				: MppResourceParsersException("Not yet implemented: " + item, line, file, function)
			{
			}

			MppResourceParsersNotImplementedException(int line, std::string const& file, std::string const& function)
				: MppResourceParsersException("Not yet implemented: function " + function, line, file, function)
			{
			}
		};
	}
}

#define THROW_MPP_RESOURCE_PARSERS(errMsg, line, file, function) throw mpp::resource_parsers::MppResourceParsersException(errMsg, line, file, function)
#define THROW_MPP_RESOURCE_PARSERS_IO(errMsg, line, file, function) throw mpp::resource_parsers::MppResourceParsersIoException(errMsg, line, file, function)
#define THROW_MPP_RESOURCE_PARSERS_NOTIMP(item, line, file, function) throw mpp::resource_parsers::MppResourceParsersNotImplementedException(item, line, file, function)
#define THROW_MPP_RESOURCE_PARSERS_FN_NOTIMP(line, file, function) throw mpp::resource_parsers::MppResourceParsersNotImplementedException(line, file, function)	
