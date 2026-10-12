#include <GL/glew.h>

#include "mpp/GpuDebugScope.h"

namespace mpp
{
	GpuDebugScope::GpuDebugScope(std::string const& label)
	{
		if (label.empty()) return;
		if (GLEW_VERSION_4_3 || GLEW_KHR_debug)
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, label.c_str());
			mBackend = Backend::Khr;
		}
		else if (GLEW_EXT_debug_marker)
		{
			glPushGroupMarkerEXT(0, label.c_str());
			mBackend = Backend::Ext;
		}
	}

	GpuDebugScope::~GpuDebugScope()
	{
		if (mBackend == Backend::Khr) glPopDebugGroup();
		else if (mBackend == Backend::Ext) glPopGroupMarkerEXT();
	}

	void insertGpuDebugMarker(std::string const& label)
	{
		if (label.empty()) return;
		if (GLEW_VERSION_4_3 || GLEW_KHR_debug)
			glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, label.c_str());
		else if (GLEW_EXT_debug_marker)
			glInsertEventMarkerEXT(0, label.c_str());
	}
}
