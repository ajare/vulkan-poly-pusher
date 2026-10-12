#include <cmath>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "mpp/Camera.h"
#include "mpp/MppException.h"

namespace mpp
{
	using namespace glm;

	Camera::Camera(vec3 const& position, float yaw, float pitch, float roll, float fov, float aspectRatio)
		: mPosition(position)
		, mYaw(yaw)
		, mPitch(pitch)
		, mRoll(roll)
		, mFov(fov)
		, mNear(0.1f)
		, mFar(1000.0f)
		, mAspectRatio(aspectRatio)
		, mDirty(true)
	{
		mDirection = vec3(0, 0, -1);
		mUp = vec3(0, 1, 0);
		updateAngles();
	}

	void Camera::setFov(float fov)
	{
		if(mFov!=fov){mFov=fov;++mRevision;}
	}

	void Camera::setAspectRatio(float aspectRatio)
	{
		if(aspectRatio<=0.0f)THROW_MPP("Camera aspect ratio must be positive.",__LINE__,__FILE__,__func__);
		if(mAspectRatio!=aspectRatio){mAspectRatio=aspectRatio;++mRevision;}
	}

	float Camera::getFov() const
	{
		return mFov;
	}

	float Camera::getAspectRatio() const{return mAspectRatio;}

	void Camera::setClipDistances(float _near, float _far)
	{
		if(mNear!=_near||mFar!=_far){mNear=_near;mFar=_far;++mRevision;}
	}

	float Camera::getNearClipDistance() const
	{
		return mNear;
	}

	float Camera::getFarClipDistance() const
	{
		return mFar;
	}

	vec3 const& Camera::getPosition() const
	{
		return mPosition;
	}

	void Camera::setLookAt(vec3 const& position, vec3 const& target, vec3 const& up)
	{
		vec3 direction = target - position;
		if (dot(direction, direction) < 0.000001f || dot(up, up) < 0.000001f)
		{
			return;
		}
		mPosition = position;
		mDirection = normalize(direction);
		mUp = normalize(up);
		mYaw = mPitch = mRoll = 0.0f;
		mDirty = false;
		++mRevision;
	}

	vec3 const& Camera::getDirection() const
	{
		updateAngles();
		return mDirection;
	}

	vec3 const& Camera::getUp() const
	{
		updateAngles();
		return mUp;
	}

	void Camera::setProjectionJitter(vec2 const& jitterNdc){mProjectionJitterNdc=jitterNdc;}
	vec2 const& Camera::getProjectionJitter() const{return mProjectionJitterNdc;}
	void Camera::markCut(){++mCutRevision;++mRevision;}
	uint64_t Camera::getRevision() const{return mRevision;}
	uint64_t Camera::getCutRevision() const{return mCutRevision;}

	void Camera::updateAngles() const
	{
		if (mDirty)
		{
			// Pitch
			vec3 right = cross(mDirection, mUp);
			normalize(right);

			mDirection = rotate(mDirection, radians(-mPitch), right);
			normalize(mDirection);

			mUp = rotate(mUp, radians(mPitch), right);
			normalize(mUp);

			// Yaw
			mDirection = rotate(mDirection, radians(-mYaw), mUp);
			normalize(mDirection);

			// Roll
			mUp = rotate(mUp, radians(-mRoll), mDirection);
			normalize(mUp);
		}

		mPitch = mYaw = mRoll = 0.0f;
		mDirty = false;
	}

	mat4 Camera::getViewTransform()
	{
		updateAngles();

		return lookAt(mPosition, mPosition + mDirection, mUp);
	}

	mat4 Camera::getProjectionTransform() const
	{
		auto projection=perspective(radians(mFov),mAspectRatio,mNear,mFar);projection[2][0]+=mProjectionJitterNdc.x;projection[2][1]+=mProjectionJitterNdc.y;return projection;
	}

	VirtualCamera::VirtualCamera(mat4 const& view, mat4 const& projection,
		float nearDistance, float farDistance)
		: Camera(vec3(0.0f), 0.0f, 0.0f, 0.0f, 60.0f, 1.0f)
		, mView(view)
		, mProjection(projection)
	{
		if (!std::isfinite(nearDistance) || !std::isfinite(farDistance) ||
			!(nearDistance > 0.0f) || !(farDistance > nearDistance))
			THROW_MPP("A virtual camera requires finite, ordered positive clip distances.", __LINE__, __FILE__, __func__);
		if (abs(determinant(view)) < 0.000001f)
			THROW_MPP("A virtual camera view transform must be invertible.", __LINE__, __FILE__, __func__);
		auto const worldFromView = inverse(view);
		auto const position = vec3(worldFromView[3]) / worldFromView[3].w;
		auto const direction = normalize(-vec3(worldFromView[2]));
		auto const up = normalize(vec3(worldFromView[1]));
		setLookAt(position, position + direction, up);
		setClipDistances(nearDistance, farDistance);
	}

	mat4 VirtualCamera::getViewTransform()
	{
		return mView;
	}

	mat4 VirtualCamera::getProjectionTransform() const
	{
		return mProjection;
	}
}