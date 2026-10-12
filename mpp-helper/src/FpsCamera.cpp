#if defined(_MSC_VER) && _MSC_VER < 1930
#  include <vld.h> // Memory tracking
#endif

#include <algorithm>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "mpp/helper/FpsCamera.h"

namespace mpp
{
	namespace helper
	{
		using namespace glm;

		FpsCamera::FpsCamera(vec3 const& position, float yaw, float pitch, float fov, float aspectRatio)
			: Camera(position, yaw, pitch, 0.0f, fov, aspectRatio)
			, mCurPitch(pitch)
		{
		}

		void FpsCamera::yaw(float yaw)
		{
			mYaw += yaw;
			mDirty = true;
		}

		void FpsCamera::pitch(float pitch)
		{
			mPitch += pitch;
			mCurPitch += pitch;

			if (mCurPitch < -85.0f)
			{
				mPitch += (-85.0f - mCurPitch);
				mCurPitch = -85.0f;
			}
			else if (mCurPitch > 85.0f)
			{
				mPitch -= (mCurPitch - 85.0f);
				mCurPitch = 85.0f;
			}

			mDirty = true;
		}

		void FpsCamera::forward(float distance)
		{
			auto d = mDirection;
			d.y = 0.0f;
			normalize(d);

			mPosition += d * distance;
		}

		void FpsCamera::backward(float distance)
		{
			auto d = mDirection;
			d.y = 0.0f;
			normalize(d);

			mPosition -= d * distance;
		}

		void FpsCamera::up(float distance)
		{
			mPosition += mUp * distance;
		}

		void FpsCamera::down(float distance)
		{
			mPosition -= mUp * distance;
		}

		void FpsCamera::left(float distance)
		{
			vec3 right = cross(mDirection, mUp);
			mPosition -= right * distance;
		}

		void FpsCamera::right(float distance)
		{
			vec3 right = cross(mDirection, mUp);
			mPosition += right * distance;
		}

		void FpsCamera::updateAngles() const
		{
			if (mDirty)
			{
				// Pitch
				vec3 right = cross(mDirection, mUp);
				normalize(right);

				mDirection = rotate(mDirection, radians(-mPitch), right);
				normalize(mDirection);

				// Yaw
				mDirection = rotate(mDirection, radians(-mYaw), mUp);
				normalize(mDirection);
			}

			mPitch = mYaw = mRoll = 0.0f;
			mDirty = false;
		}
	}
}