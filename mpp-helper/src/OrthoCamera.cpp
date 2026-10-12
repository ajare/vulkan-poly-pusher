#include "mpp/helper/OrthoCamera.h"

namespace mpp
{
	namespace helper
	{
		using namespace glm;

		OrthoCamera::OrthoCamera(glm::vec2 const& position, size_t viewWidth, size_t viewHeight)
			: Camera(vec3(position.x, 0.0f, position.y), 0.0f, 0.0f, 0.0f, 0.0f, (float)viewWidth / viewHeight)
			, mViewWidth(viewWidth)
			, mViewHeight(viewHeight)
			, mAngle(0.0f)
		{
		}

		void OrthoCamera::setPosition(glm::vec2 const& position)
		{
			mPosition.x = position.x;
			mPosition.y = 0.0f;
			mPosition.z = position.y;
		}

		void OrthoCamera::pan(glm::vec2 const& movement)
		{
			mPosition.x += movement.x;
			mPosition.y = 0.0f;
			mPosition.z += movement.y;
		}

		float OrthoCamera::getAngle() const
		{
			return mAngle;
		}

		void OrthoCamera::setAngle(float angle)
		{
			mAngle = angle;
		}

		glm::mat4 OrthoCamera::getViewTransform()
		{
			mat4 m;
			
			m = translate(m, mPosition);
			return rotate(m, mAngle, vec3(0.0f, 1.0f, 0.0f));
		}

		glm::mat4 OrthoCamera::getProjectionTransform() const
		{
			return glm::ortho(0, (int)mViewWidth, 0, (int)mViewHeight);
		}
	}
}