#pragma once

#include <stdexcept>
#include <string>

namespace mpp
{
	namespace program
	{

		class MppProgramException : public std::runtime_error
		{
			int mLine{ 0 };

			std::string mFile;

			std::string mFunction;

		public:

			explicit MppProgramException(std::string const& msg)
				: std::runtime_error(msg)
			{
			}

			MppProgramException(std::string const& msg, int line, std::string const& file, std::string const& function)
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

		class MppProgramIoException : public MppProgramException
		{
		public:

			explicit MppProgramIoException(std::string const& msg)
				: MppProgramException(msg)
			{
			}

			MppProgramIoException(std::string const& msg, int line, std::string const& file, std::string const& function)
				: MppProgramException(msg, line, file, function)
			{
			}
		};
	}
}

#define THROW_MPP_PROGRAM(errMsg, line, file, function) throw mpp::program::MppProgramException(errMsg, line, file, function)
#define THROW_MPP_PROGRAM_IO(errMsg, line, file, function) throw mpp::program::MppProgramIoException(errMsg, line, file, function)
