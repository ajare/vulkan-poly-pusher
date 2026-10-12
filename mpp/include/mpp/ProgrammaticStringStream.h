#pragma once

#include "mpp/StringStream.h"

namespace mpp
{
	class _MPPAPI ProgrammaticStringStream : public StringStream
	{
		void loadImpl();

	public:

		explicit ProgrammaticStringStream(ResourceManager* resourceMgr);

		void setString(std::string const& data);

		void setFile(std::string const& filepath);
	};
}