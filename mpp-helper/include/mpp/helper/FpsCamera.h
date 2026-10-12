#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

#include "mpp/Camera.h"

#include "Config.h"

namespace mpp
{
	namespace helper
	{

		class _MPPHELPERAPI FpsCamera : public mpp::Camera
		{
			float mCurPitch;

		protected:

			void updateAngles() const override;

		public:

			FpsCamera(glm::vec3 const& position, float yaw, float pitch, float fov, float aspectRatio);

			void yaw(float yaw);

			void pitch(float pitch);

			void forward(float distance);

			void backward(float distance);

			void up(float distance);

			void down(float distance);

			void left(float distance);

			void right(float distance);
		};

	}
}