#include "mpp/Config.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <algorithm>
#include <cassert>
#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/RenderSystem.h"
#include "mpp/Sampler.h"
#include "mpp/SamplerStream.h"
#include "mpp/MppException.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	/*
	 * Constructor.
	 *
	 */
	Sampler::Sampler(string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream)
		: Resource(name, "Sampler", renderSystem, resourceMgr, resourceStream)
	{
	}

	/*
	 * Destructor
	 *
	 */
	Sampler::~Sampler()
	{
		destroy();
	}

	/*
	 * Create the data required for the program from the resource stream.
	 *
	 */
	void Sampler::createImpl()
	{
		SamplerStream* sStr = dynamic_cast<SamplerStream*>(getResourceStream().get());
		if (!sStr)
		{
			THROW_MPP("Could not cast to type 'SamplerStream'.", __LINE__, __FILE__, __func__);
		}

		mParams = sStr->getParams();
	}

	/*
	 * Destroy the sampler data.
	 *
	 */
	void Sampler::destroyImpl()
	{
	}

	/*
	 * Create OpenGL sampler.
	 *
	 */
	void Sampler::loadImpl()
	{
		SamplerStream* sStr = dynamic_cast<SamplerStream*>(getResourceStream().get());
		if (!sStr)
		{
			THROW_MPP("Could not cast to type 'SamplerStream'.", __LINE__, __FILE__, __func__);
		}

		uint32_t samplerId;

		// Create: don't need to bind for samplers
		GL_CHECK(glGenSamplers(1, &samplerId));

		GL_CHECK(glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_R, mParams.wrap));
		GL_CHECK(glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_S, mParams.wrap));
		GL_CHECK(glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_T, mParams.wrap));
		GL_CHECK(glSamplerParameteri(samplerId, GL_TEXTURE_MIN_FILTER, mParams.minFilter));
		GL_CHECK(glSamplerParameteri(samplerId, GL_TEXTURE_MAG_FILTER, mParams.magFilter));
		GL_CHECK(glSamplerParameterf(samplerId, GL_TEXTURE_MIN_LOD, mParams.lodMinLevel));
		GL_CHECK(glSamplerParameterf(samplerId, GL_TEXTURE_MAX_LOD, mParams.lodMaxLevel));
		GL_CHECK(glSamplerParameterf(samplerId, GL_TEXTURE_LOD_BIAS, mParams.lodBias));
		auto const maxAnisotropy = getRenderSystem()->getCaps().maxAnisotropy;
		if (maxAnisotropy > 1.0f)
		{
			GL_CHECK(glSamplerParameterf(samplerId, GL_TEXTURE_MAX_ANISOTROPY, std::clamp(mParams.maxAnisotropy, 1.0f, maxAnisotropy)));
		}

		setId(samplerId);
	}

	/*
	 * Destroy the OpenGL sampler.
	 *
	 */
	void Sampler::unloadImpl()
	{
		GLuint id = getId();
		if (id != 0)
		{
			GL_CHECK(glDeleteSamplers(1, &id));
			setId(0);
		}
	}

	/*
	 * How many GL names does this resource manage?
	 *
	 */
	int Sampler::getIdCount() const
	{
		return 1;
	}

	/*
	 * How many GL names are created?
	 *
	 */
	int Sampler::getLiveIdCount() const
	{
		int c;
		GL_CHECK(c = glIsSampler(getId()));
		return c;
	}


	/*
	 * Bind sampler.
	 *
	 */
	void Sampler::bind(uint32_t unit)
	{
		if (!isLoaded())
		{
			load();
		}

		GL_CHECK(glBindSampler(unit, getId()));
	}
}