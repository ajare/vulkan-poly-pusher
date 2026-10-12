#pragma once

#include <memory>

#include "mpp/Config.h"
#include "mpp/UniformCollection.h"
#include "mpp/ModelRenderParams.h"

namespace mpp
{
	class BatchRenderer
	{
	protected:

		std::shared_ptr<mpp::UniformCollection> mUniforms;

		std::shared_ptr<mpp::ModelRenderParams> mParams;

	public:

		BatchRenderer()
		{
			mUniforms = std::make_shared<UniformCollection>();

			mParams = std::make_shared<ModelRenderParams>();
			mParams->setModelUniforms(mUniforms);
		}

		virtual ~BatchRenderer() = default;

		std::shared_ptr<mpp::ModelRenderParams> getParams()
		{
			return mParams;
		}

		virtual void create() = 0;

		virtual size_t update() = 0;

		virtual void render() = 0;
	};

	typedef std::shared_ptr<BatchRenderer> BatchRendererPtr;
}