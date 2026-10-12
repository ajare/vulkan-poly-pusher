#include <GL/glew.h>

#include "mpp/GLErrorCheck.h"
#include "mpp/MppException.h"

namespace mpp
{
	void CheckOpenGLError(char const* stmt, int line, char const* file, char const* function)
	{
		auto err = glGetError();
		if (err != 0)
		{
			THROW_MPP_GL(err, line, file, function);
		}
	}
}