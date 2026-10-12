#include <algorithm>
#include <limits>
#include <utility>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/matrix_interpolation.hpp>

#include "mpp/SceneModel3d.h"
#include "mpp/Model.h"

using namespace std;

namespace mpp
{

	SceneModel3d::SceneModel3d(ResourcePtr model)
		: ResourceWrangler("SceneModel3d")
		, mModel(model)
	{
		mModel->acquire(this);
		mParams = make_shared<ModelRenderParams>();
		resetTransform();
	}

	SceneModel3d::~SceneModel3d()
	{
		if (mModel)
		{
			mModel->release(this);
		}
	}

	void SceneModel3d::resetTransform()
	{
		glm::mat4 const next(1.0f);
		if (mTransform != next) ++mShadowRevision;
		mTransform = next;
	}

	void SceneModel3d::translate(glm::vec3 const& translate)
	{
		auto const next = glm::translate(mTransform, translate);
		if (mTransform != next) ++mShadowRevision;
		mTransform = next;
	}

	void SceneModel3d::rotateSelf(float angle, glm::vec3 const& axis)
	{
		auto rotMat = glm::axisAngleMatrix(axis, angle);
		auto const next = mTransform * rotMat;
		if (mTransform != next) ++mShadowRevision;
		mTransform = next;
	}

	void SceneModel3d::rotateOrigin(float angle, glm::vec3 const& axis)
	{
		auto rotMat = glm::axisAngleMatrix(axis, angle);
		auto const next = rotMat * mTransform;
		if (mTransform != next) ++mShadowRevision;
		mTransform = next;
	}

	void SceneModel3d::scale(glm::vec3 const& scale)
	{
		auto const next = glm::scale(mTransform, scale);
		if (mTransform != next) ++mShadowRevision;
		mTransform = next;
	}

	void SceneModel3d::setModel(ResourcePtr model)
	{
		if (mModel == model) return;
		if (mModel)
		{
			mModel->release(this);
		}

		mModel = model;
		++mShadowRevision;
	
		if (mModel)
		{
			mModel->acquire(this);
		}
	}

	ResourcePtr SceneModel3d::getModel() const
	{
		return mModel;
	}

	glm::mat4 const& SceneModel3d::getTransform() const
	{
		return mTransform;
	}

	uint64_t SceneModel3d::getShadowRevision() const
	{
		return mShadowRevision;
	}

	bool SceneModel3d::intersectsSphere(glm::vec3 const& centre, float radius) const
	{
		auto const* model = dynamic_cast<Model const*>(mModel.get());
		if (!model || radius < 0.0f) return false;
		// A model that never measured its extents reports the placeholder box at
		// its own local origin. Culling against that would select a caster by the
		// distance from the light to the model's origin rather than to its
		// geometry, dropping a world-sized dynamic model as soon as the light
		// moved further than the radius from that origin. Unknown bounds are
		// unbounded: retain the model and let the depth pass decide.
		if (!model->hasBounds()) return true;
		glm::vec3 localMin, localMax;
		model->getBounds(localMin, localMax);
		glm::vec3 worldMin(std::numeric_limits<float>::max());
		glm::vec3 worldMax(std::numeric_limits<float>::lowest());
		for (uint32_t corner = 0; corner < 8; ++corner)
		{
			glm::vec3 local((corner & 1) ? localMax.x : localMin.x,
				(corner & 2) ? localMax.y : localMin.y, (corner & 4) ? localMax.z : localMin.z);
			glm::vec3 world = glm::vec3(mTransform * glm::vec4(local, 1.0f));
			worldMin = glm::min(worldMin, world);
			worldMax = glm::max(worldMax, world);
		}
		glm::vec3 const nearest = glm::clamp(centre, worldMin, worldMax);
		glm::vec3 const delta = centre - nearest;
		return glm::dot(delta, delta) <= radius * radius;
	}

	shared_ptr<ModelRenderParams> SceneModel3d::getParams()
	{
		return mParams;
	}

	void SceneModel3d::setRenderLayers(vector<string> layers)
	{
		mRenderLayers = std::move(layers);
	}

	vector<string> const& SceneModel3d::getRenderLayers() const
	{
		return mRenderLayers;
	}

	bool SceneModel3d::isInRenderLayer(string const& layer) const
	{
		return find(mRenderLayers.begin(), mRenderLayers.end(), layer) != mRenderLayers.end();
	}

	void SceneModel3d::setDeferToWaterPass(bool defer)
	{
		mDeferToWaterPass = defer;
	}

	bool SceneModel3d::getDeferToWaterPass() const
	{
		return mDeferToWaterPass;
	}
}