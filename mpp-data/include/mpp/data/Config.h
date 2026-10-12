#pragma once

#if defined(_WIN32)
#  if defined(MPP_DATA_DLL_EXPORT)
#    define _MPPDATAAPI __declspec(dllexport)
#  elif defined(MPP_DATA_STATIC_LIB)
#    define _MPPDATAAPI
#  else
#    define _MPPDATAAPI __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  if defined(MPP_DATA_DLL_EXPORT)
#    define _MPPDATAAPI __attribute__((visibility("default")))
#  else
#    define _MPPDATAAPI
#  endif
#else
#  define _MPPDATAAPI
#endif

#if defined(_MSC_VER)
#  pragma warning(disable: 4251)
#endif
