#include "mpp/ProgrammaticProgramStream.h"

using namespace std;

namespace mpp
{

	ProgrammaticProgramStream::ProgrammaticProgramStream(ResourceManager* resourceMgr)
		: ProgramStream(resourceMgr)
	{
	}

	void ProgrammaticProgramStream::setParser(shared_ptr<program::Parser> parser)
	{
		mParser = parser;
	}

	void ProgrammaticProgramStream::setAttribs(set<string> const& attribs)
	{
		mAttribs = attribs;
	}
}