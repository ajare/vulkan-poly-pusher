#ifdef MPP_PROFILE_BUILD

#include <format>
#include <iostream>

#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/Profiler.h"
#include "mpp/RenderSystem.h"
#include "mpp/MppException.h"

#define NVPM_INITGUID
#include "NvPmApi.Manager.h"

using namespace std;

namespace mpp
{

	static NVPMContext sNVPMContext(0);

	static NvPmApiManager sNVPMManager;

	/*
	 * Constructor.  Load DLL, create profiler and add counters.
	 *
	 */
	Profiler::Profiler()
	{
		mCounterNames.push_back("OGL frame time");
		mCounterNames.push_back("OGL batch count");
		mCounterNames.push_back("OGL primitive count");
		mCounterNames.push_back("OGL vertex count");
		mCounterNames.push_back("OGL driver time waiting");
		mCounterNames.push_back("OGL driver waits for GPU");
		mCounterNames.push_back("OGL driver waits for kernel");
		mCounterNames.push_back("OGL driver waits for lock");
		mCounterNames.push_back("OGL driver waits for render");
		mCounterNames.push_back("OGL driver waits for swap");
		mCounterNames.push_back("OGL memory allocated");
		mCounterNames.push_back("OGL memory allocated (textures)");
		mCounterNames.push_back("OGL memory allocated (vertex)");

		// Load DLL
		if (sNVPMManager.Construct(L"NvPmApi.Core.dll") != S_OK)
		{
			THROW_MPP_IO("Could not open NvPmApi.Core.dll", __LINE__, __FILE__, __func__);
		}

		// Initialise profiler
		NVPMRESULT nvResult;
		if ((nvResult = sNVPMManager.Api()->Init()) != NVPM_OK)
		{
			THROW_MPP("Could not initialise profiler.", __LINE__, __FILE__, __func__);
		}

		// Create context
		HGLRC ctx = wglGetCurrentContext();

		if ((nvResult = sNVPMManager.Api()->CreateContextFromOGLContext((APIContextHandle)ctx, &sNVPMContext)) != NVPM_OK)
		{
			THROW_MPP("Could not create profiler context.", __LINE__, __FILE__, __func__);
		}

		for (auto counterName: mCounterNames)
		{
			// Enable counters
			NVPMCounterID counterID;
			auto result = sNVPMManager.Api()->GetCounterIDByContext(sNVPMContext, counterName.c_str(), &counterID);
			if (result != NVPM_OK)
			{
				THROW_MPP(std::format("Profiler could not get '{}' counter ID", counterName), __LINE__, __FILE__, __func__);
			}
			else
			{
				if (sNVPMManager.Api()->AddCounter(sNVPMContext, counterID) != NVPM_OK)
				{
					THROW_MPP(std::format("Profiler could not enable '{}' counter ID", counterName), __LINE__, __FILE__, __func__);
				}
			}
		}
	}

	/*
	 * Destructor.
	 *
	 */
	Profiler::~Profiler()
	{
		sNVPMContext = NVPMContext(0);
	}

	/*
	 * Sample the counters.
	 *
	 */
	void Profiler::sample()
	{
		NVPMUINT unused;
		auto result = sNVPMManager.Api()->Sample(sNVPMContext, nullptr, &unused);
	}

	/*
	 * Return the counter values.
	 *
	 */
	map<string, uint64_t> Profiler::getSamples()
	{
		UINT64 value = 8, cycle = 0;

		map<string, uint64_t> results;
		for (auto counterName : mCounterNames)
		{
			auto res = sNVPMManager.Api()->GetCounterValueByName(sNVPMContext, counterName.c_str(), 0, &value, &cycle);

			if (res == NVPM_OK)
			{
				results[counterName] = (uint64_t)value;
			}
			else
			{
				THROW_MPP(std::format("Profiler could not get value of '{}' counter ID", counterName), __LINE__, __FILE__, __func__);
			}
		}

		// Add non-perfkit samples
		//GLint glValue = 0;
		//glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &glValue);
		//results["Total GPU memory"] = (uint64_t)glValue;

		// These just seem to return the total memory
		/*
		glValue = 0;
		glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &glValue);
		results["Total available GPU memory"] = (uint64_t)glValue;

		glValue = 0;
		glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &glValue);
		results["Current available GPU memory"] = (uint64_t)glValue;
		*/
		return results;
	}
	
}

#endif