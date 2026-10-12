#pragma once

#include "mpp/Camera.h"

#include "Config.h"

namespace mpp
{
	namespace helper
	{

		class _MPPHELPERAPI OrthoCamera : public mpp::Camera
		{
			size_t mViewWidth, mViewHeight;

			float mAngle;

		public:

			OrthoCamera(glm::vec2 const& position, size_t viewWidth, size_t viewHeight);

			void setPosition(glm::vec2 const& position);

			void pan(glm::vec2 const& movement);

			float getAngle() const;

			void setAngle(float angle);

			glm::mat4 getViewTransform() override;

			glm::mat4 getProjectionTransform() const override;

		};

	}
}