#pragma once

#include <stdexcept>
#include <string>

namespace mpp
{
	namespace mesh
	{
		class MppMeshException : public std::runtime_error
		{
			int mLine{ 0 };

			std::string mFile;

			std::string mFunction;

		public:

			explicit MppMeshException(std::string const& msg)
				: std::runtime_error(msg)
			{
			}

			MppMeshException(std::string const& msg, int line, std::string const& file, std::string const& function)
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

		class MppMeshIoException : public MppMeshException
		{
		public:

			explicit MppMeshIoException(std::string const& msg)
				: MppMeshException(msg)
			{
			}

			MppMeshIoException(std::string const& msg, int line, std::string const& file, std::string const& function)
				: MppMeshException(msg, line, file, function)
			{
			}
		};


		class MppMeshNotImplementedException : public MppMeshException
		{
		public:

			MppMeshNotImplementedException(std::string const& item, int line, std::string const& file, std::string const& function)
				: MppMeshException("Not yet implemented: " + item, line, file, function)
			{
			}

			MppMeshNotImplementedException(int line, std::string const& file, std::string const& function)
				: MppMeshException("Not yet implemented: function " + function, line, file, function)
			{
			}
		};

	}
}

#define THROW_MPP_MESH(errMsg, line, file, function) throw mpp::mesh::MppMeshException(errMsg, line, file, function)
#define THROW_MPP_MESH_IO(errMsg, line, file, function) throw mpp::mesh::MppMeshIoException(errMsg, line, file, function)
#define THROW_MPP_MESH_NOTIMP(item, line, file, function) throw mpp::mesh::MppMeshNotImplementedException(item, line, file, function)
#define THROW_MPP_MESH_FN_NOTIMP(line, file, function) throw mpp::mesh::MppMeshNotImplementedException(line, file, function)	

