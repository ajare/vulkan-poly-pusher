#include "mpp/helper/FreeCamera.h"

namespace mpp
{
	namespace helper
	{
		using namespace glm;

		
		FreeCamera::FreeCamera(vec3 const& position, float yaw, float pitch, float roll, float fov, float aspectRatio)
			: Camera(position, yaw, pitch, roll, fov, aspectRatio)
		{
		}

		void FreeCamera::yaw(float yaw)
		{
			mYaw += yaw;
			mDirty = true;
		}

		void FreeCamera::pitch(float pitch)
		{
			mPitch += pitch;
			mDirty = true;
		}

		void FreeCamera::roll(float roll)
		{
			mRoll += roll;
			mDirty = true;
		}

		void FreeCamera::forward(float distance)
		{
			mPosition += mDirection * distance;
		}

		void FreeCamera::backward(float distance)
		{
			mPosition -= mDirection * distance;
		}

		void FreeCamera::up(float distance)
		{
			mPosition += mUp * distance;
		}

		void FreeCamera::down(float distance)
		{
			mPosition -= mUp * distance;
		}

		void FreeCamera::left(float distance)
		{
			vec3 right = cross(mDirection, mUp);
			mPosition -= right * distance;
		}

		void FreeCamera::right(float distance)
		{
			vec3 right = cross(mDirection, mUp);
			mPosition += right * distance;
		}

	}
}