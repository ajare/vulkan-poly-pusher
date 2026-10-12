#pragma once

#include <cstdint>

//#include "mpp/backward.hpp"

// Platform settings - based off OGRE3D (www.ogre3d.org)
#define MPP_PLATFORM_WINDOWS 1
#define MPP_PLATFORM_LINUX 2
#define MPP_PLATFORM_APPLE 3

#define MPP_COMPILER_MSVC 1
#define MPP_COMPILER_GNUC 2
#define MPP_COMPILER_BORL 3

// Find compiler information
#if defined( _MSC_VER )
#   define MPP_COMPILER MPP_COMPILER_MSVC
#   define MPP_COMP_VER _MSC_VER
#elif defined( __GNUC__ )
#   define MPP_COMPILER MPP_COMPILER_GNUC
#   define MPP_COMP_VER (((__GNUC__)*100) + \
        (__GNUC_MINOR__*10) + \
        __GNUC_PATCHLEVEL__)
#elif defined( __BORLANDC__ )
#   define MPP_COMPILER MPP_COMPILER_BORL
#   define MPP_COMP_VER __BCPLUSPLUS__
#else
#   pragma error "Unknown compiler."

#endif

// Set platform
#if defined( _WIN32 )
#   define MPP_PLATFORM MPP_PLATFORM_WINDOWS
#elif defined( __APPLE_CC__)
#   define MPP_PLATFORM MPP_PLATFORM_APPLE
#else
#   define MPP_PLATFORM MPP_PLATFORM_LINUX
#endif

// DLL Export
#if MPP_PLATFORM == MPP_PLATFORM_WINDOWS
#	if defined(MPP_DLL_EXPORT)
#		define _MPPAPI __declspec( dllexport )
#	elif defined(MPP_STATIC_LIB)
#		define _MPPAPI
#	else
#		if defined(__MINGW32__)
#			define _MPPAPI
#		else
#			define _MPPAPI __declspec( dllimport )
#		endif
#	endif
#elif MPP_PLATFORM == MPP_PLATFORM_LINUX
#	if defined(MPP_DLL_EXPORT)
#		define _MPPAPI __attribute__((visibility("default")))
#	else
#		define _MPPAPI
#	endif
#endif

// Disable warning on non-exported templates.
#if defined(_MSC_VER)
#pragma warning( disable: 4251 )
#endif

#define MPP_UNUSED(x) (void)(x)
