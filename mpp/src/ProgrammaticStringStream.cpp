#include <cassert>

#include "utils/FileSystem.h"

#include "mpp/ProgrammaticStringStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{

	ProgrammaticStringStream::ProgrammaticStringStream(ResourceManager* resourceMgr)
		: StringStream(resourceMgr)
	{
	}

	void ProgrammaticStringStream::loadImpl()
	{
		if (mIsFile)
		{
			// Load file
			mData = utils::FileSystem::readTextFile(mFile);
		}
	}

	void ProgrammaticStringStream::setString(string const& data)
	{
		mData = data;
		mIsFile = false;
	}

	void ProgrammaticStringStream::setFile(string const& file)
	{
		mFile = file;
		mIsFile = true;
	}
}