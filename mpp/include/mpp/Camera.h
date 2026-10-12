#pragma once

#include "mpp/Config.h"

#include <cstdint>
#include <memory>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#pragma warning(pop)

namespace mpp
{
	class _MPPAPI Camera
	{
	protected:

		glm::vec3 mPosition;
		
		mutable glm::vec3 mDirection, mUp;

		mutable float mYaw, mPitch, mRoll;

		float mFov, mNear, mFar, mAspectRatio;
		glm::vec2 mProjectionJitterNdc{ 0.0f };
		uint64_t mRevision{ 0 }, mCutRevision{ 0 };

		mutable bool mDirty;

	protected:

		virtual void updateAngles() const;

	public:

		Camera(glm::vec3 const& position, float yaw, float pitch, float roll, float fov, float aspectRatio);

		void setFov(float fov);
		void setAspectRatio(float aspectRatio);

		float getFov() const;
		float getAspectRatio() const;

		void setClipDistances(float _near, float _far);

		float getNearClipDistance() const;

		float getFarClipDistance() const;

		glm::vec3 const& getPosition() const;

		void setLookAt(glm::vec3 const& position, glm::vec3 const& target, glm::vec3 const& up = glm::vec3(0.0f, 1.0f, 0.0f));

		glm::vec3 const& getDirection() const;

		glm::vec3 const& getUp() const;

		// Renderer-driven transient NDC jitter; it is not a scene/document property.
		void setProjectionJitter(glm::vec2 const& jitterNdc);
		glm::vec2 const& getProjectionJitter() const;
		// Call after an intentional discontinuous camera change to invalidate temporal history.
		void markCut();
		uint64_t getRevision() const;
		uint64_t getCutRevision() const;

		virtual glm::mat4 getViewTransform();

		virtual glm::mat4 getProjectionTransform() const;
	};

	// Matrix-backed camera for application-described views. The supplied matrices
	// are returned byte-for-byte; position/direction/up are derived from `view`
	// only for visibility queries and draw sorting. No host Camera is mutated.
	class _MPPAPI VirtualCamera final : public Camera
	{
		glm::mat4 mView{ 1.0f };
		glm::mat4 mProjection{ 1.0f };

	public:
		VirtualCamera(glm::mat4 const& view, glm::mat4 const& projection,
			float nearDistance, float farDistance);
		glm::mat4 getViewTransform() override;
		glm::mat4 getProjectionTransform() const override;
	};

	typedef std::shared_ptr<Camera> CameraPtr;
}