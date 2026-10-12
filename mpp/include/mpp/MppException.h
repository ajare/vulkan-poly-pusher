#pragma once

#include "mpp/Config.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4275)
#endif

namespace mpp
{
	class _MPPAPI MppException : public std::runtime_error
	{
		int mLine{ 0 };

		std::string mFile;

		std::string mFunction;

		std::string mTrace;

	public:

		explicit MppException(std::string const& msg, std::string const& trace = "not available");

		MppException(std::string const& msg, int line, std::string const& file, std::string const& function, std::string const& trace = "not available");

		int getLine() const;

		std::string const& getFile() const;

		std::string const& getFunction() const;

		std::string const& getStackTrace() const;
	};

	class _MPPAPI MppIoException : public MppException
	{
	public:

		explicit MppIoException(std::string const& msg);

		MppIoException(std::string const& msg, int line, std::string const& file, std::string const& function);
	};

	class _MPPAPI MppGlException : public MppException
	{
		std::uint32_t mErrorCode;

	private:

		std::string getErrorMessage(std::uint32_t errorCode);

	public:

		explicit MppGlException(std::uint32_t errorCode);

		MppGlException(std::uint32_t errorCode, int line, std::string const& file, std::string const& function);

		std::uint32_t getErrorCode() const;
	};

	class _MPPAPI MppNotImplementedException : public MppException
	{
	public:

		MppNotImplementedException(std::string const& item, int line, std::string const& file, std::string const& function);

		MppNotImplementedException(int line, std::string const& file, std::string const& function);
	};

}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

//		backward::StackTrace st; st.load_here(32); \
//		backward::TraceResolver tr; tr.load_stacktrace(st); \
//		for (size_t i = 0; i < st.size(); ++i)	\
//		{										\
//			backward::ResolvedTrace trace = tr.resolve(st[i]); \
//			std::cout << "#" << i				\
//				<< " " << trace.object_filename \
//				<< " " << trace.object_function \
//				<< " [" << trace.addr << "]"	\
//				<< "\n";						\
//		}										\

#define THROW_MPP(errMsg, line, file, function) \
	do											\
	{											\
		throw mpp::MppException(errMsg, line, file, function); \
	} while (false)

#define THROW_MPP_IO(errMsg, line, file, function) throw mpp::MppIoException(errMsg, line, file, function)
#define THROW_MPP_GL(errCode, line, file, function) throw mpp::MppGlException(errCode, line, file, function)
#define THROW_MPP_NOTIMP(item, line, file, function) throw mpp::MppNotImplementedException(item, line, file, function)
#define THROW_MPP_FN_NOTIMP(line, file, function) throw mpp::MppNotImplementedException(line, file, function)